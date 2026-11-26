#include "inspector.h"
#include "widgets.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <array>

class Histogram : public QWidget {
  public:
    Histogram() {
        setFixedHeight(58);
        setToolTip("横轴由暗到亮，纵轴表示对应亮度的像素数量。");
    }
    void setImage(const QImage &image) {
        bins.fill(0);
        const auto rgb = image.convertToFormat(QImage::Format_RGB32);
        const int stride = qMax(1, int(std::ceil(std::sqrt(double(rgb.width()) * rgb.height() / 100000.))));
        for (int y = 0; y < rgb.height(); y += stride) {
            const auto *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            for (int x = 0; x < rgb.width(); x += stride) {
                const QRgb p = line[x];
                const int luma = (77 * qRed(p) + 150 * qGreen(p) + 29 * qBlue(p)) >> 8;
                ++bins[luma / 2];
            }
        }
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF box = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        p.setPen(QColor("#383c3a"));
        p.setBrush(QColor("#1b1d1e"));
        p.drawRoundedRect(box, 4, 4);
        for (int i = 1; i < 4; ++i) {
            double x = box.width() * i / 4;
            p.drawLine(QPointF(x, 3), QPointF(x, box.bottom() - 3));
        }
        int peak = *std::max_element(bins.begin(), bins.end());
        if (!peak)
            return;
        const double floor = box.bottom() - 3;
        QPainterPath line;
        line.moveTo(3, floor);
        for (int i = 0; i < 128; ++i)
            line.lineTo(3 + (box.width() - 6) * i / 127.,
                        floor - (box.height() - 8) * double(bins[i]) / peak);
        line.lineTo(box.right() - 3, floor);
        QPainterPath fill = line;
        fill.closeSubpath();
        p.fillPath(fill, QColor(157, 182, 110, 82));
        p.setPen(QPen(QColor("#a3b982"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

  private:
    std::array<int, 128> bins{};
};

class Track : public QSlider {
  public:
    explicit Track(int initial) : QSlider(Qt::Horizontal), initial(initial) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal middle = height() * .5;
        const qreal knob =
            5 + (width() - 10) * double(value() - minimum()) / qMax(1, maximum() - minimum());
        p.setPen(QPen(QColor("#434746"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, middle), QPointF(width() - 5, middle));
        p.setPen(QPen(isEnabled() ? QColor("#a5ba78") : QColor("#61655c"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, middle), QPointF(knob, middle));
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? QColor("#d2e9a1") : QColor("#73796a"));
        p.drawEllipse(QPointF(knob, middle), 4.5, 4.5);
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#d2e9a1"), 1));
            p.drawEllipse(QPointF(knob, middle), 6., 6.);
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        setValue(initial);
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(e);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        setFrom(e->position().x());
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (isSliderDown())
            setFrom(e->position().x());
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            setSliderDown(false);
            e->accept();
        } else
            QSlider::mouseReleaseEvent(e);
    }
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus())
            QSlider::wheelEvent(e);
        else
            e->ignore();
    }

  private:
    int initial;
    void setFrom(double x) {
        setValue(
            QStyle::sliderValueFromPosition(minimum(), maximum(), qRound(x) - 5, qMax(1, width() - 10)));
    }
};

class SectionButton : public QPushButton {
  public:
    explicit SectionButton(const QString &text) : QPushButton(text) {}

  protected:
    void paintEvent(QPaintEvent *e) override {
        QPushButton::paintEvent(e);
        QPainter p(this);
        glyph(isChecked() ? "down" : "next", QColor("#92988f"))
            .paint(&p, QRect(width() - 30, (height() - 14) / 2, 14, 14));
    }
};

Inspector::Inspector(QWidget *parent) : QWidget(parent) {
    setObjectName("rightPanel");
    auto *layout = vertical(this);
    auto *head = new QWidget;
    auto *row = horizontal(head, 0, 6);
    row->setContentsMargins(17, 0, 12, 0);
    head->setFixedHeight(58);
    row->addWidget(label("参数调节", "heading"));
    row->addStretch();
    auto *reset = button({}, "reset", "icon");
    reset->setToolTip("恢复当前预设的全部参数");
    reset->setAccessibleName("恢复预设参数");
    connect(reset, &QPushButton::clicked, this, &Inspector::resetStyle);
    row->addWidget(reset);
    layout->addWidget(head);
    layout->addWidget(rule());
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    sections = vertical(content);
    sections->setAlignment(Qt::AlignTop);
    section("基础影调", "tone", true);
    sliders({"light", "contrast", "darkFade", "brightFade"});
    section("色彩通道", "color");
    sliders({"vividU", "vividV", "shiftU", "shiftV", "level"});
    section("信号与扫描", "wave");
    sliders({"shiftX", "shiftY", "replace", "interlaced", "interlacedLine", "interlacedLight", "transposeX",
             "transposePow", "transposeNoise"});
    section("画质与颗粒", "grain");
    sliders({"zoom", "quality", "lightNoise", "darkNoise"});
    toggle("yuv420", "色度降采样  4:2:0");
    sliders({"yuv420Noise"});
    section("画幅与变形", "frame");
    toggle("sizeOrigin", "按输入图像原始尺寸处理");
    choices("wide", "画幅比例", {"4:3", "16:9", "21:9", "1:1"}, {"4:3", "16:9", "21:9", "1:1"});
    choices("fit", "适配方式", {"填充裁切", "完整显示", "拉伸铺满"}, {"cover", "contain", "fill"});
    sliders({"ratio", "border"});
    section("水印与署名", "stamp");
    toggle("watermark", "显示蒸汽机水印");
    auto *logoRow = new QWidget;
    auto *buttons = horizontal(logoRow, 0, 5);
    auto *pick = button("选择水印");
    auto *restore = button("恢复默认", {}, "quiet");
    buttons->addWidget(pick);
    buttons->addWidget(restore);
    buttons->addStretch();
    group->addWidget(logoRow);
    connect(pick, &QPushButton::clicked, this, &Inspector::chooseLogo);
    connect(restore, &QPushButton::clicked, this, &Inspector::resetLogo);
    bindings.append([logoRow, restore](const QJsonObject &s) {
        logoRow->setEnabled(s["watermark"].toBool());
        restore->setEnabled(s.contains("logoPng"));
    });
    sliders({"watermarkLeft", "watermarkTop", "watermarkAlpha", "watermarkSize"});
    toggle("styleName", "叠加预设名称");
    sliders({"styleLeft", "styleTop", "styleSize"});
    section("线条与反相", "lines");
    toggle("sharpen", "强调线条");
    choices("convoluteName", "线条方案", {"右倾", "左倾", "桑拿", "浮雕"},
            {"右倾", "左倾", "桑拿", "浮雕"});
    toggle("snow", "切断信号（雪花）");
    toggle("invertLight", "颠倒黑白");
    toggle("none", "旁路全部效果");
    sections->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    layout->addWidget(rule());
    auto *scope = new QWidget;
    auto *scopeLayout = vertical(scope, 0, 7);
    scopeLayout->setContentsMargins(16, 11, 16, 12);
    auto *scopeTitle = new QWidget;
    auto *caption = horizontal(scopeTitle);
    caption->addWidget(label("亮度直方图", "subtle"));
    caption->addStretch();
    caption->addWidget(label("0 — 255", "mono"));
    scopeLayout->addWidget(scopeTitle);
    histogram = new Histogram;
    scopeLayout->addWidget(histogram);
    layout->addWidget(scope);
}
void Inspector::section(const QString &name, const QString &icon, bool open) {
    auto *header = new SectionButton("  " + name);
    header->setObjectName("section");
    header->setIcon(glyph(icon, QColor("#b4bcb0")));
    header->setIconSize({17, 17});
    header->setCursor(Qt::PointingHandCursor);
    header->setCheckable(true);
    header->setChecked(open);
    header->setFixedHeight(43);
    auto *body = new QWidget;
    group = vertical(body, 0, 7);
    group->setContentsMargins(18, 2, 18, 14);
    sections->addWidget(header);
    sections->addWidget(body);
    sections->addWidget(rule());
    body->setVisible(open);
    connect(header, &QPushButton::toggled, body, &QWidget::setVisible);
}
void Inspector::sliders(std::initializer_list<const char *> keys) {
    for (auto key : keys) {
        auto it = std::find_if(dials().cbegin(), dials().cend(),
                               [key](const Dial &d) { return qstrcmp(d.key, key) == 0; });
        if (it == dials().cend())
            continue;
        const Dial d = *it;
        auto *entry = new QWidget;
        auto *layout = vertical(entry, 0, 2);
        auto *top = new QWidget;
        auto *row = horizontal(top);
        row->addWidget(label(d.title));
        row->addStretch();
        auto *box = new QDoubleSpinBox;
        box->setObjectName(QString("number_") + key);
        box->setAccessibleName(d.title);
        box->setFont(typeface("Consolas", 12));
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setAlignment(Qt::AlignRight);
        box->setKeyboardTracking(false);
        box->setDecimals(d.step >= 1 ? 0 : d.step >= .1 ? 1 : 2);
        box->setRange(d.minimum, d.maximum);
        box->setSingleStep(d.step);
        box->setFixedWidth(49);
        row->addWidget(box);
        auto *track = new Track(qRound((d.initial - d.minimum) / d.step));
        track->setObjectName(QString("slider_") + key);
        track->setAccessibleName(d.title);
        track->setRange(0, qRound((d.maximum - d.minimum) / d.step));
        track->setPageStep(qMax(1, track->maximum() / 20));
        track->setFixedHeight(14);
        track->setToolTip("拖动滑块，或双击恢复参数默认值");
        layout->addWidget(top);
        layout->addWidget(track);
        group->addWidget(entry);
        connect(track, &QSlider::sliderPressed, this, &Inspector::beginGesture);
        connect(track, &QSlider::sliderReleased, this, &Inspector::endGesture);
        connect(track, &QSlider::valueChanged, this,
                [this, d](int i) { emit edit(d.key, d.minimum + i * d.step); });
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, d](double v) { emit edit(d.key, v); });
        bindings.append([entry, track, box, d](const QJsonObject &s) {
            QSignalBlocker a(track), b(box);
            const double v = s[d.key].toDouble();
            box->setValue(v);
            track->setValue(qRound((v - d.minimum) / d.step));
            entry->setEnabled(enabledFor(d.key, s));
        });
    }
}
void Inspector::toggle(const QString &key, const QString &name) {
    auto *c = new QCheckBox(name);
    c->setObjectName(key);
    group->addWidget(c);
    connect(c, &QCheckBox::toggled, this, [this, key](bool b) { emit edit(key, b); });
    bindings.append([c, key](const QJsonObject &s) {
        QSignalBlocker block(c);
        c->setChecked(s[key].toBool());
    });
}
void Inspector::choices(const QString &key, const QString &name, const QStringList &titles,
                        const QStringList &values) {
    auto *w = new QWidget;
    auto *row = horizontal(w, 0, 4);
    row->addWidget(label(name));
    row->addStretch();
    auto *c = new QComboBox;
    c->setObjectName(key);
    c->setAccessibleName(name);
    for (int i = 0; i < titles.size(); ++i)
        c->addItem(titles[i], values[i]);
    row->addWidget(c);
    group->addWidget(w);
    connect(c, &QComboBox::currentIndexChanged, this,
            [this, key, c](int) { emit edit(key, c->currentData().toString()); });
    bindings.append([w, c, key](const QJsonObject &s) {
        QSignalBlocker block(c);
        c->setCurrentIndex(c->findData(s[key].toString()));
        w->setEnabled(enabledFor(key, s));
    });
}
void Inspector::sync(const QJsonObject &s) {
    for (const auto &bind : bindings)
        bind(s);
}
void Inspector::setHistogram(const QImage &image) {
    histogram->setImage(image);
}
