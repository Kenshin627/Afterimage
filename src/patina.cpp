#include "patina.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace {
bool stopped(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }
uchar channel(double value) { return uchar(std::clamp(int(std::nearbyint(value)), 0, 255)); }
QSize workingSize(QSize original, const PatinaSettings &s) {
    double scale = s.zoom / 100.;
    if (s.thumbnail)
        scale *= std::min(1., double(s.maxEdge) / std::max(original.width(), original.height()));
    return {std::max(1, int(original.width() * scale)), std::max(1, int(original.height() * scale))};
}
QSize gridSize(QSize tile, const PatinaSettings &s) {
    double width = std::min(4000., double(tile.width()) * s.grid);
    if (s.thumbnail && tile.width() < s.maxEdge)
        width = std::min(width, s.maxEdge * 2.);
    return {int(width), std::max(1, int(width * tile.height() / tile.width()))};
}
bool fitsImage(QSize size) {
    const qint64 mb = QImageReader::allocationLimit();
    return !size.isEmpty() && size.width() <= 65500 && size.height() <= 65500 &&
           (mb <= 0 || qint64(size.width()) * size.height() * 4 <= mb * 1024 * 1024);
}
QImage fitted(const QImage &image, QSize target, QRectF placement, double blur = 1.) {
    QImage out(target, QImage::Format_RGBA8888);
    if (out.isNull())
        return {};
    out.fill(Qt::white);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (blur > 1.) {
        const QSize small(std::max(1, int(target.width() / blur)),
                          std::max(1, int(target.height() / blur)));
        const QImage reduced = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (reduced.isNull())
            return {};
        p.drawImage(placement, reduced);
    } else
        p.drawImage(placement, image);
    return out;
}

void addStamp(QImage &image, const PatinaSettings &s, int generation, QRandomGenerator &rng) {
    if (s.names.isEmpty())
        return;
    auto pick = [&](int n) { return s.random ? rng.bounded(n) : generation % n; };
    const auto jitter = [&](int n) { return s.random ? rng.bounded(2 * n + 1) - n : 0; };
    QString name = s.names.at(pick(int(s.names.size()))).trimmed();
    if (name.isEmpty())
        return;
    if (name.contains("0000"))
        name.replace(
            "0000",
            QString::number(s.random ? rng.bounded(10000) : generation % 10000).rightJustified(4, '0'));
    const QString punctuation = "-_+~!^&、.。”“\"'|";
    name.replace('_', punctuation.at(pick(int(punctuation.size()))));
    name.prepend('@');
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font("Microsoft YaHei UI");
    font.setPixelSize(
        std::max(1, qRound(image.width() * s.stampSize / (22 + (s.random ? rng.bounded(8) : 0)))));
    p.setFont(font);
    const QFontMetricsF fm(font);
    const double inset = font.pixelSize() * .5;
    const int zone = pick(s.positions + 1);
    double x = zone == 0 ? image.width() - 1.2 * inset - fm.horizontalAdvance(name)
                         : (image.width() - fm.horizontalAdvance(name)) * .5;
    double y = zone == 2 ? image.height() * .5 + inset : image.height() - inset * (zone == 1 ? 1.2 : 1.);
    QPointF base(x + jitter(zone == 0 ? 5 : 10), y + jitter(zone == 2 ? 10 : 5) - fm.descent());
    // A compact Gaussian shadow keeps white usernames legible on light images.
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const double weight = std::exp(-(dx * dx + dy * dy) / 3.);
            p.setPen(QColor(0, 0, 0, qRound(70 * s.shadow * weight)));
            p.drawText(base + QPointF(dx, dy + 1), name);
        }
    p.setPen(Qt::white);
    p.drawText(base, name);
}

bool greenPass(QImage &img, const CancelFlag &cancel) {
    for (int y = 0; y < img.height(); ++y) {
        if (stopped(cancel))
            return false;
        uchar *p = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x, p += 4) {
            const int r = p[0], g = p[1], b = p[2];
            const int luma = (77 * r + 150 * g + 29 * b) >> 8;
            const int u = std::clamp(((-43 * r - 85 * g + 128 * b) >> 8) - 1, -128, 127);
            const int v = std::clamp(((128 * r - 107 * g - 21 * b) >> 8) - 1, -128, 127);
            p[0] = uchar(std::clamp(luma + ((91881 * v) >> 16), 0, 255));
            p[1] = uchar(std::clamp(luma + ((-22553 * u - 46802 * v) >> 16), 0, 255));
            p[2] = uchar(std::clamp(luma + ((116130 * u) >> 16), 0, 255));
        }
    }
    return true;
}

bool tonePass(QImage &img, const PatinaSettings &s, QRandomGenerator &rng, const CancelFlag &cancel) {
    if (!s.noise && !s.grain && !s.light && s.contrast == 1 && !s.greenBoost && s.kernel.isEmpty())
        return true;
    for (int y = 0; y < img.height(); ++y) {
        if (stopped(cancel))
            return false;
        uchar *p = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x, p += 4) {
            const int r = p[0], g = p[1], b = p[2];
            double lum = std::floor(.299 * r + .587 * g + .114 * b);
            if (s.noise)
                lum = channel(lum + (rng.generateDouble() - .5) * s.noise);
            if (s.grain)
                lum = channel(lum + (rng.generateDouble() - .5) * s.grain * (255 - lum) / 255.);
            lum = channel((lum - 128) * s.contrast + 128);
            p[0] = channel(lum + s.light * 128);
            p[1] = channel(std::floor(-.168736 * r - .331264 * g + .5 * b + 128));
            p[2] = channel(std::floor(.5 * r - .418688 * g - .081312 * b + 128));
        }
    }
    if (!s.kernel.isEmpty() && !convolve(img, s.kernel, cancel))
        return false;
    const double bias = 4 * s.greenBoost;
    for (int y = 0; y < img.height(); ++y) {
        if (stopped(cancel))
            return false;
        uchar *p = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x, p += 4) {
            const double luma = channel(p[0] - bias * s.greenLuma);
            const int u = channel(p[1] - bias) - 128, v = channel(p[2] - bias) - 128;
            p[0] = channel(std::floor(luma + 1.4075 * v));
            p[1] = channel(std::floor(luma - .3455 * u - .7169 * v));
            p[2] = channel(std::floor(luma + 1.779 * u));
        }
    }
    return true;
}

Frame ageFrame(const QImage &source, const PatinaSettings &s, bool animated, const CancelFlag &cancel,
               const std::function<void(int)> &progress) {
    Frame out;
    const QSize tile = workingSize(source.size(), s);
    const bool pop = s.pop && !animated;
    const QSize output = pop ? gridSize(tile, s) : tile;
    if (!fitsImage(tile) || !fitsImage(output)) {
        out.error = "输出尺寸超出图像编码器或内存限制，请减小缩放或启用缩略图。";
        return out;
    }
    const int count = pop ? s.grid * s.grid : s.years;
    const QRectF baseRect(s.shiftX, s.shiftY, tile.width(), tile.height());
    QImage current = source, grid;
    if (pop) {
        grid = QImage(output, QImage::Format_RGB32);
        if (!grid.isNull())
            grid.fill(Qt::white);
    }
    if (pop && grid.isNull()) {
        out.error = "波普画布内存不足。";
        return out;
    }
    QRandomGenerator geometry(s.random ? s.seed : 1u), texture(s.random ? (s.seed ^ 0x6d2b79f5u) : 23u);
    auto quality = [&](int amount) {
        return std::clamp(s.quality + (s.random ? geometry.bounded(amount + 1) : 0), 0, 100);
    };
    for (int i = 0; i < count; ++i) {
        if (stopped(cancel))
            return {};
        if (i == 0 || animated)
            current = fitted(current, tile, baseRect, s.blur);
        else
            current = current.convertToFormat(QImage::Format_RGBA8888);
        if (current.isNull()) {
            out.error = "无法创建图像缓冲区。";
            return out;
        }
        if (s.watermark)
            addStamp(current, s, i, geometry);
        if (s.green && !greenPass(current, cancel))
            return {};
        if ((i == 0 || animated) && !tonePass(current, s, texture, cancel))
            return {};
        if (!jpegPass(current, quality(i == 0 && !animated ? 5 : 10), out.jpeg)) {
            out.error = "JPEG 重编码失败。";
            return out;
        }
        if (pop) {
            QPainter p(&grid);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const int col = i % s.grid, row = i / s.grid;
            const int x = col * output.width() / s.grid, y = row * output.height() / s.grid;
            const int right = (col + 1) * output.width() / s.grid,
                      bottom = (row + 1) * output.height() / s.grid;
            p.drawImage(QRect(x, y, right - x, bottom - y), current);
        }
        if (animated || (i > 0 && i + 1 < count)) {
            int dx = s.random ? geometry.bounded(5) - 2 : 0, dy = s.random ? geometry.bounded(5) - 2 : 0;
            if (dx || dy)
                current = fitted(current, tile,
                                 QRectF(-dx * .5, -dy * .5, std::max(1, tile.width() + dx),
                                        std::max(1, tile.height() + dy)));
            if (current.isNull() || (animated && !jpegPass(current, quality(10), out.jpeg))) {
                out.error = "无法重建转发画面。";
                return out;
            }
        }
        if (progress)
            progress(i + 1);
    }
    if (pop && s.grid > 1) {
        if (!jpegPass(grid, quality(5), out.jpeg)) {
            out.error = "波普拼图编码失败。";
            return out;
        }
        current = grid;
    }
    out.original = source;
    out.processed = current;
    return out;
}
} // namespace

PatinaInput gifPatinaInput(const QByteArray &bytes, const QString &name, QString *error) {
    PatinaInput result;
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "gif");
    result.image = reader.read();
    if (result.image.isNull() || !readGifTiming(bytes, result.timing)) {
        if (error)
            *error = "无法完整读取 GIF 帧或播放时间信息。";
        return {};
    }
    result.name = name;
    result.gif = bytes;
    return result;
}

PatinaInput openPatinaInput(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    const QString name = QFileInfo(path).fileName();
    if (f.peek(6) == "GIF89a" || f.peek(6) == "GIF87a")
        return gifPatinaInput(f.readAll(), name, error);
    QImageReader reader(&f);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    PatinaInput input;
    input.image = reader.read();
    input.name = name;
    if (input.image.isNull() && error)
        *error = reader.errorString();
    return input;
}

PatinaResult renderPatina(const PatinaInput &input, const PatinaSettings &s, bool allFrames,
                          const CancelFlag &cancel, const PatinaProgress &progress) {
    PatinaResult result;
    try {
        if (input.image.isNull()) {
            result.frame.error = "请先打开一张图像。";
            return result;
        }
        const bool animated = input.animated(), full = animated && allFrames;
        const int rounds = s.pop && !animated ? s.grid * s.grid : s.years;
        const int count = full ? int(input.timing.delays.size()) : 1;
        if (s.years < 1 || s.years > 1000 || s.grid < 1 || s.grid > 50 || s.zoom < 1 || s.zoom > 100 ||
            s.maxEdge < 1 || s.blur < 1 || s.positions < 0 || s.positions > 2 ||
            qint64(count) * rounds > std::numeric_limits<int>::max()) {
            result.frame.error = "处理参数超出范围。";
            return result;
        }
        QBuffer buffer;
        buffer.setData(input.gif);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "gif");
        std::unique_ptr<GifEncoder> encoder;
        if (full)
            encoder =
                std::make_unique<GifEncoder>(workingSize(input.image.size(), s), input.timing.repeats);
        if (progress)
            progress(0, count * rounds);
        for (int i = 0; i < count; ++i) {
            if (stopped(cancel))
                return {};
            QImage image = full ? reader.read() : input.image;
            if (image.isNull()) {
                result.frame.error = QString("GIF 第 %1 帧解码失败。").arg(i + 1);
                return result;
            }
            auto frame = ageFrame(image, s, animated, cancel, [&](int step) {
                if (progress)
                    progress(i * rounds + step, count * rounds);
            });
            if (stopped(cancel))
                return {};
            if (!frame.error.isEmpty() || frame.processed.isNull()) {
                result.frame = frame;
                return result;
            }
            if (encoder && !encoder->appendFrame(frame.processed, input.timing.delays[i])) {
                result.frame.error = "GIF 帧编码失败。";
                return result;
            }
            if (i == 0)
                result.frame = std::move(frame);
        }
        if (encoder)
            result.gif = encoder->finish();
    } catch (const std::bad_alloc &) {
        result = {};
        result.frame.error = "可用内存不足，请减小输出尺寸。";
    }
    return result;
}
