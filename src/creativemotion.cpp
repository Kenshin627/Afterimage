#include "creativeeffects.h"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
bool stopped(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }
PatinaResult fail(const QString &message) {
    PatinaResult r;
    r.frame.error = message;
    return r;
}
QImage feedbackSource(const QImage &src, int edge) {
    QSize size = src.size();
    if (std::max(size.width(), size.height()) > edge)
        size.scale(edge, edge, Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    QImage image(size, QImage::Format_RGB32);
    if (!image.isNull()) {
        image.fill(Qt::black);
        QPainter p(&image);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(image.rect(), src);
    }
    return image;
}
QImage feedbackStep(const QImage &base, const QImage &prior, const WorkshopSettings::Feedback &s,
                    double phase) {
    QImage out = base.copy();
    if (out.isNull())
        return {};
    const double scale = std::clamp(s.scale * .01 + .025 * std::sin(phase), .5, .99);
    const double angle = s.rotation * (1. + .55 * std::sin(phase + .8));
    QPointF center((base.width() - 1) * s.x * .01, (base.height() - 1) * s.y * .01);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(center);
    p.rotate(angle);
    p.scale(scale, scale);
    p.translate(-center);
    p.setOpacity(1. - std::clamp(s.decay, 0, 100) * .01);
    p.drawImage(0, 0, prior);
    return out;
}
} // namespace
QImage readWorkshopFrame(const PatinaInput &doc, int index, const CancelFlag &cancel) {
    if (index == 0)
        return doc.image;
    if (index < 0 || index >= doc.timing.delays.size())
        return {};
    QBuffer bytes;
    bytes.setData(doc.gif);
    bytes.open(QIODevice::ReadOnly);
    QImageReader reader(&bytes, "gif");
    QImage frame;
    for (int i = 0; i <= index; ++i) {
        if (stopped(cancel))
            return {};
        frame = reader.read();
        if (frame.isNull())
            return {};
    }
    return frame;
}
QImage featherMask(const QImage &input, int radius, const CancelFlag &cancel) {
    auto mask = input.convertToFormat(QImage::Format_Grayscale8);
    radius = std::clamp(radius, 0, 48);
    if (!radius || mask.isNull())
        return mask;
    const int w = mask.width(), h = mask.height(), divisor = 2 * radius + 1;
    QImage tmp(mask.size(), QImage::Format_Grayscale8), out(mask.size(), QImage::Format_Grayscale8);
    if (tmp.isNull() || out.isNull())
        return {};
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return {};
        auto *src = mask.constScanLine(y);
        auto *dst = tmp.scanLine(y);
        int sum = 0;
        for (int k = -radius; k <= radius; ++k)
            sum += src[std::clamp(k, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            dst[x] = uchar((sum + radius) / divisor);
            sum += src[std::min(w - 1, x + radius + 1)] - src[std::max(0, x - radius)];
        }
    }
    QVector<int> sums(w, 0);
    for (int k = -radius; k <= radius; ++k) {
        auto *p = tmp.constScanLine(std::clamp(k, 0, h - 1));
        for (int x = 0; x < w; ++x)
            sums[x] += p[x];
    }
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return {};
        auto *dst = out.scanLine(y);
        auto *enter = tmp.constScanLine(std::min(h - 1, y + radius + 1)),
             *leave = tmp.constScanLine(std::max(0, y - radius));
        for (int x = 0; x < w; ++x) {
            dst[x] = uchar((sums[x] + radius) / divisor);
            sums[x] += enter[x] - leave[x];
        }
    }
    return out;
}
QImage blendMotion(const QImage &base, const QImage &current, const QImage &mask,
                   const CancelFlag &cancel) {
    if (base.isNull() || base.size() != current.size() || base.size() != mask.size())
        return {};
    QImage out(base.size(), QImage::Format_RGB32);
    if (out.isNull())
        return {};
    for (int y = 0; y < out.height(); ++y) {
        if (stopped(cancel))
            return {};
        const auto *a = reinterpret_cast<const QRgb *>(base.constScanLine(y)),
                   *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
        const auto *m = mask.constScanLine(y);
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            int t = m[x];
            if (!t)
                d[x] = a[x];
            else if (t == 255)
                d[x] = b[x];
            else {
                auto mix = [&](int l, int r) { return (l * (255 - t) + r * t + 127) / 255; };
                d[x] = qRgb(mix(qRed(a[x]), qRed(b[x])), mix(qGreen(a[x]), qGreen(b[x])),
                            mix(qBlue(a[x]), qBlue(b[x])));
            }
        }
    }
    return out;
}
PatinaResult renderFeedback(const PatinaInput &input, const WorkshopSettings::Feedback &s, bool full,
                            const CancelFlag &cancel, const PatinaProgress &progress) {
    constexpr int loopFrames = 24, layers = 8;
    constexpr double tau = 6.283185307179586;
    const bool motion = input.animated();
    const int n = motion ? int(input.timing.delays.size()) : loopFrames, count = full ? n : 1;
    QBuffer bytes;
    bytes.setData(input.gif);
    bytes.open(QIODevice::ReadOnly);
    QImageReader reader(&bytes, "gif");
    QImage still = feedbackSource(input.image, std::clamp(s.edge, 160, 1280)), previous;
    if (still.isNull())
        return fail("无法创建反馈画布。");
    std::unique_ptr<GifEncoder> encoder;
    if (full)
        encoder = std::make_unique<GifEncoder>(still.size(), motion ? input.timing.repeats : 0);
    PatinaResult result;
    if (progress)
        progress(0, count);
    for (int i = 0; i < count; ++i) {
        if (stopped(cancel))
            return {};
        QImage base = still;
        if (motion) {
            const auto frame = reader.read();
            if (frame.isNull())
                return fail("GIF 帧解码失败。");
            base = feedbackSource(frame, s.edge);
        }
        if (base.isNull())
            return fail("内存不足，无法创建反馈帧。");
        QImage out;
        if (!motion || previous.isNull()) {
            // Sampling periodic transform phases at each recursion depth closes the still-image loop.
            out = base;
            for (int j = layers - 1; j >= 0; --j) {
                if (stopped(cancel))
                    return {};
                out = feedbackStep(base, out, s, tau * (i - j) / n);
                if (out.isNull())
                    return fail("无法生成反馈图层。");
            }
        } else
            out = feedbackStep(base, previous, s, tau * i / n);
        if (out.isNull())
            return fail("无法合成反馈画面。");
        if (motion)
            previous = out;
        if (i == 0) {
            result.frame.original = input.image;
            result.frame.processed = out;
        }
        if (encoder && !encoder->appendFrame(out, motion ? input.timing.delays[i] : 8))
            return fail("反馈动画编码失败。");
        if (progress)
            progress(i + 1, count);
    }
    if (encoder)
        result.gif = encoder->finish();
    return result;
}
