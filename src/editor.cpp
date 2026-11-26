#include "editor.h"
#include "dialogs.h"
#include "inspector.h"
#include "patinadialog.h"
#include "style.h"
#include "widgets.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
QString dimensions(QSize size) {
    return QString("%1 × %2").arg(size.width()).arg(size.height());
}
QWidget *segments(const QStringList &names, QButtonGroup *&group, int active, QWidget *owner) {
    auto *widget = new QWidget;
    widget->setObjectName("segments");
    widget->setFixedHeight(30);
    auto *layout = horizontal(widget, 3, 2);
    group = new QButtonGroup(owner);
    for (int i = 0; i < names.size(); ++i) {
        auto *b = button(names[i], {}, "segment");
        b->setCheckable(true);
        b->setChecked(i == active);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(b);
        group->addButton(b, i);
    }
    return widget;
}
const QStringList factoryNames = {
    "Coastal VHS", "Midnight signal", "Silver gelatin", "Rising warmth", "Summer heat",
    "Faded film",  "Interlaced",      "Digital signal", "Sync failure",  "Signal silence",
    "Neon spill",  "Circa 1900",      "Color extremes", "Negative",      "Split temperature",
    "Video CD",    "Tokyo haze",      "Tangerine air",  "Solar flare",   "Nocturnal",
    "Blue hour",   "Chromatic noise"};
} // namespace

Editor::Editor(QString storageDirectory) : community(this) {
    patinaOptions.seed = QRandomGenerator::global()->generate();
    setWindowTitle("余像  Afterimage");
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/app.png"));
    dataDir = storageDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                         : storageDirectory;
    QDir().mkpath(dataDir);
    factory = studioPresets() + readCollection(":/factory.json");
    personal = readCollection(dataDir + "/personal.json");
    publicStyles = readCollection(dataDir + "/community.json");
    if (publicStyles.isEmpty())
        publicStyles = readCollection(":/community.json");
    QFile file(":/gallery.json");
    if (file.open(QIODevice::ReadOnly))
        for (auto v : QJsonDocument::fromJson(file.readAll()).array())
            galleryNames << v.toString();
    sampleNames = {"海岸列车", "午夜街角", "夏日泳池", "信号色条"};
    samples = {QImage(":/coast.jpg"), QImage(":/night.jpg"), QImage(":/pool.jpg"), colorBars()};
    thumbSource = samples.first().scaled(230, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    current = {factory.first(), factory.first()};
    auto *root = new QWidget;
    root->setObjectName("root");
    auto *layout = vertical(root);
    setCentralWidget(root);
    layout->addWidget(makeHeader());
    layout->addWidget(rule());
    auto *body = new QWidget;
    auto *row = horizontal(body);
    left = makeLibrary();
    left->setFixedWidth(216);
    row->addWidget(left);
    row->addWidget(rule(true));
    row->addWidget(makeStage(), 1);
    row->addWidget(rule(true));
    inspector = new Inspector;
    inspector->setFixedWidth(255);
    row->addWidget(inspector);
    layout->addWidget(body, 1);
    layout->addWidget(rule());
    layout->addWidget(makeFooter());
    makeMenus();
    connect(inspector, &Inspector::edit, this, &Editor::change);
    connect(inspector, &Inspector::beginGesture, this, [this] {
        gestureStart = current;
        gesturing = true;
    });
    connect(inspector, &Inspector::endGesture, this, [this] {
        if (gesturing) {
            gesturing = false;
            remember(gestureStart);
            updateControls();
        }
    });
    connect(inspector, &Inspector::resetStyle, this, [this] { applyPreset(current.base); });
    connect(inspector, &Inspector::chooseLogo, this, &Editor::chooseWatermark);
    connect(inspector, &Inspector::resetLogo, this, [this] { change("logoPng", QJsonValue::Undefined); });
    debounce.setSingleShot(true);
    debounce.setInterval(24);
    connect(&debounce, &QTimer::timeout, this, &Editor::startRender);
    connect(&worker, &QFutureWatcher<Frame>::finished, this, &Editor::finishRender);
    noticeTimer.setSingleShot(true);
    connect(&noticeTimer, &QTimer::timeout, this, [this] {
        if (ready())
            status->setText("就绪");
    });
    rebuildLibrary();
    updateControls();
    setSource(samples.first(), sampleNames.first(), 0);
    canvas->setFocus();
    const auto available = QApplication::primaryScreen()->availableGeometry();
    setMinimumSize(865, 535);
    resize(std::min(1420, int(available.width() * .92)), std::min(880, int(available.height() * .88)));
#ifdef Q_OS_WIN
    BOOL dark = TRUE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20, &dark, sizeof(dark));
#endif
}
Editor::~Editor() {
    if (canceled)
        canceled->store(true);
    worker.waitForFinished();
}

QWidget *Editor::makeHeader() {
    auto *w = new QWidget;
    w->setObjectName("mast");
    w->setFixedHeight(65);
    auto *r = horizontal(w, 0, 10);
    r->setContentsMargins(20, 0, 18, 0);
    auto *brand = label({});
    brand->setPixmap(QPixmap(":/app.png").scaled(34, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    r->addWidget(brand);
    auto *word = label("AFTERIMAGE", "brand");
    r->addWidget(word);
    r->addSpacing(3);
    auto *divider = rule(true);
    divider->setFixedHeight(21);
    r->addWidget(divider);
    r->addSpacing(3);
    r->addWidget(label("余像工作室", "brandCn"));
    r->addStretch();
    auto *patina = button("效果工坊", "grain");
    patina->setObjectName("patinaTool");
    patina->setToolTip("电子包浆、印刷、显像管和 GIF 时间效果");
    r->addWidget(patina);
    connect(patina, &QPushButton::clicked, this, &Editor::openPatina);
    undoButton = button({}, "undo", "icon");
    redoButton = button({}, "redo", "icon");
    undoButton->setToolTip("撤销（Ctrl+Z）");
    redoButton->setToolTip("重做（Ctrl+Y）");
    undoButton->setAccessibleName("撤销");
    redoButton->setAccessibleName("重做");
    connect(undoButton, &QPushButton::clicked, this, [this] { undo(false); });
    connect(redoButton, &QPushButton::clicked, this, [this] { undo(true); });
    r->addWidget(undoButton);
    r->addWidget(redoButton);
    auto *sep = rule(true);
    sep->setFixedHeight(22);
    r->addWidget(sep);
    r->addSpacing(1);
    auto *open = button("打开图像", "open");
    open->setToolTip("打开图像（Ctrl+O），也可拖入或粘贴");
    r->addWidget(open);
    connect(open, &QPushButton::clicked, this, &Editor::openImage);
    exportButton = button("导出图像", "export", "primary");
    exportButton->setToolTip("导出效果图（Ctrl+S）");
    r->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, &Editor::exportImage);
    moreButton = button({}, "more", "icon");
    moreButton->setToolTip("更多操作");
    moreButton->setAccessibleName("更多操作");
    r->addWidget(moreButton);
    return w;
}

QWidget *Editor::makeLibrary() {
    auto *w = new QWidget;
    w->setObjectName("leftPanel");
    auto *layout = vertical(w);
    auto *head = new QWidget;
    auto *hr = horizontal(head, 0, 8);
    hr->setContentsMargins(16, 0, 10, 0);
    head->setFixedHeight(58);
    hr->addWidget(label("预设库", "heading"));
    libraryCount = label({}, "count");
    hr->addWidget(libraryCount);
    hr->addStretch();
    auto *more = button({}, "more", "icon");
    more->setToolTip("管理、导入和分享预设");
    more->setAccessibleName("预设操作");
    hr->addWidget(more);
    connect(more, &QPushButton::clicked, this, [this, more] { libraryMenu(more); });
    layout->addWidget(head);
    auto *filters = new QWidget;
    auto *f = vertical(filters, 0, 9);
    f->setContentsMargins(12, 0, 12, 10);
    f->addWidget(segments({"内置", "个人", "公共"}, libraryTabs, 0, this));
    connect(libraryTabs, &QButtonGroup::idClicked, this, &Editor::showLibrary);
    search = new QLineEdit;
    search->setPlaceholderText("搜索预设");
    search->addAction(glyph("search", QColor("#969c93")), QLineEdit::LeadingPosition);
    search->setClearButtonEnabled(true);
    f->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, &Editor::rebuildLibrary);
    layout->addWidget(filters);
    presetList = new QListWidget;
    presetList->setMouseTracking(true);
    presetList->setItemDelegate(new PresetDelegate(presetList));
    presetList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    presetList->setVerticalScrollMode(QListView::ScrollPerPixel);
    auto *listWrap = new QWidget;
    auto *lw = vertical(listWrap);
    lw->setContentsMargins(7, 0, 5, 0);
    lw->addWidget(presetList);
    emptyLibrary = label({}, "subtle");
    emptyLibrary->setAlignment(Qt::AlignCenter);
    emptyLibrary->setWordWrap(true);
    lw->addWidget(emptyLibrary);
    layout->addWidget(listWrap, 1);
    connect(presetList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            applyPreset(item->data(Qt::UserRole).toJsonObject());
    });
    connect(presetList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QJsonObject preset = item->data(Qt::UserRole).toJsonObject();
        if (current.style != preset)
            applyPreset(preset);
    });
    layout->addWidget(rule());
    auto *bottom = new QWidget;
    auto *foot = vertical(bottom, 12);
    auto *save = button("保存当前预设", "plus");
    foot->addWidget(save);
    connect(save, &QPushButton::clicked, this, &Editor::savePersonal);
    layout->addWidget(bottom);
    return w;
}

QWidget *Editor::makeStage() {
    auto *w = new QWidget;
    auto *v = vertical(w);
    auto *head = new QWidget;
    head->setObjectName("stageHead");
    head->setFixedHeight(58);
    auto *hr = horizontal(head, 0, 9);
    hr->setContentsMargins(17, 0, 14, 0);
    auto *fileIcon = label({});
    fileIcon->setPixmap(glyph("image").pixmap(19, 19));
    hr->addWidget(fileIcon);
    auto *fileBox = new QWidget;
    auto *fl = vertical(fileBox, 0, 0);
    docLabel = label({});
    docLabel->setFont(typeface("Microsoft YaHei UI", 14, QFont::DemiBold));
    docLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    docLabel->setFixedHeight(22);
    sourceSize = label({}, "mono");
    sourceSize->setFixedHeight(16);
    fl->setAlignment(Qt::AlignVCenter);
    fl->addWidget(docLabel);
    fl->addWidget(sourceSize);
    hr->addWidget(fileBox, 1);
    hr->addWidget(segments({"原图", "效果", "分屏"}, viewTabs, 1, this));
    v->addWidget(head);
    v->addWidget(rule());
    canvas = new Canvas;
    v->addWidget(canvas, 1);
    connect(viewTabs, &QButtonGroup::idClicked, canvas, &Canvas::setMode);
    auto *info = new QWidget;
    info->setObjectName("foot");
    info->setFixedHeight(36);
    auto *ir = horizontal(info, 0, 7);
    ir->setContentsMargins(16, 0, 13, 0);
    auto *dot = label("●");
    dot->setStyleSheet("color:#c5df84; font-size:9px");
    ir->addWidget(dot);
    currentLabel = label({}, "subtle");
    ir->addWidget(currentLabel);
    ir->addStretch();
    zoomButton = button("适应画布", "fit", "quiet");
    zoomButton->setToolTip("选择预览缩放比例");
    ir->addWidget(zoomButton);
    connect(zoomButton, &QPushButton::clicked, this, [this] {
        QMenu m(this);
        for (double x : {0., .5, 1., 2., 4.}) {
            QString t = x == 0 ? "适应画布" : QString::number(qRound(100 * x)) + "%";
            auto *a = m.addAction(t);
            a->setCheckable(true);
            a->setChecked(qFuzzyCompare(x + 1, canvas->zoom() + 1));
            connect(a, &QAction::triggered, canvas, [this, x] { canvas->setZoom(x); });
        }
        m.exec(zoomButton->mapToGlobal(QPoint(0, zoomButton->height())));
    });
    connect(canvas, &Canvas::zoomChanged, this, [this](double v) {
        zoomButton->setText(v > 0 ? QString::number(qRound(v * 100)) + "%" : "适应画布");
    });
    v->addWidget(info);
    v->addWidget(rule());
    auto *strip = new QWidget;
    strip->setObjectName("strip");
    auto *sl = vertical(strip, 0, 7);
    sl->setContentsMargins(15, 7, 15, 12);
    auto *top = new QWidget;
    auto *tr = horizontal(top, 0, 7);
    auto *icon = label({});
    icon->setPixmap(glyph("grid").pixmap(14, 14));
    tr->addWidget(icon);
    tr->addWidget(label("样片台"));
    tr->addStretch();
    auto *all = button("原站样片库  ›", {}, "quiet");
    tr->addWidget(all);
    connect(all, &QPushButton::clicked, this, &Editor::gallery);
    sl->addWidget(top);
    auto *film = new QWidget;
    auto *filmRow = horizontal(film, 0, 7);
    for (int i = 0; i < samples.size(); ++i) {
        auto *tile =
            new SampleTile(sampleNames[i], samples[i].scaled(340, 155, Qt::KeepAspectRatioByExpanding,
                                                             Qt::SmoothTransformation));
        tiles << tile;
        filmRow->addWidget(tile, 1);
        connect(tile, &QAbstractButton::clicked, this,
                [this, i] { setSource(samples[i], sampleNames[i], i); });
    }
    sl->addWidget(film);
    v->addWidget(strip);
    return w;
}
QWidget *Editor::makeFooter() {
    auto *w = new QWidget;
    w->setObjectName("foot");
    w->setFixedHeight(27);
    auto *r = horizontal(w, 0, 12);
    r->setContentsMargins(16, 0, 16, 0);
    r->addWidget(label("本地处理", "subtle"));
    auto *sep = rule(true);
    sep->setFixedHeight(12);
    r->addWidget(sep);
    status = label("就绪", "subtle");
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    r->addWidget(status, 1);
    outputSize = label({}, "mono");
    r->addWidget(outputSize);
    r->addWidget(label("RGB / 8 bit", "mono"));
    return w;
}

void Editor::makeMenus() {
    mainMenu = new QMenu(this);
    mainMenu->addAction("从剪贴板粘贴图像", this, &Editor::paste);
    mainMenu->addAction("电子包浆", this, &Editor::openPatina);
    mainMenu->addAction("复制效果图", this, &Editor::copyImage);
    mainMenu->addSeparator();
    mainMenu->addAction("导入预设文件…", this, &Editor::importPreset);
    mainMenu->addAction("导出当前预设…", this, &Editor::exportPreset);
    mainMenu->addAction("分享公共预设…", this, [this] { submit(false); });
    mainMenu->addAction("向原站投稿样片…", this, [this] { submit(true); });
    mainMenu->addSeparator();
    mainMenu->addAction("使用说明与快捷键", this, [this] { showHelp(this); });
    connect(moreButton, &QPushButton::clicked, this,
            [this] { mainMenu->exec(moreButton->mapToGlobal(QPoint(0, moreButton->height()))); });
    auto shortcut = [this](QKeySequence key, auto fn) {
        auto *s = new QShortcut(key, this);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Open, [this] { openImage(); });
    shortcut(QKeySequence::Save, [this] { exportImage(); });
    shortcut(QKeySequence::Paste, [this] { paste(); });
    shortcut(QKeySequence::Undo, [this] { undo(false); });
    shortcut(QKeySequence("Ctrl+Y"), [this] { undo(true); });
    shortcut(QKeySequence("Ctrl+Shift+Z"), [this] { undo(true); });
    shortcut(QKeySequence("F1"), [this] { showHelp(this); });
    for (int i = 0; i < 3; ++i)
        shortcut(QKeySequence(QString::number(i + 1)), [this, i] { viewTabs->button(i)->click(); });
    shortcut(QKeySequence("F"), [this] { canvas->setZoom(0); });
}
void Editor::libraryMenu(QWidget *anchor) {
    QMenu m(this);
    m.addAction("保存当前预设…", this, &Editor::savePersonal);
    m.addAction("导入预设文件…", this, &Editor::importPreset);
    m.addAction("导出当前预设…", this, &Editor::exportPreset);
    if (libraryIndex == 1) {
        auto *a = m.addAction("删除选中的个人预设…", this, &Editor::removePersonal);
        a->setEnabled(presetList->currentItem());
    }
    m.addSeparator();
    m.addAction("刷新公共预设", this, &Editor::refreshPublic);
    m.addAction("分享公共预设…", this, [this] { submit(false); });
    m.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}
void Editor::showLibrary(int index) {
    libraryIndex = index;
    rebuildLibrary();
    if (index == 2 && !checkedPublic) {
        checkedPublic = true;
        syncPublic(false);
    }
}
void Editor::rebuildLibrary() {
    if (!presetList)
        return;
    QSignalBlocker block(presetList);
    presetList->clear();
    const auto &list = libraryIndex == 0 ? factory : libraryIndex == 1 ? personal : publicStyles;
    const QString query = search->text().trimmed();
    for (int i = 0; i < list.size(); ++i) {
        const auto &s = list[i];
        const QString name = s["name"].toString();
        QString sub = libraryIndex == 0   ? factoryNames.value(i, "原作预设")
                      : libraryIndex == 1 ? "个人收藏"
                                          : "社区预设";
        if (!name.contains(query, Qt::CaseInsensitive) && !sub.contains(query, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(name, presetList);
        item->setData(Qt::UserRole, s);
        item->setData(Qt::UserRole + 1, sub);
        const auto key = QJsonDocument(s).toJson(QJsonDocument::Compact);
        if (!previews.contains(key)) {
            const Frame thumb =
                renderFrame(thumbSource, s, s["watermark"].toBool() ? watermarkImage(s) : QImage(), 112);
            previews.insert(key, QPixmap::fromImage(thumb.processed));
        }
        item->setData(Qt::DecorationRole, previews.value(key));
    }
    libraryCount->setText(QString::number(presetList->count()));
    emptyLibrary->setText(query.isEmpty() ? "保存当前调色后，可在这里找到个人预设。" : "没有匹配的预设");
    emptyLibrary->setVisible(presetList->count() == 0);
    presetList->setVisible(presetList->count() > 0);
    syncSelection();
}
void Editor::syncSelection() {
    QSignalBlocker block(presetList);
    int selected = -1;
    for (int i = 0; i < presetList->count(); ++i)
        if (presetList->item(i)->data(Qt::UserRole).toJsonObject() == current.base) {
            selected = i;
            break;
        }
    presetList->setCurrentRow(selected);
}
void Editor::remember(const EditState &old) {
    if (old == current)
        return;
    past << old;
    if (past.size() > 80)
        past.removeFirst();
    future.clear();
}
void Editor::applyPreset(const QJsonObject &preset) {
    EditState old = current;
    current = {preset, preset};
    if (old == current)
        return;
    remember(old);
    updateControls();
    syncSelection();
    requestRender();
}
void Editor::change(const QString &key, const QJsonValue &value) {
    EditState old = current;
    if (value.isUndefined())
        current.style.remove(key);
    else
        current.style[key] = value;
    if (current == old)
        return;
    if (!gesturing)
        remember(old);
    updateControls();
    requestRender();
}
void Editor::undo(bool forward) {
    auto &from = forward ? future : past;
    auto &to = forward ? past : future;
    if (from.isEmpty() || gesturing)
        return;
    to << current;
    current = from.takeLast();
    updateControls();
    syncSelection();
    requestRender();
}
void Editor::updateControls() {
    inspector->sync(current.style);
    undoButton->setEnabled(!past.isEmpty());
    redoButton->setEnabled(!future.isEmpty());
    currentLabel->setText("预设  /  " + current.style["name"].toString() +
                          (current.style == current.base ? "" : "  ·  已调整"));
}
void Editor::setSource(QImage image, QString title, int localIndex) {
    setSource({std::move(image), std::move(title), {}, {}}, localIndex);
}
void Editor::openPatina() {
    auto *dialog = new PatinaDialog(document, ready() ? frame.processed : QImage{}, patinaOptions, workshopOptions, this);
    connect(this, &Editor::frameReady, dialog, &PatinaDialog::setStudioEffect);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this, dialog](int code) {
        patinaOptions = dialog->settings();
        workshopOptions = dialog->workshopSettings();
        if (code == QDialog::Accepted)
            setSource(dialog->appliedImage());
    });
    dialog->open();
}
void Editor::setSource(PatinaInput input, int localIndex) {
    if (input.image.isNull())
        return;
    ++sourceEpoch;
    document = std::move(input);
    docLabel->setText(document.name);
    docLabel->setToolTip(document.name);
    sourceSize->setText(dimensions(document.image.size()) + " px");
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setChecked(i == localIndex);
    canvas->resetView();
    requestRender();
}
void Editor::requestRender() {
    ++revision;
    if (canceled)
        canceled->store(true);
    exportButton->setEnabled(false);
    noticeTimer.stop();
    status->setText("正在处理…");
    debounce.start();
}
void Editor::startRender() {
    if (rendering || document.image.isNull())
        return;
    rendering = true;
    runningRevision = revision;
    canceled = std::make_shared<std::atomic_bool>(false);
    auto flag = canceled;
    auto image = document.image;
    auto style = current.style;
    QImage logo = style["watermark"].toBool() ? watermarkImage(style) : QImage();
    worker.setFuture(
        QtConcurrent::run([image, style, logo, flag] { return renderFrame(image, style, logo, 0, flag); }));
}
void Editor::finishRender() {
    rendering = false;
    if (runningRevision != revision) {
        if (!debounce.isActive())
            startRender();
        return;
    }
    Frame result = worker.result();
    if (!result.error.isEmpty() || result.processed.isNull()) {
        frame = {};
        canvas->showFrame(frame);
        status->setText(result.error);
        return;
    }
    rendered = revision;
    frame = std::move(result);
    emit frameReady(frame.processed);
    canvas->showFrame(frame);
    inspector->setHistogram(frame.processed);
    exportButton->setEnabled(true);
    outputSize->setText("OUT  " + dimensions(frame.processed.size()) + " px");
    status->setText("就绪");
}
void Editor::message(const QString &text) {
    status->setText(text);
    status->setToolTip(text);
    noticeTimer.start(6500);
}
void Editor::warn(const QString &title, const QString &text) {
    QMessageBox::warning(this, title, text);
}
