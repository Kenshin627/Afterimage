#include "creativeeffects.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
bool stopped(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }
int light(QRgb v) { return (54 * qRed(v) + 183 * qGreen(v) + 19 * qBlue(v) + 128) >> 8; }
QImage onPaper(const QImage &input, QSize size, Qt::GlobalColor color) {
    QImage out(size, QImage::Format_RGB32);
    if (out.isNull())
        return {};
    out.fill(color);
    QRectF box(QPointF(0, 0), QSizeF(input.size()).scaled(QSizeF(size), Qt::KeepAspectRatio));
    box.moveCenter(QRectF(out.rect()).center());
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(box, input);
    return out;
}
} // namespace
QImage sortPixels(const QImage &source, const WorkshopSettings::PixelSort &s, const CancelFlag &cancel) {
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    if (out.isNull())
        return {};
    const bool vertical = s.direction >= 2, reverse = s.direction % 2;
    const int lines = vertical ? out.width() : out.height(), size = vertical ? out.height() : out.width();
    const int low = std::min(s.low, s.high), high = std::max(s.low, s.high),
              limit = std::clamp(s.length, 1, size);
    std::vector<QRgb> run;
    run.reserve(limit);
    auto *bits = out.bits();
    const qsizetype stride = out.bytesPerLine() / sizeof(QRgb);
    for (int i = 0; i < lines; ++i) {
        if (stopped(cancel))
            return {};
        const qsizetype step = vertical ? stride : 1;
        auto *line = reinterpret_cast<QRgb *>(bits) + (vertical ? i : stride * i);
        int pos = 0;
        while (pos < size) {
            int start = pos;
            run.clear();
            while (pos < size && int(run.size()) < limit) {
                QRgb pixel = line[step * pos];
                int value = light(pixel);
                if (!qAlpha(pixel) || value < low || value > high)
                    break;
                run.push_back(pixel);
                ++pos;
            }
            if (run.empty()) {
                ++pos;
                continue;
            }
            std::stable_sort(run.begin(), run.end(), [reverse](QRgb a, QRgb b) {
                return reverse ? light(a) > light(b) : light(a) < light(b);
            });
            for (size_t j = 0; j < run.size(); ++j)
                line[(start + qsizetype(j)) * step] = run[j];
        }
    }
    return out;
}
QImage dualImage(const QImage &front, const QImage &back, const CancelFlag &cancel) {
    if (front.isNull() || back.isNull())
        return {};
    const QImage a = onPaper(front, front.size(), Qt::white), b = onPaper(back, front.size(), Qt::black);
    QImage out(front.size(), QImage::Format_ARGB32);
    if (out.isNull() || a.isNull() || b.isNull())
        return {};
    for (int y = 0; y < out.height(); ++y) {
        if (stopped(cancel))
            return {};
        auto *p = reinterpret_cast<const QRgb *>(a.constScanLine(y)),
             *q = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        auto *dest = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            // Split the tonal range so both targets can be represented by one RGBA pixel.
            const int white = 128 + (light(p[x]) * 127 + 127) / 255,
                      black = (light(q[x]) * 127 + 127) / 255;
            const int alpha = 255 - white + black, gray = alpha ? (255 * black + alpha / 2) / alpha : 0;
            dest[x] = qRgba(gray, gray, gray, alpha);
        }
    }
    return out;
}
QImage glyphPicture(const QImage &source, const WorkshopSettings::Glyphs &s, QString *text,
                    const CancelFlag &cancel) {
    if (source.isNull())
        return {};
    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(std::clamp(s.fontSize, 6, 72));
    QFontMetricsF fm(font);
    const QString chars = s.characters.trimmed().isEmpty() ? QStringLiteral(" .:-=+*#%@") : s.characters;
    QStringList symbols = {" "};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, chars);
    int start = 0;
    for (int end = finder.toNextBoundary(); end >= 0 && symbols.size() < 96;
         end = finder.toNextBoundary()) {
        auto symbol = chars.mid(start, end - start);
        start = end;
        if (!symbol.isEmpty() && QChar::isPrint(symbol.toUcs4().front()) && !symbols.contains(symbol))
            symbols.append(symbol);
    }
    double widest = fm.horizontalAdvance("M");
    for (const auto &symbol : symbols)
        widest = std::max(widest, fm.horizontalAdvance(symbol));
    const int cw = std::max(1, int(std::ceil(widest))),
              ch = std::max(1, int(std::ceil(fm.height())) + s.leading);
    struct Glyph {
        QString symbol;
        QImage tile;
        double density;
    };
    std::vector<Glyph> atlas;
    const bool doubleWidth = widest > fm.horizontalAdvance("M") * 1.5;
    for (const auto &symbol : symbols) {
        QImage tile(cw, ch, QImage::Format_RGB32);
        if (tile.isNull() || stopped(cancel))
            return {};
        tile.fill(Qt::white);
        QPainter p(&tile);
        p.setFont(font);
        p.setPen(Qt::black);
        p.drawText(tile.rect(), Qt::AlignCenter, symbol);
        p.end();
        double density = 0;
        for (int y = 0; y < ch; ++y) {
            auto *pixels = reinterpret_cast<const QRgb *>(tile.constScanLine(y));
            for (int x = 0; x < cw; ++x)
                density += 255 - light(pixels[x]);
        }
        QString cellText=symbol;
        if(doubleWidth && fm.horizontalAdvance(symbol)<widest*.75)cellText.append(' ');
        atlas.push_back({cellText, tile, density});
    }
    std::stable_sort(atlas.begin(), atlas.end(),
                     [](const Glyph &a, const Glyph &b) { return a.density < b.density; });
    // Keep complete cells even for images smaller than one glyph.
    int cols = std::max(1, source.width() / cw), rows = std::max(1, source.height() / ch);
    QImage input = source.hasAlphaChannel() ? onPaper(source, source.size(), Qt::white) : source;
    const QImage samples = input.scaled(cols, rows, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGB32);
    QImage out(source.size(), QImage::Format_RGB32);
    if (out.isNull() || samples.isNull())
        return {};
    out.fill(Qt::white);
    QPainter painter(&out);
    if (text)
        text->clear();
    for (int y = 0; y < rows; ++y) {
        if (stopped(cancel))
            return {};
        const auto *pixels = reinterpret_cast<const QRgb *>(samples.constScanLine(y));
        for (int x = 0; x < cols; ++x) {
            const double target = (255 - light(pixels[x])) / 255. * atlas.back().density;
            auto it = std::lower_bound(atlas.begin(), atlas.end(), target,
                                       [](const Glyph &g, double v) { return g.density < v; });
            if (it == atlas.end())
                it = std::prev(atlas.end());
            if (it != atlas.begin() && target - std::prev(it)->density < it->density - target)
                --it;
            const int left = int(qint64(x) * source.width() / cols), top = int(qint64(y) * source.height() / rows);
            const QRect rect(left, top, int(qint64(x + 1) * source.width() / cols) - left,
                             int(qint64(y + 1) * source.height() / rows) - top);
            painter.drawImage(rect, it->tile);
            if (text)text->append(it->symbol);
        }
        if (text)
            text->append('\n');
    }
    return out;
}
QImage contourPoster(const QImage &source, const WorkshopSettings::Contours &s, const CancelFlag &cancel) {
    if (source.isNull())
        return {};
    const int w = source.width(), h = source.height();
    const int step = std::max(2, std::max(w, h) / 380);
    const int nx = std::max(2, w / step + 1), ny = std::max(2, h / step + 1);
    const auto base = source.hasAlphaChannel() ? onPaper(source, source.size(), Qt::white) : source;
    const QImage field = base.scaled(nx, ny, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    QImage out(source.size(), QImage::Format_RGB32);
    if (out.isNull() || field.isNull())
        return {};
    out.fill(s.paper);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(s.ink, s.width, Qt::SolidLine, Qt::RoundCap));
    const double dx = double(w - 1) / (nx - 1), dy = double(h - 1) / (ny - 1);
    const int levels = std::clamp(s.levels, 2, 40);
    const double interval = 255. / (levels + 1);
    for (int y = 0; y < ny - 1; ++y) {
        if (stopped(cancel))
            return {};
        QVector<QLineF> lines;
        const auto *a = field.constScanLine(y), *b = field.constScanLine(y + 1);
        for (int x = 0; x < nx - 1; ++x) {
            std::array<int, 4> v{a[x], a[x + 1], b[x + 1], b[x]};
            auto extremes = std::minmax_element(v.begin(), v.end());
            const int first = int(std::floor(*extremes.first / interval)) + 1,
                      last = std::min(levels, int(std::floor(*extremes.second / interval)));
            if (first > last)
                continue;
            std::array<QPointF, 4> corners{QPointF(x * dx, y * dy), QPointF((x + 1) * dx, y * dy),
                                           QPointF((x + 1) * dx, (y + 1) * dy),
                                           QPointF(x * dx, (y + 1) * dy)};
            for (int k = first; k <= last; ++k) {
                double level = k * interval;
                std::array<QPointF, 4> intersections;
                int n = 0;
                for (int j = 0; j < 4; ++j) {
                    int next = (j + 1) % 4;
                    if ((v[j] < level) == (v[next] < level))
                        continue;
                    intersections[n++] =
                        corners[j] + (corners[next] - corners[j]) * ((level - v[j]) / (v[next] - v[j]));
                }
                if (n == 4 && (((v[0] + v[1] + v[2] + v[3]) * .25 < level) == (v[0] < level)))
                    std::swap(intersections[1], intersections[3]);
                for (int j = 1; j < n; j += 2)
                    lines.append(QLineF(intersections[j - 1], intersections[j]));
            }
        }
        if (!lines.isEmpty())
            p.drawLines(lines);
    }
    return out;
}
