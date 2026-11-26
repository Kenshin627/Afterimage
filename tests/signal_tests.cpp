#include "community.h"
#include "renderer.h"
#include "style.h"
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class SignalTests : public QObject {
    Q_OBJECT
    QImage source;
    QJsonObject base;
  private slots:
    void initTestCase() {
        source = QImage(113, 71, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixel(
                    x, y, qRgb((x * 9 + y * 3) % 256, (x * 2 + y * 13) % 256, (x * 17 + y * 7) % 256));
        base = completeStyle({{"sizeOrigin", true}, {"sharpen", false}, {"yuv420", false}, {"quality", 1}});
    }
    void dimensionsAndValidation() {
        for (const auto &p : QList<QPair<QString, QSize>>{
                 {"4:3", {640, 480}}, {"16:9", {720, 405}}, {"21:9", {748, 320}}, {"1:1", {500, 500}}}) {
            auto s = completeStyle({{"wide", p.first}});
            auto f = renderFrame(source, s);
            QCOMPARE(f.processed.size(), p.second);
            QVERIFY(f.error.isEmpty());
        }
        auto clamped = completeStyle({{"light", 900}, {"quality", -60}, {"fit", "broken"}});
        QCOMPARE(clamped["light"].toDouble(), 4.);
        QCOMPARE(clamped["quality"].toDouble(), 0.);
        QCOMPARE(clamped["fit"].toString(), "cover");
        QVERIFY(!isPreset({{"name", "hello"}}));
        QVERIFY(isPreset({{"name", "合法"}, {"light", 1}}));
        QImage wide(2500, 47, QImage::Format_RGB32);
        wide.fill(Qt::red);
        auto s = base;
        s["zoom"] = 2.3;
        QCOMPARE(renderFrame(wide, s).processed.size(), wide.size());
        QImage tiny(1, 1, QImage::Format_RGB32);
        tiny.fill(Qt::white);
        s["zoom"] = 20;
        s["shiftY"] = 1000;
        s["interlaced"] = 8;
        QCOMPARE(renderFrame(tiny, s).processed.size(), QSize(1, 1));
    }
    void deterministicNoiseAndControls() {
        const auto initial = renderFrame(source, base);
        QVERIFY(!initial.processed.isNull());
        const QJsonObject variants{{"light", 1.37},       {"contrast", .56},
                                   {"darkFade", 88},      {"brightFade", 82},
                                   {"vividU", 2.6},       {"vividV", .14},
                                   {"shiftU", 90},        {"shiftV", -88},
                                   {"level", 90},         {"shiftX", 8},
                                   {"shiftY", 4},         {"replace", 2},
                                   {"interlaced", 4},     {"interlacedLight", .72},
                                   {"transposeX", 1.2},   {"zoom", 3.1},
                                   {"quality", .15},      {"lightNoise", 60},
                                   {"darkNoise", 60},     {"border", 7},
                                   {"ratio", 1.7},        {"snow", true},
                                   {"invertLight", true}, {"sharpen", true},
                                   {"yuv420", true}};
        for (auto it = variants.begin(); it != variants.end(); ++it) {
            auto s = base;
            s[it.key()] = it.value();
            const auto f = renderFrame(source, s);
            QVERIFY2(f.error.isEmpty() && !f.processed.isNull(), qPrintable(it.key()));
            QVERIFY2(f.jpeg != initial.jpeg, qPrintable(it.key()));
            QCOMPARE(f.jpeg, renderFrame(source, s).jpeg);
        }
        auto s = base;
        s["sharpen"] = true;
        QSet<QByteArray> results;
        for (auto mode : {"右倾", "左倾", "桑拿", "浮雕"}) {
            s["convoluteName"] = mode;
            results.insert(renderFrame(source, s).jpeg);
        }
        QCOMPARE(results.size(), 4);
        s = base;
        s["yuv420"] = true;
        auto normal = renderFrame(source, s).jpeg;
        s["yuv420Noise"] = 8;
        QVERIFY(normal != renderFrame(source, s).jpeg);
        s = base;
        s["interlaced"] = 3;
        normal = renderFrame(source, s).jpeg;
        s["interlacedLine"] = 8;
        QVERIFY(normal != renderFrame(source, s).jpeg);
        s = base;
        s["transposeX"] = 2;
        normal = renderFrame(source, s).jpeg;
        s["transposePow"] = 1;
        QVERIFY(normal != renderFrame(source, s).jpeg);
        normal = renderFrame(source, s).jpeg;
        s["transposeNoise"] = 1.7;
        QVERIFY(normal != renderFrame(source, s).jpeg);
        for (auto mode : {"cover", "contain", "fill"}) {
            s = base;
            s["sizeOrigin"] = false;
            s["fit"] = mode;
            results.insert(renderFrame(source, s).jpeg);
        }
        QCOMPARE(results.size(), 7);
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(renderFrame(source, base, {}, 0, cancel).processed.isNull());
    }
    void imageStampAndExport() {
        QImage mark(45, 20, QImage::Format_ARGB32);
        mark.fill(QColor(255, 80, 10, 200));
        auto s = base;
        s["watermark"] = true;
        auto fx = renderFrame(source, s, mark);
        auto plain = renderFrame(source, base);
        QVERIFY(fx.jpeg != plain.jpeg);
        s["watermarkAlpha"] = 0;
        QCOMPARE(renderFrame(source, s, mark).jpeg, plain.jpeg);
        s = base;
        s["styleName"] = true;
        QVERIFY(renderFrame(source, s).jpeg != plain.jpeg);
        s["logoPng"] = "test";
        QVERIFY(!shareableStyle(s).contains("logoPng"));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (auto ext : {"png", "jpg", "webp"}) {
            QString path = dir.filePath(QString("测试导出.") + ext), error;
            QVERIFY2(saveFrame(fx, path, &error), qPrintable(error));
            auto read = readImage(path);
            QCOMPARE(read.size(), source.size());
            QCOMPARE(read.convertToFormat(QImage::Format_RGB32),
                     fx.processed.convertToFormat(QImage::Format_RGB32));
        }
        QString error;
        QVERIFY(!saveFrame(fx, dir.filePath("missing/sub.png"), &error));
        QVERIFY(!error.isEmpty());
    }
    void originalBrowserReference() {
        const QDir folder(QString::fromUtf8(REFERENCE_DIR));
        const QImage input = readImage(folder.filePath("input.png"));
        QVERIFY(!input.isNull());
        QFile cases(folder.filePath("cases.json"));
        QVERIFY(cases.open(QIODevice::ReadOnly));
        const QJsonObject styles = QJsonDocument::fromJson(cases.readAll()).object();
        QCOMPARE(styles.size(), 4);
        for (auto it = styles.begin(); it != styles.end(); ++it) {
            auto native = renderFrame(input, completeStyle(it.value().toObject()))
                              .processed.convertToFormat(QImage::Format_RGB32);
            auto reference = readImage(folder.filePath("web-" + it.key() + ".jpg"))
                                 .convertToFormat(QImage::Format_RGB32);
            QCOMPARE(native.size(), reference.size());
            QVERIFY(!reference.isNull());
            quint64 deviation = 0;
            for (int y = 0; y < native.height(); ++y) {
                auto a = reinterpret_cast<const QRgb *>(native.constScanLine(y));
                auto b = reinterpret_cast<const QRgb *>(reference.constScanLine(y));
                for (int x = 0; x < native.width(); ++x)
                    deviation += qAbs(qRed(a[x]) - qRed(b[x])) + qAbs(qGreen(a[x]) - qGreen(b[x])) +
                                 qAbs(qBlue(a[x]) - qBlue(b[x]));
            }
            const double average = double(deviation) / (3.0 * native.width() * native.height());
            qInfo("%s mean channel difference %.3f / 255", qPrintable(it.key()), average);
            QVERIFY2(average < 8, qPrintable(it.key()));
        }
    }
    void inputFormatsAndOrientation() {
        const QDir fixtures(QString::fromUtf8(REFERENCE_DIR));
        QCOMPARE(readImage(fixtures.filePath("orientation-6.jpg")).size(), QSize(5, 9));
        for (const auto &file : {"still.gif", "still.webp"})
            QCOMPARE(readImage(fixtures.filePath(file)).size(), QSize(9, 5));
        QTemporaryDir dir;
        for (const auto &file : {"读写.png", "读写.bmp", "读写.tiff", "读写.jpg"}) {
            const QString path = dir.filePath(file);
            QVERIFY(source.save(path));
            QCOMPARE(readImage(path).size(), source.size());
        }
    }
    void liveCatalogReads() {
        if (!qEnvironmentVariableIsSet("AFTERIMAGE_CHECK_LIVE"))
            QSKIP("Online read checks are opt-in");
        Community api;
        bool done = false;
        QString error;
        QByteArray payload;
        auto receive = [&](QByteArray data, QString err) {
            payload = data;
            error = err;
            done = true;
        };
        api.presets(receive);
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(QJsonDocument::fromJson(payload).array().size() > 0);
        for (bool small : {true, false}) {
            done = false;
            api.sample("hibike1-004.jpg", small, receive);
            QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(!QImage::fromData(payload).isNull());
        }
    }
    void communitySubmission() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QUrl site(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
        Community api(nullptr, site);
        QStringList routes;
        QList<QByteArray> bodies;
        bool reject = false;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *sock = server.nextPendingConnection()) {
                auto pending = std::make_shared<QByteArray>();
                connect(sock, &QTcpSocket::readyRead, sock, [&, sock, pending] {
                    pending->append(sock->readAll());
                    int end = int(pending->indexOf("\r\n\r\n"));
                    if (end < 0)
                        return;
                    auto header = pending->left(end);
                    int length = 0;
                    for (auto l : header.split('\n'))
                        if (l.toLower().startsWith("content-length:"))
                            length = l.mid(15).trimmed().toInt();
                    if (pending->size() < end + 4 + length)
                        return;
                    QString route = QString::fromLatin1(header.split(' ').value(1));
                    routes << route;
                    bodies << pending->mid(end + 4, length);
                    QJsonObject reply{{"success", !reject}};
                    if (route == "/api/upyun/authorization")
                        reply = {{"api", site.toString()},
                                 {"bucket", "upload"},
                                 {"policy", "signed-policy"},
                                 {"authorization", "signed-token"},
                                 {"prefix", "https://example.com"}};
                    if (route == "/upload")
                        reply = {{"code", 200}, {"url", "/photo.jpg"}};
                    auto payload = QJsonDocument(reply).toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                                "close\r\nContent-Length: " +
                                QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        for (bool capture : {false, true}) {
            bool finished = false;
            QString result;
            routes.clear();
            bodies.clear();
            api.submit(capture, "jpeg-content", "image/jpeg", "photo.jpg", {{"name", "测试预设"}}, "tester",
                       [&](QString e) {
                           result = e;
                           finished = true;
                       });
            QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
            QVERIFY2(result.isEmpty(), qPrintable(result));
            QCOMPARE(routes.size(), 3);
            QCOMPARE(routes[0], "/api/upyun/authorization");
            QVERIFY(bodies[1].contains("jpeg-content"));
            QCOMPARE(routes.last(), capture ? "/api/vaporwave/capture" : "/api/vaporwave/style");
            const auto final = QJsonDocument::fromJson(bodies.last()).object();
            QCOMPARE(final["author"].toString(), "tester");
            QCOMPARE(final[capture ? "src" : "preview"].toString(), "https://example.com/photo.jpg");
        }
        reject = true;
        bool done = false;
        QString failure;
        api.submit(false, "image", "image/jpeg", "x.jpg", {}, {}, [&](QString e) {
            failure = e;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!failure.isEmpty());
    }
};
QTEST_MAIN(SignalTests)
#include "signal_tests.moc"
