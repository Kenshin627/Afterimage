#include "editor.h"
#include "patinadialog.h"
#include "style.h"
#include "widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QImageWriter>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QtTest>

class EditorTests : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    static QPushButton *findButton(QWidget &w, QString text) {
        for (auto *b : w.findChildren<QPushButton *>())
            if (b->text() == text || b->accessibleName() == text)
                return b;
        return nullptr;
    }
    static void chooseFile(QString path) {
        QTimer::singleShot(100, qApp, [path] {
            auto *d = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (d) {
                d->setDirectory(QFileInfo(path).absolutePath());
                auto *name = d->findChild<QLineEdit *>("fileNameEdit");
                if (name)
                    name->setText(QFileInfo(path).fileName());
                else
                    d->selectFile(path);
                QTimer::singleShot(220, d, [d] { QMetaObject::invokeMethod(d, "accept"); });
                QTimer::singleShot(4000, d, &QDialog::reject);
            }
        });
    }
  private slots:
    void initTestCase() {
        QVERIFY(storage.isValid());
        qApp->setAttribute(Qt::AA_DontUseNativeDialogs);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
        qApp->setOrganizationName("AfterimageQA");
        qApp->setApplicationName("EditorTests");
        applyTheme();
    }
    void initialAndHistory() {
        Editor w(storage.path());
        w.resize(1380, 836);
        w.show();
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.ready(), 10000);
        auto *list = w.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 22);
        auto *box = w.findChild<QDoubleSpinBox *>("number_light");
        QVERIFY(box);
        QCOMPARE(box->value(), 1.04);
        const auto original = w.currentFrame().jpeg;
        QString snapshot = qEnvironmentVariable("AFTERIMAGE_TEST_CAPTURE");
        if (!snapshot.isEmpty()) {
            QTest::qWait(120);
            QVERIFY(w.grab().save(snapshot));
        }
        box->setValue(1.65);
        QTRY_VERIFY(w.ready());
        QVERIFY(original != w.currentFrame().jpeg);
        findButton(w, "撤销")->click();
        QTRY_VERIFY(w.ready());
        QCOMPARE(w.currentFrame().jpeg, original);
        findButton(w, "重做")->click();
        QTRY_VERIFY(w.ready());
        QCOMPARE(box->value(), 1.65);
        auto *track = w.findChild<QSlider *>("slider_light");
        QVERIFY(track);
        QTest::mousePress(track, Qt::LeftButton, Qt::NoModifier, {19, 7});
        QTest::mouseMove(track, {110, 7});
        QTest::mouseRelease(track, Qt::LeftButton, Qt::NoModifier, {110, 7});
        QTRY_VERIFY(w.ready());
        findButton(w, "撤销")->click();
        QTRY_VERIFY(w.ready());
        QCOMPARE(box->value(), 1.65);
        list->setCurrentRow(3);
        QTRY_VERIFY(w.ready());
        QCOMPARE(w.currentFrame().processed.size(), QSize(640, 480));
        findButton(w, "分屏")->click();
        QVERIFY(findButton(w, "分屏")->isChecked());
        auto *canvas = w.findChild<Canvas *>();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        if (!snapshot.isEmpty()) {
            QVERIFY(w.grab().save(QFileInfo(snapshot).absolutePath() + "/comparison.png"));
        }
        findButton(w, "公共")->click();
        QVERIFY(list->count() >= 90);
        auto *search = w.findChild<QLineEdit *>();
        QVERIFY(search);
        search->setText("夏日阴凉");
        QCOMPARE(list->count(), 1);
        auto cells = readCollection(":/factory.json");
        QCOMPARE(cells.size(), 19);
    }
    void patinaWorkspace() {
        Editor w(storage.path());
        w.show();
        QTRY_VERIFY(w.ready());
        auto *tool = w.findChild<QPushButton *>("patinaTool");
        QVERIFY(tool);
        tool->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        auto *save = d->findChild<QPushButton *>("primary");
        auto *source = d->findChild<QComboBox *>("patinaSource");
        auto *years = d->findChild<QSpinBox *>("patinaYears");
        QVERIFY(save && source && years);
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        QCOMPARE(years->value(), 12);
        auto snapshot = qEnvironmentVariable("AFTERIMAGE_PATINA_CAPTURE");
        if (!snapshot.isEmpty())
            QVERIFY(d->grab().save(snapshot));
        source->setCurrentIndex(source->findData(1));
        QCOMPARE(source->currentText(), QString("蒸汽波效果"));
        years->setValue(1);
        QTRY_VERIFY(save->isEnabled());
        auto *cascade = findButton(*d, "继续包浆");
        QVERIFY(cascade);
        cascade->click();
        QCOMPARE(source->currentData().toInt(), 2);
        QTRY_VERIFY(save->isEnabled());
        const auto path = storage.filePath("patina-output.png");
        chooseFile(path);
        save->click();
        QVERIFY(!QImage(path).isNull());
        auto *apply = findButton(*d, "送回主画布");
        QVERIFY(apply);
        apply->click();
        QTRY_VERIFY(w.ready());
        QVERIFY(w.findChild<PatinaDialog *>() == nullptr || !w.findChild<PatinaDialog *>()->isVisible());
    }
    void patinaAnimatedExport() {
        Editor w(storage.path());
        w.show();
        QVERIFY(w.openPath(QStringLiteral(REFERENCE_DIR) + "/patina-motion.gif"));
        QTRY_VERIFY(w.ready());
        w.findChild<QPushButton *>("patinaTool")->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        auto *save = d->findChild<QPushButton *>("primary");
        auto *years = d->findChild<QSpinBox *>("patinaYears");
        auto *apply = findButton(*d, "送回主画布");
        QVERIFY(save && years && apply);
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        QVERIFY(save->text().contains("GIF"));
        years->setValue(2);
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!apply->isEnabled());
        const auto path = storage.filePath("patina-output.gif");
        chooseFile(path);
        save->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path), 10000);
        auto output = openPatinaInput(path, nullptr);
        QCOMPARE(output.timing.delays, QList<quint16>({4, 11, 7}));
        QCOMPARE(output.timing.repeats, 3);
        auto artifact = qEnvironmentVariable("AFTERIMAGE_PATINA_GIF");
        if (!artifact.isEmpty())
            QVERIFY(writeAtomically(artifact, output.gif, nullptr));
        QTRY_VERIFY(apply->isEnabled());
        apply->click();
        QTRY_VERIFY(w.ready());
        w.findChild<QPushButton *>("patinaTool")->click();
        auto *reopened = w.findChild<PatinaDialog *>();
        QVERIFY(reopened);
        QTRY_VERIFY(findButton(*reopened, "送回主画布")->isEnabled());
        QVERIFY(reopened->findChild<QPushButton *>("primary")->text().contains("GIF"));
        reopened->reject();
    }
    void workshopEffectsAndStacking() {
        Editor w(storage.path());
        w.show();
        QTRY_VERIFY(w.ready());
        auto *tool = findButton(w, "效果工坊");
        QVERIFY(tool);
        tool->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        auto *effects = d->findChild<QComboBox *>("workshopEffect");
        auto *exportButton = d->findChild<QPushButton *>("primary");
        auto *status = d->findChild<QLabel *>("patinaStatus");
        QVERIFY(effects && exportButton && status);
        QCOMPARE(effects->count(), 13);
        QCOMPARE(effects->currentText(), QString("电子包浆"));
        effects->setCurrentText("GIF 时间切片");
        QTRY_VERIFY(status->text().contains("需要多帧 GIF"));
        QVERIFY(!exportButton->isEnabled());
        effects->setCurrentText("双色套印");
        QTRY_VERIFY_WITH_TIMEOUT(exportButton->isEnabled(), 12000);
        auto *ink = findButton(*d, "第一色墨");
        QVERIFY(ink);
        QTimer::singleShot(100, qApp, [] {
            if (auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget())) {
                picker->setCurrentColor(QColor("#f66739"));
                picker->accept();
            }
        });
        ink->click();
        QCOMPARE(ink->text(), QString("#F66739"));
        QPushButton *reset = nullptr;
        for (auto *b : d->findChildren<QPushButton *>())
            if (b->text() == "恢复本效果默认值" && b->isVisible())
                reset = b;
        QVERIFY(reset);
        reset->click();
        QCOMPARE(ink->text(), QString("#F04F83"));
        QTRY_VERIFY(exportButton->isEnabled());
        for (auto *menu : d->findChildren<QComboBox *>())
            if (menu->accessibleName() == "预览方式")
                menu->setCurrentIndex(2);
        QString snapshot = qEnvironmentVariable("AFTERIMAGE_WORKSHOP_CAPTURE");
        if (!snapshot.isEmpty())
            QVERIFY(d->grab().save(snapshot));
        for (auto *number : d->findChildren<QSpinBox *>())
            if (number->accessibleName() == "网点间距")
                number->setValue(12);
        QTRY_VERIFY(exportButton->isEnabled());
        auto *again = findButton(*d, "作为下次输入");
        QVERIFY(again);
        again->click();
        auto *source = d->findChild<QComboBox *>("patinaSource");
        QVERIFY(source->currentText().contains("双色套印"));
        effects->setCurrentText("热敏小票");
        QTRY_VERIFY(exportButton->isEnabled());
        QString path = storage.filePath("workshop-receipt.png");
        chooseFile(path);
        exportButton->click();
        QImage saved(path);
        QVERIFY(!saved.isNull());
        QCOMPARE(saved.width(), 384);
        effects->setCurrentText("CRT 显像管");
        effects->setCurrentText("多代复印");
        QTRY_VERIFY(exportButton->isEnabled());
        d->reject();
    }
    void workshopMotionExport() {
        Editor w(storage.path());
        w.show();
        QVERIFY(w.openPath(QStringLiteral(REFERENCE_DIR) + "/patina-motion.gif"));
        QTRY_VERIFY(w.ready());
        findButton(w, "效果工坊")->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        auto *effect = d->findChild<QComboBox *>("workshopEffect");
        auto *save = d->findChild<QPushButton *>("primary");
        QString artifactDir = qEnvironmentVariable("AFTERIMAGE_WORKSHOP_GIFS");
        for (WorkshopEffect kind : {WorkshopEffect::TimeSlice, WorkshopEffect::Exposure}) {
            effect->setCurrentText(workshopName(kind));
            QTRY_VERIFY(save->isEnabled());
            QString name = kind == WorkshopEffect::TimeSlice ? "time-slice.gif" : "exposure.gif";
            QString path = storage.filePath(name);
            chooseFile(path);
            save->click();
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path), 5000);
            auto loaded = openPatinaInput(path, nullptr);
            QCOMPARE(loaded.timing.delays, QList<quint16>({4, 11, 7}));
            QCOMPARE(loaded.timing.repeats, 3);
            if (!artifactDir.isEmpty())
                QVERIFY(writeAtomically(artifactDir + "/" + name, loaded.gif, nullptr));
        }
        d->reject();
    }
    void creativeStillExports() {
        Editor w(storage.path());
        w.show();
        QTRY_VERIFY(w.ready());
        findButton(w, "效果工坊")->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        auto *choose = d->findChild<QComboBox *>("workshopEffect");
        auto *save = d->findChild<QPushButton *>("primary");
        choose->setCurrentText("像素排序");
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        choose->setCurrentText("等高线海报");
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        choose->setCurrentText("明暗双图");
        auto *second = findButton(*d, "选择黑底图");
        QVERIFY(second);
        chooseFile(QStringLiteral(REFERENCE_DIR) + "/input.png");
        second->click();
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        for (auto *spin : d->findChildren<QSpinBox *>())
            if (spin->accessibleName() == "预览背景")
                spin->setValue(0);
        QVERIFY(save->isEnabled());
        QString png = storage.filePath("light-dark.png");
        chooseFile(png);
        save->click();
        QImage photo(png);
        QVERIFY(!photo.isNull() && photo.hasAlphaChannel());
        choose->setCurrentText("字符拼画");
        for (auto *edit : d->findChildren<QLineEdit *>())
            if (edit->accessibleName() == "字符集")
                edit->setText(" 海岸@");
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        QString text = storage.filePath("glyphs.txt");
        chooseFile(text);
        save->click();
        QFile data(text);
        QVERIFY(data.open(QIODevice::ReadOnly));
        auto rendered = QString::fromUtf8(data.readAll());
        QVERIFY(rendered.contains("海") || rendered.contains("岸"));
        QVERIFY(rendered.count('\n') > 2);
        choose->setCurrentText("视频反馈隧道");
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 30000);
        auto *canvas = d->findChild<Canvas *>();
        QVERIFY(canvas);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          canvas->rect().center() + QPoint(38, -15));
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        QVERIFY(!findButton(*d, "送回主画布")->isEnabled());
        QString gif = storage.filePath("feedback.gif");
        chooseFile(gif);
        save->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(gif), 30000);
        const auto animation = openPatinaInput(gif, nullptr);
        QCOMPARE(animation.timing.delays.size(), 24);
        QCOMPARE(animation.timing.repeats, 0);
        QString demo = qEnvironmentVariable("AFTERIMAGE_FEEDBACK_DEMO");
        if (!demo.isEmpty())
            QVERIFY(writeAtomically(demo, animation.gif, nullptr));
        d->reject();
    }
    void paintMotionMaskAndExport() {
        Editor w(storage.path());
        w.show();
        QVERIFY(w.openPath(QStringLiteral(REFERENCE_DIR) + "/patina-motion.gif"));
        QTRY_VERIFY(w.ready());
        findButton(w, "效果工坊")->click();
        auto *d = w.findChild<PatinaDialog *>();
        QVERIFY(d);
        d->findChild<QComboBox *>("workshopEffect")->setCurrentText("局部动图");
        QTimer::singleShot(150, qApp, [] {
            auto *mask = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!mask)
                return;
            auto *drawing = mask->findChild<QWidget *>("motionMaskCanvas");
            if (!drawing) {
                mask->reject();
                return;
            }
            for (auto *spin : mask->findChildren<QSpinBox *>()) {
                if (spin->accessibleName() == "静止底帧")
                    spin->setValue(2);
                if (spin->accessibleName() == "笔径")
                    spin->setValue(42);
            }
            const auto center = drawing->rect().center();
            QTest::mousePress(drawing, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseMove(drawing, center + QPoint(15, 0));
            QTest::mouseRelease(drawing, Qt::LeftButton, Qt::NoModifier, center + QPoint(15, 0));
            QTimer::singleShot(350, mask, [mask] {
                auto *done = findButton(*mask, "应用蒙版");
                if (done && done->isEnabled())
                    done->click();
                else
                    mask->reject();
            });
        });
        findButton(*d, "绘制运动区域")->click();
        auto *save = d->findChild<QPushButton *>("primary");
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 10000);
        QString gif = storage.filePath("cinema.gif");
        chooseFile(gif);
        save->click();
        auto output = openPatinaInput(gif, nullptr);
        QCOMPARE(output.timing.delays, QList<quint16>({4, 11, 7}));
        QCOMPARE(output.timing.repeats, 3);
        QString artifacts = qEnvironmentVariable("AFTERIMAGE_WORKSHOP_GIFS");
        if (!artifacts.isEmpty())
            QVERIFY(writeAtomically(artifacts + "/cinema.gif", output.gif, nullptr));
        d->reject();
    }
    void openingSavingAndPresets() {
        Editor w(storage.path());
        w.show();
        w.activateWindow();
        QTRY_VERIFY(w.ready());
        QImage fixture = colorBars().scaled(231, 141);
        QString input = storage.filePath("输入 文件.png");
        QVERIFY(fixture.save(input));
        chooseFile(input);
        findButton(w, "打开图像")->click();
        QTRY_VERIFY(w.ready());
        auto *origin = w.findChild<QCheckBox *>("sizeOrigin");
        QVERIFY(origin);
        origin->setChecked(true);
        QTRY_VERIFY(w.ready());
        QCOMPARE(w.currentFrame().processed.size(), fixture.size());
        QString output = storage.filePath("导出 文件.png");
        chooseFile(output);
        findButton(w, "导出图像")->click();
        QCOMPARE(readImage(output).convertToFormat(QImage::Format_RGB32),
                 w.currentFrame().processed.convertToFormat(QImage::Format_RGB32));
        QString customPath = storage.filePath("水印.png");
        QImage custom(30, 20, QImage::Format_ARGB32);
        custom.fill(Qt::red);
        QVERIFY(custom.save(customPath));
        w.findChild<QCheckBox *>("watermark")->setChecked(true);
        chooseFile(customPath);
        findButton(w, "选择水印")->click();
        QTRY_VERIFY(w.ready());
        QTimer::singleShot(80, qApp, [] {
            if (auto *d = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                d->setTextValue("本机测试");
                d->accept();
            }
        });
        findButton(w, "保存当前预设")->click();
        QTRY_VERIFY(w.ready());
        const auto saved = readCollection(storage.filePath("personal.json"));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first()["name"].toString(), "本机测试");
        QVERIFY(saved.first().contains("logoPng"));
        Editor reopened(storage.path());
        reopened.show();
        QTRY_VERIFY(reopened.ready());
        findButton(reopened, "个人")->click();
        auto *list = reopened.findChild<QListWidget *>();
        QCOMPARE(list->count(), 1);
        list->setCurrentRow(0);
        QTRY_VERIFY(reopened.ready());
        QVERIFY(reopened.findChild<QCheckBox *>("watermark")->isChecked());
        QImage pasted(444, 230, QImage::Format_RGB32);
        pasted.fill(Qt::blue);
        QApplication::clipboard()->setImage(pasted);
        reopened.activateWindow();
        QTest::qWait(60);
        QTest::keyClick(&reopened, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.currentFrame().processed.size(), pasted.size());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(input)});
        QDragEnterEvent enter({8, 8}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&reopened, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop({8, 8}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&reopened, &drop);
        QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.currentFrame().processed.size(), fixture.size());
    }
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
