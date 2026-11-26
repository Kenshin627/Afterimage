#include "workshop.h"
#include "creativeeffects.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using Effect = WorkshopEffect;
PatinaInput still() {
    QImage image(QStringLiteral(REFERENCE_DIR) + "/input.png");
    return {image.scaled(144, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), "测试图", {}, {}};
}
PatinaInput animation(QList<quint16> delays = {3, 11, 6}) {
    GifEncoder gif(QSize(16, 16), 3);
    QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    for (int i = 0; i < colors.size(); ++i) {
        QImage frame(16, 16, QImage::Format_RGB32);
        frame.fill(colors[i]);
        gif.appendFrame(frame, delays[i]);
    }
    return gifPatinaInput(gif.finish(), "时序图", nullptr);
}
QList<QImage> frames(QByteArray gif) {
    QBuffer b(&gif);
    b.open(QIODevice::ReadOnly);
    QImageReader reader(&b, "gif");
    QList<QImage> out;
    while (reader.canRead()) {
        auto image = reader.read();
        if (image.isNull())
            break;
        out.append(image);
    }
    return out;
}
} // namespace
class WorkshopTests : public QObject {
    Q_OBJECT
  private slots:
    void pixelSortingRangeAndDirection() {
        QImage src(8, 1, QImage::Format_RGB32);
        int values[] = {80, 60, 223, 16, 110, 50, 70, 250};
        for (int x = 0; x < 8; ++x)
            src.setPixel(x, 0, qRgb(values[x], values[x], values[x]));
        WorkshopSettings s;
        s.sort.low = 50;
        s.sort.high = 230;
        s.sort.length = 8;
        auto image = sortPixels(src, s.sort, {});
        int expected[] = {60, 80, 223, 16, 50, 70, 110, 250};
        for (int x = 0; x < 8; ++x)
            QCOMPARE(qRed(image.pixel(x, 0)), expected[x]);
        s.sort.direction = 1;
        QCOMPARE(qRed(sortPixels(src, s.sort, {}).pixel(0, 0)), 223);
        s.sort.direction = 0;
        s.sort.length = 2;
        QCOMPARE(qRed(sortPixels(src, s.sort, {}).pixel(5, 0)), 110);
        QTransform rotation;
        rotation.rotate(90);
        s.sort.length = 8;
        s.sort.direction = 2;
        QCOMPARE(sortPixels(src.transformed(rotation), s.sort, {}), image.transformed(rotation));
    }
    void dualCompositesOnBothBackgrounds() {
        QImage a(16, 16, QImage::Format_RGB32), b(a.size(), a.format());
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                a.setPixel(x, y, qRgb(17 * x, 17 * x, 17 * x));
                b.setPixel(x, y, qRgb(17 * y, 17 * y, 17 * y));
            }
        auto png = dualImage(a, b, {});
        QVERIFY(png.hasAlphaChannel());
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                auto p = png.pixel(x, y);
                int black = (qAlpha(p) * qRed(p) + 127) / 255, white = black + 255 - qAlpha(p);
                QVERIFY(std::abs(black - 17 * y * 127 / 255.) <= 1);
                QVERIFY(std::abs(white - 128 - 17 * x * 127 / 255.) <= 1);
            }
        QCOMPARE(qAlpha(png.pixel(15, 0)), 0);
        auto doc = animation();
        WorkshopSettings opts;
        opts.dual.dark = b;
        auto result = renderWorkshop(doc, Effect::DualImage, opts, true);
        QVERIFY(result.gif.isEmpty() && result.frame.processed.hasAlphaChannel());
    }
    void glyphAndContourRendering() {
        WorkshopSettings s;
        auto doc = still();
        s.glyphs.characters = " 海浪@";
        s.glyphs.fontSize = 8;
        auto first = renderWorkshop(doc, Effect::Glyphs, s, false);
        QVERIFY(!first.frame.processed.isNull());
        QVERIFY(first.plainText.endsWith('\n'));
        QSet<QChar> letters;
        for (auto c : first.plainText) {
            QVERIFY(c == '\n' || s.glyphs.characters.contains(c));
            letters.insert(c);
        }
        QVERIFY(letters.size() >= 3);
        s.glyphs.leading = 12;
        auto spaced = renderWorkshop(doc, Effect::Glyphs, s, false);
        QVERIFY(first.plainText.count('\n') > spaced.plainText.count('\n'));
        auto poster = renderWorkshop(doc, Effect::Contours, s, false).frame.processed;
        QCOMPARE(poster.size(), doc.image.size());
        s.contours.levels = 4;
        QVERIFY(poster != renderWorkshop(doc, Effect::Contours, s, false).frame.processed);
        doc.image.fill(Qt::gray);
        auto flat = renderWorkshop(doc, Effect::Contours, s, false).frame.processed;
        for (int y = 0; y < flat.height(); y += 3)
            for (int x = 0; x < flat.width(); x += 3)
                QCOMPARE(flat.pixelColor(x, y), s.contours.paper);
    }
    void selectedFreezeFrameAndSoftMask() {
        auto doc = animation();
        WorkshopSettings s;
        auto &opt = s.cinema;
        opt.base = 1;
        opt.sourceKey = doc.image.cacheKey();
        opt.feather = 0;
        opt.mask = QImage(doc.image.size(), QImage::Format_Grayscale8);
        opt.mask.fill(0);
        for (int y = 0; y < 16; ++y)
            for (int x = 8; x < 16; ++x)
                opt.mask.scanLine(y)[x] = 255;
        auto result = renderWorkshop(doc, Effect::Cinemagraph, s, true);
        auto video = frames(result.gif);
        QCOMPARE(video.size(), 3);
        GifTiming timing;
        QVERIFY(readGifTiming(result.gif, timing));
        QCOMPARE(timing.delays, doc.timing.delays);
        QCOMPARE(timing.repeats, 3);
        for (auto &f : video)
            QCOMPARE(f.pixelColor(0, 8), QColor(Qt::green));
        QCOMPARE(video[0].pixelColor(12, 8), QColor(Qt::red));
        QCOMPARE(video[2].pixelColor(12, 8), QColor(Qt::blue));
        opt.feather = 2;
        auto softened = renderWorkshop(doc, Effect::Cinemagraph, s, false).frame.processed;
        auto edge = softened.pixelColor(8, 8);
        QVERIFY(edge.green() > 0 && edge.red() > 0);
        opt.sourceKey = 0;
        QVERIFY(!renderWorkshop(doc, Effect::Cinemagraph, s, true).frame.error.isEmpty());
    }
    void feedbackFromStillAndGif() {
        WorkshopSettings s;
        auto doc = still();
        auto result = renderWorkshop(doc, Effect::Feedback, s, true);
        GifTiming timing;
        QVERIFY(readGifTiming(result.gif, timing));
        QCOMPARE(timing.delays.size(), 24);
        for (auto t : timing.delays)
            QCOMPARE(t, 8);
        QCOMPARE(timing.repeats, 0);
        auto video = frames(result.gif);
        QCOMPARE(video.size(), 24);
        QVERIFY(video[0] != video[6]);
        QCOMPARE(result.frame.processed, renderWorkshop(doc, Effect::Feedback, s, false).frame.processed);
        s.feedback.x = 25;
        QVERIFY(result.frame.processed != renderWorkshop(doc, Effect::Feedback, s, false).frame.processed);
        doc = animation();
        result = renderWorkshop(doc, Effect::Feedback, s, true);
        QVERIFY(readGifTiming(result.gif, timing));
        QCOMPARE(timing.delays, doc.timing.delays);
        QCOMPARE(timing.repeats, doc.timing.repeats);
        auto flag = std::make_shared<std::atomic_bool>(false);
        auto stopped = renderWorkshop(still(), Effect::Feedback, s, true, flag, [flag](int n, int) {
            if (n == 2)
                flag->store(true);
        });
        QVERIFY(stopped.gif.isEmpty());
    }
    void printAndDisplayControls() {
        auto input = still();
        QVERIFY(!input.image.isNull());
        WorkshopSettings s;
        auto render = [&](Effect e) { return renderWorkshop(input, e, s, false).frame.processed; };
        const auto mono = render(Effect::Photocopy);
        QVERIFY(mono.isGrayscale());
        QCOMPARE(mono.size(), input.image.size());
        s.copy.passes = 1;
        QVERIFY(mono != render(Effect::Photocopy));
        s.copy.passes = 4;
        s.copy.toner = 10;
        QVERIFY(mono != render(Effect::Photocopy));
        s.copy.toner = 54;
        s.copy.skew = 100;
        QVERIFY(mono != render(Effect::Photocopy));
        const auto print = render(Effect::Duotone);
        QVERIFY(!print.isGrayscale());
        QCOMPARE(print, render(Effect::Duotone));
        s.print.offset = -10;
        QVERIFY(print != render(Effect::Duotone));
        s.print.offset = 3;
        s.print.pitch = 18;
        QVERIFY(print != render(Effect::Duotone));
        s.print.pitch = 6;
        s.print.first = Qt::green;
        QVERIFY(print != render(Effect::Duotone));
        const auto crt = render(Effect::CRT);
        QVERIFY(crt != input.image);
        s.crt.mask = 2;
        QVERIFY(crt != render(Effect::CRT));
        s.crt.mask = 0;
        s.crt.glow = 0;
        QVERIFY(crt != render(Effect::CRT));
        s.crt.glow = 45;
        s.crt.curvature = 0;
        QVERIFY(crt != render(Effect::CRT));
        QImage alpha(32, 32, QImage::Format_ARGB32);
        alpha.fill(Qt::transparent);
        auto paper = renderWorkshop({alpha, "", {}, {}}, Effect::Duotone, s, false).frame.processed;
        QVERIFY(qRed(paper.pixel(15, 15)) > 230);
        QTemporaryDir dir;
        Frame frame;
        frame.processed = print;
        QVERIFY(saveFrame(frame, dir.filePath("无损.png")));
        QCOMPARE(QImage(dir.filePath("无损.png")).convertToFormat(QImage::Format_RGB32), print);
        QVERIFY(saveFrame(frame, dir.filePath("印刷.jpg")));
        QCOMPARE(QImage(dir.filePath("印刷.jpg")).size(), print.size());
    }
    void receiptGeometryAndHead() {
        auto doc = still();
        doc.image.fill(Qt::black);
        WorkshopSettings s;
        s.thermal.width = 200;
        s.thermal.missing = 0;
        const auto clear = renderWorkshop(doc, Effect::Thermal, s, false).frame.processed;
        QCOMPARE(clear.width(), 200);
        QCOMPARE(clear.height(), 139);
        const auto paper = clear.pixel(0, 0);
        QVERIFY(clear.pixel(100, 40) != paper);
        s.thermal.missing = 4;
        const auto broken = renderWorkshop(doc, Effect::Thermal, s, false).frame.processed;
        int blank = 0;
        for (int x = 8; x < 192; ++x) {
            if (broken.pixel(x, 35) == paper) {
                ++blank;
                QCOMPARE(broken.pixel(x, 100), paper);
            }
        }
        QCOMPARE(blank, 4);
        auto gray = still();
        gray.image.fill(QColor(130, 130, 130));
        s.thermal.missing = 0;
        auto light = renderWorkshop(gray, Effect::Thermal, s, false).frame.processed;
        s.thermal.heat = 80;
        QVERIFY(light != renderWorkshop(gray, Effect::Thermal, s, false).frame.processed);
    }
    void timeSliceUsesFrameDurations() {
        auto doc = animation();
        WorkshopSettings s;
        s.slice.spanMs = 199;
        auto last = [&]() { return frames(renderWorkshop(doc, Effect::TimeSlice, s, true).gif).value(2); };
        for (int direction = 0; direction < 4; ++direction) {
            s.slice.direction = direction;
            auto image = last();
            QVERIFY(!image.isNull());
            const bool horizontal = direction >= 2, reverse = direction % 2;
            auto at = [&](int p) {
                if (reverse)
                    p = 15 - p;
                return image.pixelColor(horizontal ? p : 8, horizontal ? 8 : p);
            };
            QCOMPARE(at(0), QColor(Qt::blue));
            QCOMPARE(at(7), QColor(Qt::green));
            QCOMPARE(at(15), QColor(Qt::red));
        }
        s.slice.direction = 0;
        doc = animation({11, 3, 6});
        QCOMPARE(last().pixelColor(8, 7), QColor(Qt::red));
        s.slice.spanMs = 0;
        QCOMPARE(last().pixelColor(8, 15), QColor(Qt::blue));
    }
    void exposureWeightsAndTiming() {
        auto doc = animation();
        WorkshopSettings s;
        s.trail.spanMs = 140;
        s.trail.decay = 0;
        auto last = [&]() {
            return frames(renderWorkshop(doc, Effect::Exposure, s, true).gif).value(2).pixelColor(8, 8);
        };
        const QColor slow = last();
        QVERIFY(slow.red() > 0 && slow.green() > slow.red() && slow.blue() > 100);
        s.trail.decay = 100;
        const auto fast = last();
        QVERIFY(fast.red() < slow.red() && fast.green() > slow.green());
        s.trail.spanMs = 0;
        QCOMPARE(last(), QColor(Qt::blue));
        s.trail.spanMs = 140;
        s.trail.decay = 0;
        doc = animation({11, 3, 6});
        QVERIFY(last().red() > slow.red());
    }
    void gifExportsAndCancellation() {
        auto doc = animation();
        const WorkshopSettings s;
        for (Effect e : {Effect::Photocopy, Effect::Duotone, Effect::CRT, Effect::Thermal,
                         Effect::TimeSlice, Effect::Exposure}) {
            int completed = 0;
            auto result = renderWorkshop(doc, e, s, true, {}, [&](int done, int total) {
                completed = done;
                QCOMPARE(total, 3);
            });
            QVERIFY2(!result.gif.isEmpty(), qPrintable(result.frame.error));
            QCOMPARE(completed, 3);
            GifTiming timing;
            QVERIFY(readGifTiming(result.gif, timing));
            QCOMPARE(timing.delays, doc.timing.delays);
            QCOMPARE(timing.repeats, 3);
            auto all = frames(result.gif);
            QCOMPARE(all.size(), 3);
            QVERIFY(all.first() != all.last() || all.first() != all[1]);
            auto flag = std::make_shared<std::atomic_bool>(false);
            auto cancelled = renderWorkshop(doc, e, s, true, flag, [flag](int done, int) {
                if (done == 1)
                    flag->store(true);
            });
            QVERIFY(cancelled.gif.isEmpty());
            QVERIFY(cancelled.frame.processed.isNull());
        }
        QVERIFY(!renderWorkshop(still(), Effect::Exposure, s, true).frame.error.isEmpty());
        QVERIFY(!renderWorkshop(still(), Effect::TimeSlice, s, true).frame.error.isEmpty());
    }
};
QTEST_MAIN(WorkshopTests)
#include "workshop_tests.moc"
