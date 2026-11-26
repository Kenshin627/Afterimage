#include "patina.h"
#include <QBuffer>
#include <QImageReader>
#include <QRandomGenerator>
#include <QtTest>
#include <cmath>

namespace {
QString reference(const char *name) { return QStringLiteral(REFERENCE_DIR) + "/" + name; }
double averageError(const QImage &a, const QImage &b) {
    if (a.size() != b.size() || a.isNull())
        return 1000;
    double total = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            total += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) +
                     std::abs(qBlue(p) - qBlue(q));
        }
    return total / (3. * a.width() * a.height());
}
PatinaSettings controlled() {
    PatinaSettings s;
    s.thumbnail = false;
    s.watermark = false;
    s.random = false;
    return s;
}
} // namespace
class PatinaTests : public QObject {
    Q_OBJECT
  private slots:
    void compareOriginalTwelveYears() {
        auto doc = openPatinaInput(reference("input.png"), nullptr);
        const QImage web(reference("web-patina-12.jpg"));
        const auto out = renderPatina(doc, controlled(), false);
        QVERIFY2(!out.frame.processed.isNull(), qPrintable(out.frame.error));
        QCOMPARE(out.frame.processed.size(), web.size());
        const auto error = averageError(out.frame.processed, web);
        qInfo() << "12 round reference mean absolute channel error:" << error;
        QVERIFY(error < 5.0);
        QCOMPARE(out.frame.processed, QImage::fromData(out.frame.jpeg, "jpeg"));
    }
    void greenAndRecompression() {
        QImage base(80, 64, QImage::Format_RGB32);
        base.fill(QColor(140, 140, 140));
        PatinaInput doc{base, "gray", {}, {}};
        auto s = controlled();
        // At low JPEG quality a flat patch can quantize back to the same color each generation.
        s.quality = 100;
        s.years = 1;
        auto first = renderPatina(doc, s, false).frame.processed.pixelColor(40, 36);
        s.years = 12;
        auto last = renderPatina(doc, s, false).frame.processed.pixelColor(40, 36);
        QVERIFY(last.green() - last.red() > first.green() - first.red() + 15);
        s.green = false;
        const auto neutral = renderPatina(doc, s, false).frame.processed.pixelColor(40, 36);
        QVERIFY(std::abs(neutral.red() - neutral.green()) < 3);
        doc.image = QImage(reference("input.png"));
        s.quality = 60;
        s.years = 1;
        auto one = renderPatina(doc, s, false).frame;
        s.years = 2;
        auto two = renderPatina(doc, s, false).frame;
        auto repeated = one.processed;
        QByteArray bytes;
        QVERIFY(jpegPass(repeated, s.quality, bytes));
        QCOMPARE(two.jpeg, bytes);
        QVERIFY(two.jpeg != one.jpeg);
    }
    void popOrderSizeAndStamps() {
        QImage base(128, 64, QImage::Format_RGB32);
        base.fill(QColor(104, 105, 106));
        PatinaInput doc{base, "gray", {}, {}};
        auto s = controlled();
        s.quality = 100;
        s.pop = true;
        s.grid = 3;
        auto grid = renderPatina(doc, s, false).frame.processed;
        QCOMPARE(grid.size(), QSize(384, 192));
        s.pop = false;
        for (int i = 0; i < 9; ++i) {
            s.years = i + 1;
            auto tile = renderPatina(doc, s, false).frame.processed;
            auto cell = grid.copy((i % 3) * 128, (i / 3) * 64, 128, 64);
            QVERIFY(averageError(cell, tile) < 1.6);
        }
        s.names = {"TEST_0000"};
        s.watermark = true;
        s.random = true;
        s.seed = 712;
        auto marked = renderPatina(doc, s, false).frame;
        QVERIFY(!marked.processed.isNull());
        QCOMPARE(marked.jpeg, renderPatina(doc, s, false).frame.jpeg);
        s.seed++;
        QVERIFY(marked.jpeg != renderPatina(doc, s, false).frame.jpeg);
        s.watermark = false;
        QVERIFY(marked.jpeg != renderPatina(doc, s, false).frame.jpeg);
        s.years = 1;
        s.thumbnail = true;
        s.maxEdge = 200;
        s.zoom = 50;
        doc.image = base.scaled(640, 320);
        QCOMPARE(renderPatina(doc, s, false).frame.processed.size(), QSize(100, 50));
        s.thumbnail = false;
        QCOMPARE(renderPatina(doc, s, false).frame.processed.size(), QSize(320, 160));
    }
    void advancedAndCancel() {
        auto doc = openPatinaInput(reference("input.png"), nullptr);
        doc.image = doc.image.scaled(128, 96);
        auto s = controlled();
        s.years = 2;
        auto baseline = renderPatina(doc, s, false).frame.jpeg;
        s.blur = 2;
        s.noise = 8;
        s.grain = 10;
        s.greenBoost = 2;
        s.greenLuma = .7;
        s.contrast = 1.2;
        s.light = .1;
        s.shiftX = 1;
        s.shiftY = -1;
        QSet<QByteArray> outputs;
        for (auto &kernel : QStringList{"右倾", "左倾", "桑拿", "浮雕"}) {
            s.kernel = kernel;
            const auto frame = renderPatina(doc, s, false).frame;
            QVERIFY(!frame.jpeg.isEmpty());
            QVERIFY(frame.jpeg != baseline);
            outputs.insert(frame.jpeg);
        }
        QCOMPARE(outputs.size(), 4);
        auto flag = std::make_shared<std::atomic_bool>(false);
        s.years = 1000;
        const auto stop = renderPatina(doc, s, false, flag, [flag](int step, int) {
            if (step == 2)
                flag->store(true);
        });
        QVERIFY(stop.frame.processed.isNull());
    }
    void animationTimingsAndPixels() {
        QString error;
        auto doc = openPatinaInput(reference("patina-motion.gif"), &error);
        QVERIFY2(doc.animated(), qPrintable(error));
        QCOMPARE(doc.timing.delays, QList<quint16>({4, 11, 7}));
        QCOMPARE(doc.timing.repeats, 3);
        auto s = controlled();
        s.years = 2;
        s.pop = true;
        const auto preview = renderPatina(doc, s, false);
        QVERIFY(preview.gif.isEmpty());
        int finalStep = 0;
        const auto full = renderPatina(doc, s, true, {}, [&](int done, int total) {
            finalStep = done;
            QCOMPARE(total, 6);
        });
        QCOMPARE(finalStep, 6);
        QVERIFY2(!full.gif.isEmpty(), qPrintable(full.frame.error));
        QCOMPARE(preview.frame.jpeg, full.frame.jpeg);
        auto output = gifPatinaInput(full.gif, "output", &error);
        QVERIFY2(output.animated(), qPrintable(error));
        QCOMPARE(output.timing.repeats, 3);
        QCOMPARE(output.timing.delays, doc.timing.delays);
        QBuffer source(&doc.gif);
        source.open(QIODevice::ReadOnly);
        QImageReader referenceReader(&source, "gif");
        QBuffer encoded;
        encoded.setData(full.gif);
        encoded.open(QIODevice::ReadOnly);
        QImageReader reader(&encoded, "gif");
        for (int i = 0; i < 3; ++i) {
            PatinaInput single = doc;
            single.image = referenceReader.read();
            const auto expected = renderPatina(single, s, false).frame.processed;
            auto image = reader.read();
            QVERIFY(!image.isNull());
            QVERIFY(averageError(image, expected) < 10.);
        }
        QVERIFY(!reader.canRead());
    }
    void encoderBoundaries() {
        QRandomGenerator rng(124);
        QImage noise(253, 159, QImage::Format_Indexed8);
        QList<QRgb> table;
        for (int i = 0; i < 256; ++i)
            table.append(qRgb(i, 255 - i, i));
        noise.setColorTable(table);
        for (int y = 0; y < noise.height(); ++y)
            for (int x = 0; x < noise.width(); ++x)
                noise.scanLine(y)[x] = uchar(rng.bounded(256));
        for (const int repeats : {-1, 0, 1, 65535}) {
            GifEncoder encoder(noise.size(), repeats);
            QVERIFY(encoder.appendFrame(noise, 0));
            QVERIFY(encoder.appendFrame(noise, 65535));
            QByteArray gif = encoder.finish();
            GifTiming timing;
            QVERIFY(readGifTiming(gif, timing));
            QCOMPARE(timing.repeats, repeats);
            QCOMPARE(timing.delays, QList<quint16>({0, 65535}));
            QBuffer b(&gif);
            b.open(QIODevice::ReadOnly);
            QImageReader reader(&b, "gif");
            for (int i = 0; i < 2; ++i)
                QCOMPARE(averageError(reader.read(), noise), 0.);
            gif.chop(1);
            QVERIFY(!readGifTiming(gif, timing));
        }
        QImage tiny(1, 1, QImage::Format_RGB32);
        tiny.fill(Qt::red);
        GifEncoder encoder(tiny.size(), -1);
        QVERIFY(encoder.appendFrame(tiny, 7));
        QCOMPARE(QImage::fromData(encoder.finish()).pixelColor(0, 0), QColor(Qt::red));
    }
};
QTEST_MAIN(PatinaTests)
#include "patina_tests.moc"
