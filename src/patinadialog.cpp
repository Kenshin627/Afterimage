#include "patinadialog.h"
#include "style.h"
#include "widgets.h"
#include "workshopcontrols.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRandomGenerator>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace {
QWidget *scrollable(QWidget *body) {
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);
    return scroll;
}
void fieldRow(QVBoxLayout *layout, const QString &title, QWidget *control) {
    auto *w = new QWidget;
    auto *row = horizontal(w, 0, 6);
    row->addWidget(label(title));
    row->addStretch();
    row->addWidget(control);
    layout->addWidget(w);
}
QString directory(const QString &key) {
    return QSettings()
        .value(key, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
        .toString();
}
} // namespace

PatinaDialog::PatinaDialog(PatinaInput doc, QImage studioImage, PatinaSettings options,
                           WorkshopSettings workshopOptions, QWidget *parent)
    : QDialog(parent), original(std::move(doc)), studio{studioImage, "蒸汽波效果", {}, {}},
      config(std::move(options)), workshop(std::move(workshopOptions)) {
    setWindowTitle("效果工坊 · 余像 Afterimage");
    setObjectName("patinaDialog");
    setAcceptDrops(true);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    auto *verticalLayout = vertical(this);
    auto *header = new QWidget;
    header->setObjectName("mast");
    auto *head = horizontal(header, 0, 10);
    head->setContentsMargins(18, 0, 18, 0);
    header->setFixedHeight(58);
    auto *mark = label({});
    mark->setPixmap(glyph("grain", QColor("#d0ed84")).pixmap(23));
    head->addWidget(mark);
    head->addWidget(label("效果工坊", "heading"));
    head->addStretch();
    openBtn = button("打开图像", "open");
    head->addWidget(openBtn);
    connect(openBtn, &QPushButton::clicked, this, &PatinaDialog::openFile);
    exportBtn = button("导出图像", "export", "primary");
    exportBtn->setObjectName("primary");
    head->addWidget(exportBtn);
    connect(exportBtn, &QPushButton::clicked, this, &PatinaDialog::exportFile);
    verticalLayout->addWidget(header);
    verticalLayout->addWidget(rule());
    auto *body = new QWidget;
    auto *bodyRow = horizontal(body);
    auto *stage = new QWidget;
    auto *stageLayout = vertical(stage);
    auto *sourceRow = new QWidget;
    auto *sr = horizontal(sourceRow, 0, 7);
    sr->setContentsMargins(14, 6, 12, 6);
    sr->addWidget(label("输入", "subtle"));
    sourceMenu = new QComboBox;
    sourceMenu->setObjectName("patinaSource");
    sourceMenu->addItem("主画布原图");
    setStudioEffect(studioImage);
    sourceMenu->setMinimumContentsLength(5);
    sr->addWidget(sourceMenu);
    sr->addStretch();
    auto *mode = new QComboBox;
    mode->setAccessibleName("预览方式");
    mode->addItems({"原图", "效果", "分屏对比"});
    mode->setCurrentIndex(1);
    sr->addWidget(mode);
    stageLayout->addWidget(sourceRow);
    canvas = new Canvas;
    connect(canvas, &Canvas::anchorPlaced, this, [this](QPointF point) {
        if (effect == WorkshopEffect::Feedback) {
            workshop.feedback.x = qRound(100 * point.x());
            workshop.feedback.y = qRound(100 * point.y());
            edited();
        }
    });
    stageLayout->addWidget(canvas, 1);
    connect(mode, &QComboBox::currentIndexChanged, canvas, &Canvas::setMode);
    auto *infoRow = new QWidget;
    auto *ir = horizontal(infoRow, 0, 5);
    ir->setContentsMargins(13, 4, 10, 4);
    fileInfo = label({}, "mono");
    fileInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ir->addWidget(fileInfo, 1);
    againBtn = button("继续包浆", {}, "quiet");
    againBtn->setToolTip("把当前结果作为输入，再做旧一次");
    ir->addWidget(againBtn);
    connect(againBtn, &QPushButton::clicked, this, [this] { useResult(false); });
    stageLayout->addWidget(infoRow);
    bodyRow->addWidget(stage, 1);
    bodyRow->addWidget(rule(true));
    settingsPanel = controls();
    bodyRow->addWidget(settingsPanel);
    verticalLayout->addWidget(body, 1);
    verticalLayout->addWidget(rule());
    auto *footer = new QWidget;
    footer->setObjectName("foot");
    auto *fr = horizontal(footer, 0, 7);
    fr->setContentsMargins(14, 5, 14, 5);
    feedback = label({}, "subtle");
    feedback->setObjectName("patinaStatus");
    feedback->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fr->addWidget(feedback, 1);
    bar = new QProgressBar;
    bar->setTextVisible(false);
    bar->setFixedSize(70, 5);
    fr->addWidget(bar);
    cancelBtn = button("取消处理", {}, "quiet");
    fr->addWidget(cancelBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &PatinaDialog::stop);
    animateBtn = button("渲染 GIF");
    fr->addWidget(animateBtn);
    connect(animateBtn, &QPushButton::clicked, this, [this] { request(Output::Animation); });
    applyBtn = button("送回主画布");
    fr->addWidget(applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, [this] { useResult(true); });
    verticalLayout->addWidget(footer);
    connect(sourceMenu, &QComboBox::currentIndexChanged, this, &PatinaDialog::selectSource);
    debounce.setSingleShot(true);
    debounce.setInterval(100);
    connect(&debounce, &QTimer::timeout, this, &PatinaDialog::start);
    poll.setInterval(95);
    connect(&poll, &QTimer::timeout, this, &PatinaDialog::refreshProgress);
    connect(&watcher, &QFutureWatcher<PatinaResult>::finished, this, &PatinaDialog::finish);
    connect(&movie, &QMovie::frameChanged, this, [this](int index) {
        if (result.gif.isEmpty())
            return;
        QImage before = input.image;
        if (input.animated() && originalMovie.jumpToFrame(index))
            before = originalMovie.currentImage();
        Frame view;
        view.original = before;
        view.processed = movie.currentImage();
        canvas->showFrame(view);
    });
    auto shortcut = [&](const QKeySequence &seq, auto fn) {
        auto *key = new QShortcut(seq, this);
        connect(key, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Open, [this] { openFile(); });
    shortcut(QKeySequence::Paste, [this] { paste(); });
    shortcut(QKeySequence::Save, [this] { exportFile(); });
    setMinimumSize(670, 460);
    const auto screen = parent->screen()->availableSize();
    resize(std::min(1120, int(screen.width() * .88)), std::min(730, int(screen.height() * .85)));
    selectSource(0);
}
PatinaDialog::~PatinaDialog() {
    if (cancel)
        cancel->store(true);
    watcher.waitForFinished();
    clearPlayer();
}
void PatinaDialog::setStudioEffect(const QImage &image) {
    if (image.isNull())
        return;
    studio.image = image;
    if (sourceMenu->findData(1) < 0)
        sourceMenu->addItem(original.animated() ? "蒸汽波效果（首帧）" : "蒸汽波效果", 1);
}

QWidget *PatinaDialog::controls() {
    auto *panel = new QWidget;
    panel->setObjectName("rightPanel");
    panel->setFixedWidth(282);
    auto *layout = vertical(panel, 0, 0);
    auto *selector = new QWidget;
    auto *chooserRow = horizontal(selector, 12, 8);
    chooserRow->addWidget(label("效果", "subtle"));
    auto *chooser = new QComboBox;
    chooser->setObjectName("workshopEffect");
    chooser->setAccessibleName("效果");
    chooserRow->addWidget(chooser, 1);
    layout->addWidget(selector);
    layout->addWidget(rule());
    effectPages = new QStackedWidget;
    layout->addWidget(effectPages, 1);
    auto *patinaPage = new QWidget;
    auto *patinaLayout = vertical(patinaPage);
    tabs = new QTabWidget;
    tabs->setObjectName("patinaTabs");
    tabs->addTab(scrollable(basicPanel()), "常用");
    tabs->addTab(scrollable(advancedPanel()), "高级");
    patinaLayout->addWidget(tabs, 1);
    patinaLayout->addWidget(rule());
    auto *actions = new QWidget;
    auto *buttons = horizontal(actions, 9, 6);
    reroll = button("换一组随机效果");
    auto *reset = button("恢复默认", {}, "quiet");
    buttons->addWidget(reroll, 1);
    buttons->addWidget(reset);
    connect(reroll, &QPushButton::clicked, this, [this] {
        config.seed = QRandomGenerator::global()->generate();
        edited();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        auto names = config.names;
        config = PatinaSettings();
        config.names = names;
        edited();
    });
    patinaLayout->addWidget(actions);
    effectPages->addWidget(patinaPage);
    chooser->addItem(workshopName(WorkshopEffect::Patina), int(WorkshopEffect::Patina));
    for (auto item : {WorkshopEffect::Photocopy, WorkshopEffect::Duotone, WorkshopEffect::CRT,
                      WorkshopEffect::Thermal, WorkshopEffect::TimeSlice, WorkshopEffect::Exposure,
                      WorkshopEffect::PixelSort, WorkshopEffect::DualImage, WorkshopEffect::Cinemagraph,
                      WorkshopEffect::Glyphs, WorkshopEffect::Contours, WorkshopEffect::Feedback}) {
        chooser->addItem(workshopName(item), int(item));
        effectPages->addWidget(scrollable(workshopControls(
            item, workshop, [this] { edited(); }, [this]() -> const PatinaInput & { return input; },
            [this] { canvas->setPreviewBackground(workshop.dual.background); })));
    }
    connect(chooser, &QComboBox::currentIndexChanged, this, [this, chooser](int index) {
        effect = static_cast<WorkshopEffect>(chooser->itemData(index).toInt());
        effectPages->setCurrentIndex(index);
        againBtn->setText(effect == WorkshopEffect::Patina ? "继续包浆" : "作为下次输入");
        againBtn->setToolTip("将当前结果作为下一种效果的输入");
        selectSource(sourceMenu->currentIndex());
    });
    return panel;
}
QCheckBox *PatinaDialog::check(QString title, bool PatinaSettings::*field, QVBoxLayout *layout) {
    auto *box = new QCheckBox(title);
    layout->addWidget(box);
    refreshers.append([this, box, field] { box->setChecked(config.*field); });
    connect(box, &QCheckBox::toggled, this, [this, field](bool value) {
        if (syncing)
            return;
        config.*field = value;
        edited();
    });
    return box;
}
QSpinBox *PatinaDialog::integer(QString title, int PatinaSettings::*field, int lo, int hi,
                                QVBoxLayout *layout, bool slider) {
    auto *spin = new QSpinBox;
    spin->setRange(lo, hi);
    spin->setKeyboardTracking(false);
    spin->setAccessibleName(title);
    spin->setFixedWidth(72);
    fieldRow(layout, title, spin);
    QSlider *track = nullptr;
    if (slider) {
        track = new QSlider(Qt::Horizontal);
        track->setRange(lo, hi);
        track->setAccessibleName(title);
        layout->addWidget(track);
        connect(track, &QSlider::valueChanged, this, [this, spin](int v) {
            if (!syncing)
                spin->setValue(v);
        });
    }
    refreshers.append([this, spin, track, field] {
        spin->setValue(config.*field);
        if (track) {
            track->setRange(spin->minimum(), spin->maximum());
            track->setValue(config.*field);
            track->setEnabled(spin->isEnabled());
        }
    });
    connect(spin, &QSpinBox::valueChanged, this, [this, field](int v) {
        if (syncing)
            return;
        config.*field = v;
        edited();
    });
    return spin;
}
void PatinaDialog::decimal(QString title, double PatinaSettings::*field, double lo, double hi, double step,
                           QVBoxLayout *layout) {
    auto *spin = new QDoubleSpinBox;
    spin->setRange(lo, hi);
    spin->setDecimals(step >= 1 ? 0 : 1);
    spin->setSingleStep(step);
    spin->setFixedWidth(78);
    spin->setKeyboardTracking(false);
    spin->setAccessibleName(title);
    fieldRow(layout, title, spin);
    refreshers.append([this, spin, field] { spin->setValue(config.*field); });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
        if (syncing)
            return;
        config.*field = v;
        edited();
    });
}
QWidget *PatinaDialog::basicPanel() {
    auto *panel = new QWidget;
    auto *v = vertical(panel, 15, 9);
    years = integer("做旧年份", &PatinaSettings::years, 1, 100, v, true);
    years->setObjectName("patinaYears");
    quality = integer("JPEG 画质（%）", &PatinaSettings::quality, 0, 100, v, true);
    check("绿图", &PatinaSettings::green, v);
    check("叠加转发水印", &PatinaSettings::watermark, v);
    popBox = check("波普拼图", &PatinaSettings::pop, v);
    grid = integer("每行格数", &PatinaSettings::grid, 1, 12, v);
    grid->setObjectName("patinaGrid");
    auto *hint = label("每一年重新压缩一次。波普按从左到右、从上到下的顺序记录每一轮。", "subtle");
    hint->setWordWrap(true);
    v->addWidget(hint);
    v->addWidget(rule());
    check("缩略图模式", &PatinaSettings::thumbnail, v);
    edge = integer("最长边（像素）", &PatinaSettings::maxEdge, 200, 1200, v);
    edge->setSingleStep(100);
    v->addStretch();
    return panel;
}
QWidget *PatinaDialog::advancedPanel() {
    auto *panel = new QWidget;
    auto *v = vertical(panel, 15, 8);
    check("随机错位与画质波动", &PatinaSettings::random, v);
    auto *super = check("超强模式", &PatinaSettings::superMode, v);
    super->setToolTip("年份上限 1000，波普上限 50 × 50");
    integer("相对尺寸（%）", &PatinaSettings::zoom, 1, 100, v);
    decimal("模糊", &PatinaSettings::blur, 1, 20, .1, v);
    decimal("亮度噪点", &PatinaSettings::noise, 0, 125, 1, v);
    decimal("暗部颗粒", &PatinaSettings::grain, 0, 125, 1, v);
    decimal("亮度", &PatinaSettings::light, -1, 1, .1, v);
    decimal("对比度", &PatinaSettings::contrast, 0, 2, .1, v);
    integer("水平偏移（像素）", &PatinaSettings::shiftX, -20, 20, v);
    integer("垂直偏移（像素）", &PatinaSettings::shiftY, -20, 20, v);
    decimal("YUV 绿化强度", &PatinaSettings::greenBoost, -20, 20, 1, v);
    decimal("绿化亮度系数", &PatinaSettings::greenLuma, -2, 2, .1, v);
    auto *kernel = new QComboBox;
    kernel->addItems({"无", "右倾", "左倾", "桑拿", "浮雕"});
    fieldRow(v, "卷积效果", kernel);
    refreshers.append([this, kernel] {
        kernel->setCurrentIndex(config.kernel.isEmpty() ? 0 : kernel->findText(config.kernel));
    });
    connect(kernel, &QComboBox::currentIndexChanged, this, [this, kernel](int i) {
        if (!syncing) {
            config.kernel = i ? kernel->itemText(i) : QString();
            edited();
        }
    });
    v->addWidget(rule());
    namesGroup = new QWidget;
    auto *stamps = vertical(namesGroup, 0, 7);
    stamps->addWidget(label("转发水印", "heading"));
    decimal("字号比例", &PatinaSettings::stampSize, .5, 1.5, .1, stamps);
    decimal("阴影透明度", &PatinaSettings::shadow, 0, 1, .1, stamps);
    auto *zone = new QComboBox;
    zone->addItems({"右下角", "右下角、底部", "右下角、底部、中央"});
    fieldRow(stamps, "落点范围", zone);
    refreshers.append([this, zone] { zone->setCurrentIndex(config.positions); });
    connect(zone, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!syncing) {
            config.positions = i;
            edited();
        }
    });
    stamps->addWidget(label("用户名（每行一个）"));
    auto *names = new QPlainTextEdit;
    names->setObjectName("patinaNames");
    names->setFixedHeight(90);
    names->setPlaceholderText("像素电台_0000");
    refreshers.append([this, names] {
        if (!names->hasFocus() && names->toPlainText() != config.names.join('\n'))
            names->setPlainText(config.names.join('\n'));
    });
    connect(names, &QPlainTextEdit::textChanged, this, [this, names] {
        if (syncing)
            return;
        config.names.clear();
        for (auto line : names->toPlainText().split('\n'))
            if (!line.trimmed().isEmpty())
                config.names << line.trimmed();
        edited();
    });
    stamps->addWidget(names);
    auto *tip = label("0000 替换为四位数字，_ 替换为随机符号。", "subtle");
    tip->setWordWrap(true);
    stamps->addWidget(tip);
    v->addWidget(namesGroup);
    v->addStretch();
    return panel;
}

void PatinaDialog::sync() {
    syncing = true;
    canvas->setPreviewBackground(effect == WorkshopEffect::DualImage ? workshop.dual.background : -1);
    canvas->setFeedbackAnchor(effect == WorkshopEffect::Feedback,
                              {workshop.feedback.x * .01, workshop.feedback.y * .01});
    config.years = std::clamp(config.years, 1, config.superMode ? 1000 : 100);
    config.grid = std::clamp(config.grid, 1, config.superMode ? 50 : 12);
    years->setMaximum(config.superMode ? 1000 : 100);
    grid->setMaximum(config.superMode ? 50 : 12);
    const bool canPop = !input.animated();
    popBox->setEnabled(canPop);
    years->setEnabled(!config.pop || !canPop);
    grid->setEnabled(config.pop && canPop);
    namesGroup->setEnabled(config.watermark);
    edge->setEnabled(config.thumbnail);
    reroll->setEnabled(config.random);
    for (const auto &fn : refreshers)
        fn();
    if (!canPop)
        popBox->setChecked(false);
    syncing = false;
}
void PatinaDialog::edited() {
    sync();
    request(requiresMotion(effect) && input.animated() ? Output::Animation : Output::Preview);
}
void PatinaDialog::selectSource(int index) {
    const int value = sourceMenu->itemData(index).toInt();
    input = value == 1 ? studio : value == 2 ? other : original;
    canvas->resetView();
    sync();
    Frame view;
    view.original = input.image;
    view.processed = input.image;
    canvas->showFrame(view);
    fileInfo->setText(input.name);
    const bool motion = animatedOutput(effect, input);
    animateBtn->setVisible(motion);
    exportBtn->setText(motion                                ? "导出 GIF…"
                       : effect == WorkshopEffect::DualImage ? "导出透明 PNG"
                                                             : "导出图像");
    request(motion ? Output::Animation : Output::Preview);
}
void PatinaDialog::clearPlayer() {
    QSignalBlocker silence(movie);
    movie.stop();
    originalMovie.stop();
    movie.setDevice(nullptr);
    originalMovie.setDevice(nullptr);
    gifBuffer.close();
    originalBuffer.close();
    gifBuffer.setData({});
    originalBuffer.setData({});
}
void PatinaDialog::request(Output render, QString path) {
    ++revision;
    pending = true;
    queued = render;
    saveAfter = std::move(path);
    if (cancel)
        cancel->store(true);
    clearPlayer();
    result = {};
    exportBtn->setEnabled(false);
    againBtn->setEnabled(false);
    applyBtn->setEnabled(false);
    animateBtn->setEnabled(false);
    feedback->setText(render == Output::Animation ? "正在生成动图…" : "正在生成预览…");
    cancelBtn->show();
    bar->show();
    bar->setRange(0, 0);
    settingsPanel->setEnabled(saveAfter.isEmpty());
    sourceMenu->setEnabled(saveAfter.isEmpty());
    openBtn->setEnabled(saveAfter.isEmpty());
    debounce.start();
}
void PatinaDialog::start() {
    if (!pending || running)
        return;
    pending = false;
    running = true;
    activeRevision = revision;
    cancel = std::make_shared<std::atomic_bool>(false);
    progress = std::make_shared<Progress>();
    auto state = progress;
    auto flag = cancel;
    auto cfg = config;
    auto chosen = effect;
    auto options = workshop;
    auto doc = input;
    bool full = queued == Output::Animation;
    poll.start();
    watcher.setFuture(QtConcurrent::run([doc, cfg, chosen, options, state, flag, full] {
        const auto report = [state](int done, int total) {
            state->total.store(total);
            state->done.store(done);
        };
        return chosen == WorkshopEffect::Patina ? renderPatina(doc, cfg, full, flag, report)
                                                : renderWorkshop(doc, chosen, options, full, flag, report);
    }));
}
void PatinaDialog::refreshProgress() {
    if (!running || !progress || activeRevision != revision)
        return;
    const int total = progress->total.load(), done = progress->done.load();
    bar->setRange(0, total);
    bar->setValue(done);
    if (total > 0)
        feedback->setText(QString("正在处理  %1 / %2").arg(done).arg(total));
}
void PatinaDialog::finish() {
    running = false;
    poll.stop();
    if (activeRevision != revision) {
        if (!debounce.isActive())
            start();
        return;
    }
    result = watcher.result();
    bar->hide();
    cancelBtn->hide();
    settingsPanel->setEnabled(true);
    sourceMenu->setEnabled(true);
    openBtn->setEnabled(true);
    animateBtn->setEnabled(true);
    if (!result.frame.error.isEmpty() || result.frame.processed.isNull()) {
        feedback->setText(result.frame.error);
        saveAfter.clear();
        return;
    }
    showResult();
    if (!saveAfter.isEmpty()) {
        const QString path = saveAfter;
        saveAfter.clear();
        save(path);
    }
}
void PatinaDialog::showResult() {
    canvas->showFrame(result.frame);
    const QSize size = result.frame.processed.size();
    fileInfo->setText(QString("%1  ·  %2 × %3 px").arg(input.name).arg(size.width()).arg(size.height()));
    exportBtn->setEnabled(true);
    const bool motion = animatedOutput(effect, input);
    againBtn->setEnabled(!motion || !result.gif.isEmpty());
    applyBtn->setEnabled(againBtn->isEnabled());
    animateBtn->setText(result.gif.isEmpty() ? "渲染整段 GIF" : "重新渲染 GIF");
    animateBtn->setVisible(motion);
    if (result.gif.isEmpty()) {
        feedback->setText(motion ? "首帧预览已更新，可渲染整段 GIF" : "已完成");
        return;
    }
    gifBuffer.setData(result.gif);
    gifBuffer.open(QIODevice::ReadOnly);
    movie.setDevice(&gifBuffer);
    movie.setFormat("gif");
    feedback->setText(QString("动图已完成  ·  %1 帧").arg(movie.frameCount()));
    if (input.animated()) {
        originalBuffer.setData(input.gif);
        originalBuffer.open(QIODevice::ReadOnly);
        originalMovie.setDevice(&originalBuffer);
        originalMovie.setFormat("gif");
    }
    movie.start();
}
void PatinaDialog::stop() {
    ++revision;
    pending = false;
    saveAfter.clear();
    debounce.stop();
    poll.stop();
    if (cancel)
        cancel->store(true);
    cancelBtn->hide();
    bar->hide();
    feedback->setText("已取消，可调整参数或重新渲染。");
    animateBtn->setEnabled(true);
    animateBtn->show();
    animateBtn->setText(animatedOutput(effect, input) ? "渲染整段 GIF" : "重新渲染");
    settingsPanel->setEnabled(true);
    sourceMenu->setEnabled(true);
    openBtn->setEnabled(true);
}
void PatinaDialog::setOther(PatinaInput doc) {
    if (doc.image.isNull())
        return;
    other = std::move(doc);
    const QSignalBlocker block(sourceMenu);
    int i = sourceMenu->findData(2);
    if (i < 0) {
        sourceMenu->addItem(other.name, 2);
        i = sourceMenu->count() - 1;
    } else
        sourceMenu->setItemText(i, other.name);
    sourceMenu->setCurrentIndex(i);
    selectSource(i);
}
void PatinaDialog::openFile() {
    if (!saveAfter.isEmpty())
        return;
    QString path = QFileDialog::getOpenFileName(
        this, "打开工坊素材", directory("importDir"),
        "图像 (*.gif *.jpg *.jpeg *.png *.webp *.bmp *.tif *.tiff);;所有文件 (*)");
    if (path.isEmpty())
        return;
    QString error;
    auto doc = openPatinaInput(path, &error);
    if (doc.image.isNull()) {
        QMessageBox::warning(this, "打开失败", error);
        return;
    }
    QSettings().setValue("importDir", QFileInfo(path).absolutePath());
    setOther(std::move(doc));
}
bool PatinaDialog::useMime(const QMimeData *m) {
    if (!m || !saveAfter.isEmpty())
        return false;
    QString error;
    for (const auto &url : m->urls())
        if (url.isLocalFile()) {
            auto doc = openPatinaInput(url.toLocalFile(), &error);
            if (!doc.image.isNull()) {
                setOther(std::move(doc));
                return true;
            }
            break;
        }
    if (m->hasFormat("image/gif")) {
        auto doc = gifPatinaInput(m->data("image/gif"), "剪贴板 GIF", &error);
        if (!doc.image.isNull()) {
            setOther(std::move(doc));
            return true;
        }
    }
    if (m->hasImage()) {
        auto img = qvariant_cast<QImage>(m->imageData());
        if (!img.isNull()) {
            setOther({img, "剪贴板图像", {}, {}});
            return true;
        }
    }
    feedback->setText(error.isEmpty() ? "未找到可读取的图像" : error);
    return false;
}
void PatinaDialog::paste() { useMime(QApplication::clipboard()->mimeData()); }
void PatinaDialog::dragEnterEvent(QDragEnterEvent *e) {
    const auto *m = e->mimeData();
    if (m->hasUrls() || m->hasImage() || m->hasFormat("image/gif"))
        e->acceptProposedAction();
}
void PatinaDialog::dropEvent(QDropEvent *e) {
    if (useMime(e->mimeData()))
        e->acceptProposedAction();
}
void PatinaDialog::useResult(bool sendToEditor) {
    if (result.frame.processed.isNull())
        return;
    PatinaInput doc{result.frame.processed,
                    QFileInfo(input.name).completeBaseName() + " · " + workshopName(effect),
                    {},
                    {}};
    if (animatedOutput(effect, input)) {
        if (result.gif.isEmpty())
            return;
        QString error;
        doc = gifPatinaInput(result.gif, doc.name, &error);
        if (doc.image.isNull()) {
            feedback->setText(error);
            return;
        }
    }
    if (sendToEditor) {
        applied = std::move(doc);
        accept();
    } else
        setOther(std::move(doc));
}
void PatinaDialog::exportFile() {
    if (result.frame.processed.isNull())
        return;
    QStringList filters = {"JPEG 图像 (*.jpg *.jpeg)", "PNG 图像 (*.png)", "WebP 图像 (*.webp)"};
    if (effect != WorkshopEffect::Patina)
        filters.swapItemsAt(0, 1);
    const bool motion = animatedOutput(effect, input);
    if (motion)
        filters = {"GIF 动图 (*.gif)", "JPEG 首帧 (*.jpg *.jpeg)", "PNG 首帧 (*.png)",
                   "WebP 首帧 (*.webp)"};
    if (effect == WorkshopEffect::DualImage)
        filters = {"透明 PNG (*.png)"};
    if (effect == WorkshopEffect::Glyphs)
        filters.append(motion ? "UTF-8 首帧纯文本 (*.txt)" : "UTF-8 纯文本 (*.txt)");
    QFileDialog dlg(this, "导出" + workshopName(effect), directory("exportDir"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setNameFilters(filters);
    dlg.setDefaultSuffix(motion ? "gif" : effect == WorkshopEffect::Patina ? "jpg" : "png");
    connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString &s) {
        dlg.setDefaultSuffix(s.startsWith("UTF-8")                           ? "txt"
                             : (s.startsWith("PNG") || s.startsWith("透明")) ? "png"
                             : s.startsWith("WebP")                          ? "webp"
                             : s.startsWith("GIF")                           ? "gif"
                                                                             : "jpg");
    });
    QString stem = QFileInfo(input.name).completeBaseName();
    for (auto ch : QStringLiteral("\\/:*?\"<>|"))
        stem.replace(ch, '_');
    dlg.selectFile(stem + " - " + workshopName(effect));
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty())
        return;
    const QString path = dlg.selectedFiles().first();
    if (QFileInfo(path).suffix().toLower() == "gif" && motion && result.gif.isEmpty()) {
        request(Output::Animation, path);
        return;
    }
    save(path);
}
void PatinaDialog::save(const QString &path) {
    QString ext = QFileInfo(path).suffix().toLower(), error;
    bool ok = false;
    if (effect == WorkshopEffect::DualImage && ext != "png")
        error = "请使用 .png 扩展名以保留双图的透明度。";
    else if (effect == WorkshopEffect::Glyphs && ext == "txt" && !result.plainText.isEmpty())
        ok = writeAtomically(path, result.plainText.toUtf8(), &error);
    else if (ext == "gif" && !result.gif.isEmpty())
        ok = writeAtomically(path, result.gif, &error);
    else if (QStringList{"jpg", "jpeg", "png", "webp"}.contains(ext))
        ok = saveFrame(result.frame, path, &error);
    else
        error = "请选择与输出格式相符的扩展名。";
    if (!ok) {
        QMessageBox::warning(this, "导出失败", error);
        return;
    }
    QSettings().setValue("exportDir", QFileInfo(path).absolutePath());
    feedback->setText("已保存  " + QFileInfo(path).fileName());
}
