#include "workshop.h"
#include "workshopspatial.h"
#include "creativeeffects.h"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

bool requiresMotion(WorkshopEffect effect) {
    return effect == WorkshopEffect::TimeSlice || effect == WorkshopEffect::Exposure ||
           effect == WorkshopEffect::Cinemagraph;
}
bool animatedOutput(WorkshopEffect effect, const PatinaInput &input) {
    return effect == WorkshopEffect::Feedback || (effect != WorkshopEffect::DualImage && input.animated());
}
QString workshopName(WorkshopEffect effect) {
    switch (effect) {
    case WorkshopEffect::Patina:
        return "电子包浆";
    case WorkshopEffect::Photocopy:
        return "多代复印";
    case WorkshopEffect::Duotone:
        return "双色套印";
    case WorkshopEffect::TimeSlice:
        return "GIF 时间切片";
    case WorkshopEffect::CRT:
        return "CRT 显像管";
    case WorkshopEffect::Thermal:
        return "热敏小票";
    case WorkshopEffect::Exposure:
        return "GIF 长曝光残影";
    case WorkshopEffect::PixelSort:
        return "像素排序";
    case WorkshopEffect::DualImage:
        return "明暗双图";
    case WorkshopEffect::Cinemagraph:
        return "局部动图";
    case WorkshopEffect::Glyphs:
        return "字符拼画";
    case WorkshopEffect::Contours:
        return "等高线海报";
    case WorkshopEffect::Feedback:
        return "视频反馈隧道";
    }
    return {};
}
namespace {
struct TimedFrame {
    qint64 begin, end;
    QImage image;
};
using History = std::deque<TimedFrame>;
bool stopped(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }
PatinaResult failure(const QString &message) {
    PatinaResult result;
    result.frame.error = message;
    return result;
}
QImage motionFrame(const QImage &input) {
    if (!input.hasAlphaChannel())
        return input.convertToFormat(QImage::Format_RGB32);
    QImage out(input.size(), QImage::Format_RGB32);
    if (!out.isNull()) {
        out.fill(Qt::white);
        QPainter painter(&out);
        painter.drawImage(0, 0, input);
    }
    return out;
}
QImage slice(const History &history, const WorkshopSettings::TimeSlice &opt, const CancelFlag &cancel) {
    const auto &current = history.back();
    if (history.size() == 1 || opt.spanMs <= 0)
        return current.image;
    const int w = current.image.width(), h = current.image.height();
    const bool horizontal = opt.direction >= 2, reverse = (opt.direction % 2) != 0;
    int strips = horizontal ? w : h;
    std::vector<const QImage *> samples(strips);
    for (int i = 0; i < strips; ++i) {
        int offset = reverse ? strips - 1 - i : i;
        // Anchor at the end of the display interval so the current frame occupies its share of the scan.
        const qint64 time = current.end - 1 - qint64(opt.spanMs) * offset / std::max(1, strips - 1);
        const auto it = std::upper_bound(history.begin(), history.end(), time,
                                         [](qint64 t, const TimedFrame &f) { return t < f.begin; });
        samples[i] = &(it == history.begin() ? history.front() : *std::prev(it)).image;
    }
    QImage out(w, h, QImage::Format_RGB32);
    if (out.isNull())
        return {};
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return {};
        if (!horizontal)
            std::memcpy(out.scanLine(y), samples[y]->constScanLine(y), size_t(w) * sizeof(QRgb));
        else {
            auto *dest = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x)
                dest[x] = reinterpret_cast<const QRgb *>(samples[x]->constScanLine(y))[x];
        }
    }
    return out;
}

QImage exposure(const History &history, const WorkshopSettings::Exposure &opt, const CancelFlag &cancel) {
    const auto &current = history.back();
    if (history.size() == 1 || opt.spanMs <= 0)
        return current.image;
    const double falloff = .3 + opt.decay * .06, denominator = -std::expm1(-falloff);
    struct Tap {
        const QImage *frame;
        double weight;
    };
    std::vector<Tap> taps;
    double present = 1.;
    // Integrate the shutter response over each prior frame's actual display interval.
    for (auto it = history.begin(); it != std::prev(history.end()); ++it) {
        const double oldest = std::min(1., double(current.begin - it->begin) / opt.spanMs);
        const double newest = std::min(1., double(current.begin - it->end) / opt.spanMs);
        const double weight =
            .54 * (std::exp(-falloff * newest) - std::exp(-falloff * oldest)) / denominator;
        if (weight > 0) {
            taps.push_back({&it->image, weight});
            present -= weight;
        }
    }
    QImage out(current.image.size(), QImage::Format_RGB32);
    if (out.isNull())
        return {};
    const int w = out.width();
    struct RGB {
        double r, g, b;
    };
    std::vector<RGB> scan(w);
    for (int y = 0; y < out.height(); ++y) {
        if (stopped(cancel))
            return {};
        const auto *src = reinterpret_cast<const QRgb *>(current.image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            scan[x] = {qRed(src[x]) * present, qGreen(src[x]) * present, qBlue(src[x]) * present};
        for (const auto &tap : taps) {
            const auto *line = reinterpret_cast<const QRgb *>(tap.frame->constScanLine(y));
            for (int x = 0; x < w; ++x) {
                scan[x].r += qRed(line[x]) * tap.weight;
                scan[x].g += qGreen(line[x]) * tap.weight;
                scan[x].b += qBlue(line[x]) * tap.weight;
            }
        }
        auto *dest = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            dest[x] = qRgb(qRound(scan[x].r), qRound(scan[x].g), qRound(scan[x].b));
    }
    return out;
}
} // namespace

PatinaResult renderWorkshop(const PatinaInput &input, WorkshopEffect effect,
                            const WorkshopSettings &settings, bool allFrames, const CancelFlag &cancel,
                            const PatinaProgress &progress) {
    if (input.image.isNull())
        return failure("请先打开图像。");
    if (requiresMotion(effect) && !input.animated())
        return failure("这个效果需要多帧 GIF。点击「打开图像」或拖入动图即可预览。");
    try {
        if (effect == WorkshopEffect::Feedback)
            return renderFeedback(input, settings.feedback, allFrames, cancel, progress);
        if (effect == WorkshopEffect::DualImage && settings.dual.dark.isNull())
            return failure("请选择黑底显示的第二张图。");
        QImage freeze, mask;
        if (effect == WorkshopEffect::Cinemagraph) {
            if (settings.cinema.sourceKey != input.image.cacheKey() ||
                settings.cinema.mask.size() != input.image.size())
                return failure("点击「绘制运动区域」选择底帧，涂出要保留运动的部分。");
            freeze = motionFrame(readWorkshopFrame(input, settings.cinema.base, cancel));
            mask = featherMask(settings.cinema.mask, settings.cinema.feather, cancel);
            if (stopped(cancel))
                return {};
            if (freeze.isNull() || mask.isNull())
                return failure("无法读取底帧或运动蒙版。");
        }
        const bool full = animatedOutput(effect, input) && allFrames;
        const int count = full ? int(input.timing.delays.size()) : 1;
        QBuffer bytes;
        bytes.setData(input.gif);
        bytes.open(QIODevice::ReadOnly);
        QImageReader reader(&bytes, "gif");
        std::unique_ptr<GifEncoder> encoder;
        History history;
        qint64 time = 0;
        const int span =
            effect == WorkshopEffect::TimeSlice ? settings.slice.spanMs : settings.trail.spanMs;
        PatinaResult result;
        if (progress)
            progress(0, count);
        for (int i = 0; i < count; ++i) {
            if (stopped(cancel))
                return {};
            const QImage source = full ? reader.read() : input.image;
            if (source.isNull())
                return failure(QString("无法读取第 %1 帧。").arg(i + 1));
            QImage output;
            if (effect == WorkshopEffect::Cinemagraph)
                output = blendMotion(freeze, motionFrame(source), mask, cancel);
            else if (effect == WorkshopEffect::DualImage)
                output = dualImage(source, settings.dual.dark, cancel);
            else if (effect == WorkshopEffect::PixelSort)
                output = sortPixels(source, settings.sort, cancel);
            else if (effect == WorkshopEffect::Contours)
                output = contourPoster(source, settings.contours, cancel);
            else if (effect == WorkshopEffect::Glyphs)
                output =
                    glyphPicture(source, settings.glyphs, i == 0 ? &result.plainText : nullptr, cancel);
            else if (requiresMotion(effect)) {
                auto image = motionFrame(source);
                if (image.isNull())
                    return failure("没有足够内存处理此帧。");
                const qint64 duration = std::max(1, int(input.timing.delays.at(i))) * 10;
                const qint64 earliest =
                    (effect == WorkshopEffect::TimeSlice ? time + duration - 1 : time) - span;
                while (!history.empty() && history.front().end <= earliest)
                    history.pop_front();
                const qint64 budget = std::max<qint64>(256, QImageReader::allocationLimit()) * 1024 * 1024;
                if (quint64(image.sizeInBytes()) * (history.size() + 1) > quint64(budget))
                    return failure("当前时间跨度需要缓存过多帧，请缩短时间跨度或使用较小的 GIF。");
                history.push_back({time, time + duration, std::move(image)});
                output = effect == WorkshopEffect::TimeSlice ? slice(history, settings.slice, cancel)
                                                             : exposure(history, settings.trail, cancel);
                time += duration;
            } else
                output = renderSpatialEffect(source, effect, settings, cancel);
            if (stopped(cancel))
                return {};
            if (output.isNull())
                return failure("无法生成画面，请减小图像尺寸或调整参数。");
            if (i == 0) {
                result.frame.original = source;
                result.frame.processed = output;
                if (full)
                    encoder = std::make_unique<GifEncoder>(output.size(), input.timing.repeats);
                if (encoder && effect == WorkshopEffect::Cinemagraph)
                    encoder->useFrozenBackgroundPalette(freeze);
            }
            if (full && !encoder->appendFrame(output, input.timing.delays.at(i)))
                return failure("GIF 编码失败，请减小输出尺寸。");
            if (progress)
                progress(i + 1, count);
        }
        if (encoder)
            result.gif = encoder->finish();
        return result;
    } catch (const std::bad_alloc &) {
        return failure("可用内存不足，请减小尺寸或缩短动图的时间跨度。");
    }
}
