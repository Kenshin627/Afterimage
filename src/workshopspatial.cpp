#include "workshopspatial.h"
#include <QPainter>
#include <QImageReader>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
bool stopped(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }
int byte(double value) { return std::clamp(int(value + .5), 0, 255); }
const QRgb *row(const QImage &image, int y) {
    return reinterpret_cast<const QRgb *>(image.constScanLine(y));
}
QRgb *row(QImage &image, int y) { return reinterpret_cast<QRgb *>(image.scanLine(y)); }
int luminance(QRgb rgb) { return (54 * qRed(rgb) + 183 * qGreen(rgb) + 19 * qBlue(rgb)) >> 8; }
quint32 grain(int x, int y, int layer) {
    quint32 n = quint32(x) * 0x1f123bb5u ^ quint32(y) * 0x5f356495u ^ quint32(layer) * 0x9e3779b9u;
    n ^= n >> 16;
    n *= 0x85ebca6bu;
    return n ^ (n >> 13);
}
QImage opaque(const QImage &image, Qt::GlobalColor background) {
    if (!image.hasAlphaChannel())
        return image.convertToFormat(QImage::Format_RGB32);
    QImage result(image.size(), QImage::Format_RGB32);
    if (!result.isNull()) {
        result.fill(background);
        QPainter p(&result);
        p.drawImage(0, 0, image);
    }
    return result;
}

QImage photocopy(const QImage &source, const WorkshopSettings::Photocopy &opt, const CancelFlag &cancel) {
    QImage current = source.convertToFormat(QImage::Format_Grayscale8);
    const int w = source.width(), h = source.height();
    double ink = (opt.toner - 50) * .68;
    for (int pass = 0; pass < opt.passes; ++pass) {
        if (stopped(cancel) || current.isNull())
            return {};
        QImage next(current.size(), QImage::Format_Grayscale8);
        if (next.isNull())
            return {};
        // Each pass feeds its own result through the copier, including accumulated skew and dust.
        const double slope = (.0017 + .002 * std::sin(pass * 2.16)) * opt.skew / 60.;
        for (int y = 0; y < h; ++y) {
            if (stopped(cancel))
                return {};
            const auto *line = current.constScanLine(y);
            auto *dest = next.scanLine(y);
            double offset = (y - (h - 1) * .5) * slope + opt.skew * .012 * std::cos(pass * 1.7);
            const int shift = int(std::floor(offset));
            const double fraction = offset - shift;
            int band = int((grain(0, y / 3, pass) & 7)) - 3;
            auto sample = [&](int x) { return (x >= 0 && x < w) ? int(line[x]) : 250; };
            for (int x = 0; x < w; ++x) {
                int sx = x + shift;
                double v = sample(sx) * (1 - fraction) + sample(sx + 1) * fraction;
                const quint32 noise = grain(x, y, pass + 17);
                const double speckle = (int(noise & 255) - 127) * .14;
                v = (v - 137 - ink) * 1.26 + 136 + band + speckle;
                if ((noise & 1023) < 4)
                    v = v < 100 ? v + 65 : v - 61;
                dest[x] = uchar(std::clamp(int(v), 8, 249));
            }
        }
        current = std::move(next);
    }
    return current.convertToFormat(QImage::Format_RGB32);
}

double coverage(double tone, double threshold) { return std::clamp((tone - threshold) * 9. + .5, 0., 1.); }
QImage duotone(const QImage &source, const WorkshopSettings::Duotone &opt, const CancelFlag &cancel) {
    const int w = source.width(), h = source.height(), pitch = std::max(2, opt.pitch);
    QImage result(source.size(), QImage::Format_RGB32);
    if (result.isNull())
        return {};
    // Rotated periodic screens. Ink overlap multiplies the two reflected colors.
    const double frequency = 6.28318530718 / pitch;
    auto screen = [&](int x, int y, double cs, double sn) {
        return .5 +
               .25 * (std::cos(frequency * (x * cs + y * sn)) + std::cos(frequency * (y * cs - x * sn)));
    };
    const double a[] = {opt.first.redF(), opt.first.greenF(), opt.first.blueF()};
    const double b[] = {opt.second.redF(), opt.second.greenF(), opt.second.blueF()};
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return {};
        const auto *base = row(source, y);
        const int sy = y + opt.offset / 3;
        const auto *shifted = sy >= 0 && sy < h ? row(source, sy) : nullptr;
        auto *dest = row(result, y);
        for (int x = 0; x < w; ++x) {
            const QRgb first = base[x];
            int sx = x - opt.offset;
            QRgb second = shifted && sx >= 0 && sx < w ? shifted[sx] : qRgb(255, 255, 255);
            double warmA = (qRed(first) - qBlue(first)) / 255.,
                   warmB = (qRed(second) - qBlue(second)) / 255.;
            double tintA = std::clamp((1. - luminance(first) / 255.) * 1.20 + warmA * .86 - .05, 0., 1.);
            double tintB = std::clamp((1. - luminance(second) / 255.) * 1.30 - warmB * .80 - .10, 0., 1.);
            auto dot = [&](double tint, double thresh) {
                return tint <= 0 ? 0. : tint >= 1 ? 1. : coverage(tint, thresh);
            };
            double fa = dot(tintA, screen(x, y, .966, .259));
            double fb = dot(tintB, screen(sx, sy, .707107, .707107));
            const double paper = int(grain(x, y, 3) & 7) - 3.;
            auto channel = [&](int k, double white) {
                return byte((white + paper) * (1. - fa * (1. - a[k])) * (1. - fb * (1. - b[k])));
            };
            dest[x] = qRgb(channel(0, 249), channel(1, 243), channel(2, 227));
        }
    }
    return result;
}

QImage crt(const QImage &source, const WorkshopSettings::CRT &opt, const CancelFlag &cancel) {
    const int w = source.width(), h = source.height();
    QImage glow;
    if (opt.glow > 0) {
        // A low-resolution bloom buffer spreads only highlights across neighboring phosphors.
        glow = source.scaled(std::max(1, w / 9), std::max(1, h / 9), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
        if (glow.isNull())
            return {};
        for (int y = 0; y < glow.height(); ++y) {
            if (stopped(cancel))
                return {};
            auto *line = row(glow, y);
            for (int x = 0; x < glow.width(); ++x) {
                QRgb p = line[x];
                const double k = std::clamp((std::max({qRed(p), qGreen(p), qBlue(p)}) - 65) / 160., 0., 1.);
                line[x] = qRgb(byte(qRed(p) * k), byte(qGreen(p) * k), byte(qBlue(p) * k));
            }
        }
        glow = glow.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (glow.isNull())
            return {};
    }
    QImage out(source.size(), QImage::Format_RGB32);
    if (out.isNull())
        return {};
    const double curvature = opt.curvature * .0012;
    for (int y = 0; y < h; ++y) {
        if (stopped(cancel))
            return {};
        auto *dest = row(out, y);
        const double ny = (2. * y + 1) / h - 1.;
        for (int x = 0; x < w; ++x) {
            const double nx = (2. * x + 1) / w - 1.;
            const double fx = x + nx * ny * ny * curvature * w * .5;
            const double fy = y + ny * nx * nx * curvature * h * .5;
            if (fx < 0 || fy < 0 || fx > w - 1 || fy > h - 1) {
                dest[x] = qRgb(7, 9, 12);
                continue;
            }
            const int ix = int(fx), iy = int(fy);
            const int jx = std::min(ix + 1, w - 1), jy = std::min(iy + 1, h - 1);
            const double dx = fx - ix, dy = fy - iy;
            const QRgb p = row(source, iy)[ix], q = row(source, iy)[jx], r = row(source, jy)[ix],
                       s = row(source, jy)[jx];
            const QRgb light = glow.isNull() ? 0 : row(static_cast<const QImage &>(glow), iy)[ix];
            const int stripe = (x + (opt.mask == 1 ? (y / 2) % 2 : 0)) % 3;
            double scan = (opt.mask == 1 ? (y % 3 == 2 ? .79 : 1.)
                                         : (opt.mask == 2 ? (y % 4 == 3 ? .68 : 1.) : (y % 2 ? .93 : 1.)));
            const double vignette = 1. - .075 * (nx * nx + ny * ny);
            auto channel = [&](int k, int shift) {
                auto component = [shift](QRgb v) { return int((v >> shift) & 255); };
                double val = (1 - dy) * ((1 - dx) * component(p) + dx * component(q)) +
                             dy * ((1 - dx) * component(r) + dx * component(s));
                const double mask = k == stripe ? 1.13 : (opt.mask == 0 ? .84 : .78);
                return byte((val * mask * scan + component(light) * opt.glow * .006) * vignette);
            };
            dest[x] = qRgb(channel(0, 16), channel(1, 8), channel(2, 0));
        }
    }
    return out;
}

QImage thermal(const QImage &source, const WorkshopSettings::Thermal &opt, const CancelFlag &cancel) {
    const int width = std::clamp(opt.width, 160, 832), margin = std::max(6, width / 23),
              contentWidth = width - 2 * margin;
    qint64 contentHeight =
        std::max<qint64>(1, (qint64(source.height()) * contentWidth + source.width() / 2) / source.width());
    qint64 height = contentHeight + 2 * margin;
    const qint64 limit = qint64(QImageReader::allocationLimit()) * 1024 * 1024;
    if (height > 65535 || (limit > 0 && height * width * 4 > limit))
        return {};
    QImage image =
        source.scaled(contentWidth, int(contentHeight), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage out(width, int(height), QImage::Format_RGB32);
    if (out.isNull() || image.isNull())
        return {};
    const QRgb paper = qRgb(249, 246, 232), ink = qRgb(46, 41, 51);
    out.fill(paper);
    std::vector<bool> missing(contentWidth, false);
    for (int i = 0; i < opt.missing; ++i)
        missing[(contentWidth * (2 * i + 1) / (2 * opt.missing) + contentWidth / 9) % contentWidth] = true;
    std::vector<double> current(contentWidth + 2), next(contentWidth + 2);
    for (int y = 0; y < contentHeight; ++y) {
        if (stopped(cancel))
            return {};
        auto *dest = row(out, y + margin) + margin;
        const auto *src = row(static_cast<const QImage &>(image), y);
        int dir = (y % 2) ? -1 : 1;
        for (int step = 0; step < contentWidth; ++step) {
            int x = dir == 1 ? step : contentWidth - 1 - step;
            double tone =
                std::clamp((luminance(src[x]) - 128) * 1.10 + 128 - (opt.heat - 50) * 1.72, 0., 255.);
            double v = tone + current[x + 1];
            bool black = v < 128;
            if (black && !missing[x])
                dest[x] = ink;
            double err = v - (black ? 0 : 255);
            current[x + 1 + dir] += err * 7 / 16;
            next[x + 1 - dir] += err * 3 / 16;
            next[x + 1] += err * 5 / 16;
            next[x + 1 + dir] += err / 16;
        }
        current.swap(next);
        std::fill(next.begin(), next.end(), 0.);
    }
    return out;
}
} // namespace

QImage renderSpatialEffect(const QImage &source, WorkshopEffect effect, const WorkshopSettings &settings,
                           const CancelFlag &cancel) {
    if (source.isNull() || stopped(cancel))
        return {};
    auto image = opaque(source, effect == WorkshopEffect::CRT ? Qt::black : Qt::white);
    if (image.isNull())
        return {};
    switch (effect) {
    case WorkshopEffect::Photocopy:
        return photocopy(image, settings.copy, cancel);
    case WorkshopEffect::Duotone:
        return duotone(image, settings.print, cancel);
    case WorkshopEffect::CRT:
        return crt(image, settings.crt, cancel);
    case WorkshopEffect::Thermal:
        return thermal(image, settings.thermal, cancel);
    default:
        return {};
    }
}
