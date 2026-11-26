#include "renderer.h"
#include "style.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
bool stopped(const CancelFlag &cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}
uchar byte(double v) {
    return static_cast<uchar>(std::clamp(int(std::nearbyint(v)), 0, 255));
}

QImage compose(const QImage &source, const QSize size, const QJsonObject &s) {
    QImage canvas(size, QImage::Format_RGBA8888);
    if (canvas.isNull())
        return {};
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double ratio = s["ratio"].toDouble();
    const QString mode = s["fit"].toString();
    QRectF src(source.rect());
    QRectF dst{QPointF(), QSizeF(size)};
    const double aspect = double(size.width()) / size.height();
    const double sourceAspect = src.width() / src.height();
    if (mode == "cover") {
        const double cropAspect = aspect * ratio;
        if (sourceAspect > cropAspect)
            src.setWidth(src.height() * cropAspect);
        else
            src.setHeight(src.width() / cropAspect);
        src.moveCenter(QRectF(source.rect()).center());
    } else if (mode == "contain") {
        const double displayAspect = sourceAspect / ratio;
        if (displayAspect > aspect)
            dst.setHeight(dst.width() / displayAspect);
        else
            dst.setWidth(dst.height() * displayAspect);
        dst.moveCenter(QRectF(QPointF(), QSizeF(size)).center());
    }
    const double margin = s["border"].toDouble() / 200.0;
    dst.adjust(dst.width() * margin, dst.height() * margin, -dst.width() * margin, -dst.height() * margin);
    painter.drawImage(dst, source, src);
    return canvas;
}

void stamp(QImage &image, const QJsonObject &s, const QImage &logo) {
    QPainter p(&image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const int w = image.width(), h = image.height();
    if (s["watermark"].toBool() && !logo.isNull()) {
        const double width = w * s["watermarkSize"].toDouble();
        QRectF dest(0, 0, width, width * logo.height() / logo.width());
        dest.moveCenter({w * s["watermarkLeft"].toDouble(), h * s["watermarkTop"].toDouble()});
        p.setOpacity(s["watermarkAlpha"].toDouble());
        p.drawImage(dest, logo);
        p.setOpacity(1);
    }
    if (s["styleName"].toBool()) {
        QFont font = p.font();
        font.setPixelSize(std::max(1, qRound(w * s["styleSize"].toDouble() / 15.0)));
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        const QFontMetricsF fm(font);
        const double baseline = h * s["styleTop"].toDouble() + (fm.ascent() - fm.descent()) * .5;
        p.drawText(QPointF(w * s["styleLeft"].toDouble(), baseline), s["name"].toString());
    }
}

} // namespace

bool convolve(QImage &img, const QString &mode, const CancelFlag &cancel) {
    std::array<int, 9> weights{0, -1, 0, -1, 2, 2, 0, -1, 0};
    int divisor = 1;
    if (mode == "左倾")
        weights = {0, -1, 0, 3, 2, -2, 0, -1, 0};
    else if (mode == "桑拿") {
        weights.fill(1);
        divisor = 9;
    } else if (mode == "浮雕")
        weights = {1, 1, 1, 1, 1, -1, -1, -1, -1};
    const QImage input = img;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return false;
        uchar *dst = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int rgb[3]{};
            for (int j = 0; j < 3; ++j) {
                const uchar *row = input.constScanLine(std::clamp(y + j - 1, 0, h - 1));
                for (int i = 0; i < 3; ++i) {
                    const int weight = weights[j * 3 + i];
                    const uchar *px = row + 4 * std::clamp(x + i - 1, 0, w - 1);
                    for (int c = 0; c < 3; ++c)
                        rgb[c] += weight * px[c];
                }
            }
            for (int c = 0; c < 3; ++c)
                dst[x * 4 + c] = byte(double(rgb[c]) / divisor);
            dst[x * 4 + 3] = 255;
        }
    }
    return true;
}

namespace {
struct Noise {
    quint32 state = 0x71ac9e53u;
    double unit() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return double(state) / 4294967296.;
    }
    int below(double limit) {
        return int(std::floor(unit() * limit));
    }
};

bool signal(QImage &image, const QJsonObject &s, double scale, const CancelFlag &cancel) {
    const int w = image.width(), h = image.height();
    const qsizetype pixels = qsizetype(w) * h, bytes = pixels * 4;
    const int sx = qRound(s["shiftX"].toDouble() * scale), sy = qRound(s["shiftY"].toDouble() * scale);
    const qsizetype du = qsizetype(w) * sy + sx, dv = sx + sy;
    Noise noise;
    uchar *p = image.bits();
    if (!p)
        return false;
    const bool snow = s["snow"].toBool();
    if (!snow) {
        const int replace = s["replace"].toInt();
        if (replace > 0 && bytes > replace)
            std::memmove(p, p + replace, size_t(bytes - replace));
        if (s["sharpen"].toBool() && !convolve(image, s["convoluteName"].toString(), cancel))
            return false;
        p = image.bits();
    } else {
        std::memset(p, 0, size_t(bytes));
    }
    const double light = s["light"].toDouble(), contrast = s["contrast"].toDouble();
    const double low = s["darkFade"].toDouble(), high = 255 - s["brightFade"].toDouble();
    const double uGain = s["vividU"].toDouble(), vGain = s["vividV"].toDouble();
    const double uBias = s["shiftU"].toDouble(), vBias = s["shiftV"].toDouble(),
                 levels = s["level"].toDouble();
    auto chroma = [levels](double value, double gain, double bias) {
        const double changed = std::min(255., (std::floor(value) - 128) * gain + 128 + bias);
        return byte(std::floor(changed / levels + .5) * levels);
    };
    for (qsizetype i = 0; i < pixels; ++i) {
        if ((i & 65535) == 0 && stopped(cancel))
            return false;
        uchar *px = p + 4 * i;
        if (snow) {
            px[0] = uchar(noise.below(25) * 10);
            if (i >= du)
                p[(i - du) * 4 + 1] = uchar(108 + noise.below(40));
            if (i >= dv)
                p[(i - dv) * 4 + 2] = uchar(118 + noise.below(20));
            if ((i / w) % 2 == 0 && i >= du + w * 2)
                p[(i - du - w * 2) * 4] = byte(p[(i - du) * 4] + 100);
        } else {
            const double r = px[0], g = px[1], b = px[2];
            const double y = std::floor(.299 * r + .587 * g + .114 * b);
            px[0] = byte(std::min(high, std::max(low, ((y - 128) * contrast + 128) * light)));
            if (i >= du)
                p[(i - du) * 4 + 1] = chroma(-.168736 * r - .331264 * g + .5 * b + 128, uGain, uBias);
            if (i >= dv)
                p[(i - dv) * 4 + 2] = chroma(.5 * r - .418688 * g - .081312 * b + 128, vGain, vBias);
        }
        px[3] = 255;
    }
    if (s["yuv420"].toBool() && w >= 2 && h >= 2) {
        const qsizetype line = qsizetype(std::floor(s["yuv420Noise"].toDouble() * 4 * w));
        for (int y = 0; y < h - 1; y += 2) {
            if (stopped(cancel))
                return false;
            for (int x = 0; x < w - 1; x += 2) {
                const qsizetype i = (qsizetype(y) * w + x) * 4;
                for (int channel = 1; channel <= 2; ++channel) {
                    qsizetype a = i + channel, b = a + 4, c = (a + line) % bytes, d = (c + 4) % bytes;
                    p[a] = p[b] = p[c] = p[d] = byte((p[a] + p[b] + p[c] + p[d]) * .25);
                }
            }
        }
    }
    const double scan = s["interlaced"].toDouble() * scale;
    const double difference = std::pow(s["interlacedLight"].toDouble(), 2);
    const int period = std::max(2, qRound(s["interlacedLine"].toDouble() * scale));
    const double drift = s["transposeX"].toDouble(), power = s["transposePow"].toDouble();
    const double jitter = s["transposeNoise"].toDouble();
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0 && !scan && !difference)
            continue;
        if (pass == 1 && !drift)
            continue;
        for (int y = 0; y < h; ++y) {
            if (stopped(cancel))
                return false;
            const double phase = std::abs(double(y % period) / period - .5) * 2;
            const qsizetype offset = pass == 0
                                         ? qsizetype(std::ceil(scan * phase))
                                         : qsizetype(std::floor(w * drift * std::pow(double(y) / h, power) *
                                                                (1 + jitter * (noise.unit() - .5))));
            const double gain = pass == 0 ? 1 + difference * (phase - .5) * 2 : 1;
            for (int x = 0; x < w; ++x) {
                const qsizetype i = qsizetype(y) * w + x;
                if (i >= offset)
                    p[(i - offset) * 4] = byte(p[i * 4] * gain);
            }
        }
    }
    const bool invert = s["invertLight"].toBool();
    const double grain = s["darkNoise"].toDouble(), hiss = s["lightNoise"].toDouble();
    for (qsizetype i = 0; i < pixels; ++i) {
        if ((i & 65535) == 0 && stopped(cancel))
            return false;
        uchar *px = p + 4 * i;
        double y = invert ? 255 - px[0] : px[0];
        if (hiss)
            y = byte(y + noise.below(hiss) - hiss * .5);
        if (grain)
            y = byte(y + (noise.below(grain) - grain * .5) * (255 - y) / 255);
        const double u = px[1] - 128, v = px[2] - 128;
        px[0] = byte(std::floor(y + 1.4075 * v));
        px[1] = byte(std::floor(y - .3455 * u - .7169 * v));
        px[2] = byte(std::floor(y + 1.779 * u));
        px[3] = 255;
    }
    return true;
}

} // namespace

bool jpegPass(QImage &image, int quality, QByteArray &data) {
    data.clear();
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return false;
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(std::clamp(quality, 0, 100));
    if (!writer.write(image))
        return false;
    image = QImage::fromData(data, "JPEG");
    return !image.isNull();
}
QImage readImage(const QString &path, QString *error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    QImage image = reader.read();
    if (image.isNull() && error)
        *error = reader.errorString();
    return image;
}

QImage colorBars() {
    QImage img(1280, 720, QImage::Format_RGB32);
    QPainter p(&img);
    const QList<QColor> colors = {QColor("#e2e2e2"), QColor("#dfd739"), QColor("#54d3d0"),
                                  QColor("#57cb66"), QColor("#cc5ab9"), QColor("#db4d48"),
                                  QColor("#4d53b5")};
    for (int i = 0; i < 7; ++i) {
        p.fillRect(i * 1280 / 7, 0, (i + 1) * 1280 / 7 - i * 1280 / 7, 540, colors[i]);
        p.fillRect(i * 1280 / 7, 540, (i + 1) * 1280 / 7 - i * 1280 / 7, 64,
                   i % 2 ? QColor("#232428") : colors[6 - i]);
    }
    for (int i = 0; i < 16; ++i)
        p.fillRect(i * 80, 604, 80, 116, QColor(i * 17, i * 17, i * 17));
    return img;
}

QImage watermarkImage(const QJsonObject &s) {
    if (s.contains("logoPng")) {
        auto img = QImage::fromData(QByteArray::fromBase64(s["logoPng"].toString().toLatin1()), "PNG");
        if (!img.isNull())
            return img;
    }
    static const QImage original(":/watermark.png");
    return original;
}

Frame renderFrame(const QImage &source, const QJsonObject &input, const QImage &logo, int thumbnailEdge,
                  const CancelFlag &cancel) {
    Frame out;
    if (source.isNull()) {
        out.error = "请先打开一张图片。";
        return out;
    }
    try {
        const QJsonObject s = completeStyle(input);
        const QSize full = canvasSize(source.size(), s);
        QSize target = full;
        if (thumbnailEdge > 0)
            target.scale(thumbnailEdge, thumbnailEdge, Qt::KeepAspectRatio);
        const double scale = double(target.width()) / full.width(), zoom = s["zoom"].toDouble();
        const QSize work(std::max(1, int(target.width() / zoom)), std::max(1, int(target.height() / zoom)));
        out.original = compose(source, target, s);
        QImage result = (work == target) ? out.original : compose(source, work, s);
        if (out.original.isNull() || result.isNull()) {
            out.error = "图像缓冲区分配失败。";
            return out;
        }
        if (stopped(cancel))
            return {};
        if (!s["none"].toBool()) {
            if (s["watermark"].toBool() || s["styleName"].toBool())
                stamp(result, s, logo);
            if (!signal(result, s, scale, cancel)) {
                if (!stopped(cancel))
                    out.error = "无法处理此图像。";
                return out;
            }
        }
        const int quality = qRound(s["quality"].toDouble() * 100);
        if (stopped(cancel))
            return {};
        if (!jpegPass(result, quality, out.jpeg)) {
            out.error = "JPEG 编码器不可用。";
            return out;
        }
        if (work != target) {
            result = result.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (!jpegPass(result, quality, out.jpeg)) {
                out.error = "无法编码输出画幅。";
                return out;
            }
        }
        out.processed = result;
    } catch (const std::bad_alloc &) {
        out.error = "图像过大，内存不足。";
    }
    return out;
}

bool saveFrame(const Frame &frame, const QString &path, QString *error) {
    if (frame.processed.isNull()) {
        if (error)
            *error = "没有可导出的画面。";
        return false;
    }
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    if ((suffix == "jpg" || suffix == "jpeg") && !frame.jpeg.isEmpty())
        return writeAtomically(path, frame.jpeg, error);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, suffix);
    if (suffix == "jpg" || suffix == "jpeg")
        writer.setQuality(94);
    if (suffix == "webp")
        writer.setQuality(100);
    if (!writer.write(frame.processed)) {
        if (error)
            *error = writer.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
