#include "workshopcontrols.h"
#include "widgets.h"
#include "maskeditor.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

QWidget *workshopControls(WorkshopEffect effect, WorkshopSettings &s, std::function<void()> changed,
                          std::function<const PatinaInput &()> source,
                          std::function<void()> previewChanged) {
    auto *panel = new QWidget;
    auto *layout = vertical(panel, 15, 12);
    QList<std::function<void()>> resetters;
    const WorkshopSettings defaults;
    auto hint = [&](const QString &text) {
        auto *l = label(text, "subtle");
        l->setWordWrap(true);
        layout->addWidget(l);
    };
    auto number = [&](const QString &title, int &field, int def, int low, int high, QString unit = {},
                      std::function<void()> notify = {}) {
        if (!notify)
            notify = changed;
        auto *line = new QWidget;
        auto *r = horizontal(line);
        r->addWidget(label(title));
        r->addStretch();
        auto *spin = new QSpinBox;
        spin->setRange(low, high);
        spin->setSuffix(unit);
        spin->setKeyboardTracking(false);
        spin->setAccessibleName(title);
        spin->setValue(field);
        r->addWidget(spin);
        layout->addWidget(line);
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setFixedHeight(16);
        slider->setAccessibleName(title);
        slider->setRange(low, high);
        slider->setValue(field);
        layout->addWidget(slider);
        QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        QObject::connect(spin, &QSpinBox::valueChanged, panel, [&field, slider, notify](int v) {
            field = v;
            QSignalBlocker block(slider);
            slider->setValue(v);
            notify();
        });
        resetters.append([spin, def] { spin->setValue(def); });
    };
    auto choice = [&](const QString &title, const QStringList &items, int &field, int def) {
        layout->addWidget(label(title));
        auto *menu = new QComboBox;
        menu->setAccessibleName(title);
        menu->addItems(items);
        menu->setCurrentIndex(field);
        layout->addWidget(menu);
        QObject::connect(menu, &QComboBox::currentIndexChanged, panel, [&field, changed](int index) {
            field = index;
            changed();
        });
        resetters.append([menu, def] { menu->setCurrentIndex(def); });
    };
    auto ink = [&](const QString &title, QColor &field, QColor def) {
        auto *line = new QWidget;
        auto *r = horizontal(line);
        r->addWidget(label(title));
        r->addStretch();
        auto *pick = button(field.name().toUpper());
        pick->setMinimumWidth(114);
        pick->setAccessibleName(title);
        r->addWidget(pick);
        layout->addWidget(line);
        auto paint = [pick, &field] {
            QPixmap swatch(23, 17);
            swatch.fill(field);
            pick->setIcon(QIcon(swatch));
            pick->setText(field.name().toUpper());
        };
        paint();
        QObject::connect(pick, &QPushButton::clicked, panel, [pick, title, &field, paint, changed] {
            auto c = QColorDialog::getColor(field, pick, title);
            if (c.isValid() && c != field) {
                field = c;
                paint();
                changed();
            }
        });
        resetters.append([&field, def, paint, changed] {
            if (field != def) {
                field = def;
                paint();
                changed();
            }
        });
    };
    switch (effect) {
    case WorkshopEffect::Photocopy:
        number("复印代数", s.copy.passes, defaults.copy.passes, 1, 16);
        number("墨量", s.copy.toner, defaults.copy.toner, 0, 100, " %");
        number("走纸偏斜", s.copy.skew, defaults.copy.skew, 0, 100, " %");
        hint("每一代都重扫上一张复印件，纸边偏移和墨粉颗粒会逐渐累积。");
        break;
    case WorkshopEffect::Duotone:
        ink("第一色墨", s.print.first, defaults.print.first);
        ink("第二色墨", s.print.second, defaults.print.second);
        number("网点间距", s.print.pitch, defaults.print.pitch, 2, 24, " px");
        number("套印偏移", s.print.offset, defaults.print.offset, -24, 24, " px");
        hint("点击色块换墨。两张网版以不同角度叠印，交叠处会形成第三种颜色。");
        break;
    case WorkshopEffect::CRT:
        choice("荧光罩", {"孔栅条纹", "错列点阵", "狭缝荫罩"}, s.crt.mask, defaults.crt.mask);
        number("辉光", s.crt.glow, defaults.crt.glow, 0, 100, " %");
        number("玻璃曲率", s.crt.curvature, defaults.crt.curvature, 0, 100, " %");
        hint("辉光从亮部向周围扩散。双击画布可在 100% 比例下观察荧光点阵。");
        break;
    case WorkshopEffect::Thermal:
        number("纸宽", s.thermal.width, defaults.thermal.width, 160, 832, " px");
        number("打印浓度", s.thermal.heat, defaults.thermal.heat, 0, 100, " %");
        number("缺失打印列", s.thermal.missing, defaults.thermal.missing, 0, 12);
        hint("纸宽决定导出宽度。黑点疏密表现明暗，缺线固定在相同的打印头位置。");
        break;
    case WorkshopEffect::TimeSlice:
        choice("扫描方向", {"从上到下", "从下到上", "从左到右", "从右到左"}, s.slice.direction,
               defaults.slice.direction);
        number("时间跨度", s.slice.spanMs, defaults.slice.spanMs, 0, 2000, " ms");
        hint("从扫描起点到终点，依次取用当前帧到较早的画面。跨度为零时可查看原动画。");
        break;
    case WorkshopEffect::Exposure:
        number("残影时长", s.trail.spanMs, defaults.trail.spanMs, 0, 2000, " ms");
        number("衰减速度", s.trail.decay, defaults.trail.decay, 0, 100, " %");
        hint("保留清晰的当前帧，再按每帧停留时长叠加残影。衰减越快，久远的动作越淡。");
        break;
    case WorkshopEffect::PixelSort:
        number("亮度下限", s.sort.low, defaults.sort.low, 0, 255);
        number("亮度上限", s.sort.high, defaults.sort.high, 0, 255);
        choice("排序方向", {"从左到右", "从右到左", "从上到下", "从下到上"}, s.sort.direction,
               defaults.sort.direction);
        number("最长条带", s.sort.length, defaults.sort.length, 2, 2048, " px");
        hint("只在所选亮度区间内重排像素，遇到区间外的像素就开始下一段。");
        break;
    case WorkshopEffect::DualImage: {
        auto *pick = button("选择黑底图…", "open");
        pick->setAccessibleName("选择黑底图");
        layout->addWidget(pick);
        auto *name = label(s.dual.name, "subtle");
        name->setWordWrap(true);
        layout->addWidget(name);
        QObject::connect(pick, &QPushButton::clicked, panel, [panel, &s, name, changed] {
            QString path = QFileDialog::getOpenFileName(
                panel, "选择黑底显示的第二张图片", QSettings().value("importDir").toString(),
                "图像 (*.jpg *.jpeg *.png *.gif *.webp *.bmp *.tif *.tiff);;所有文件 (*)");
            if (path.isEmpty())
                return;
            QString error;
            auto image = readImage(path, &error);
            if (image.isNull()) {
                QMessageBox::warning(panel, "打开失败", error);
                return;
            }
            s.dual.dark = image;
            s.dual.name = QFileInfo(path).fileName();
            name->setText(s.dual.name);
            changed();
        });
        number("预览背景", s.dual.background, defaults.dual.background, 0, 255, {}, previewChanged);
        hint("白底显示当前输入，黑底显示第二张图。第二张图按比例居中。导出透明灰阶 "
             "PNG，预览背景不会写入文件。");
        break;
    }
    case WorkshopEffect::Cinemagraph: {
        auto *edit = button("绘制运动区域…");
        edit->setAccessibleName("绘制运动区域");
        layout->addWidget(edit);
        QObject::connect(edit, &QPushButton::clicked, panel, [panel, &s, source, changed] {
            if (editMotionMask(panel, source(), s.cinema))
                changed();
        });
        number("边缘羽化", s.cinema.feather, defaults.cinema.feather, 0, 48, " px");
        hint("在蒙版编辑器里选择静止底帧，用画笔涂出要动的区域，橡皮擦可恢复静止。");
        break;
    }
    case WorkshopEffect::Glyphs: {
        layout->addWidget(label("字符集"));
        auto *edit = new QLineEdit(s.glyphs.characters);
        edit->setAccessibleName("字符集");
        edit->setMaxLength(128);
        layout->addWidget(edit);
        QObject::connect(edit, &QLineEdit::textChanged, panel, [&s, changed](const QString &v) {
            s.glyphs.characters = v;
            changed();
        });
        resetters.append([edit, chars = defaults.glyphs.characters] { edit->setText(chars); });
        number("字号", s.glyphs.fontSize, defaults.glyphs.fontSize, 6, 64, " px");
        number("行间距", s.glyphs.leading, defaults.glyphs.leading, 0, 20, " px");
        hint("按字形墨量还原明暗。可输入中文或符号，导出时选择 UTF-8 纯文本并用等宽字体查看。");
        break;
    }
    case WorkshopEffect::Contours:
        number("线条层数", s.contours.levels, defaults.contours.levels, 2, 40);
        number("线宽", s.contours.width, defaults.contours.width, 1, 9, " px");
        ink("线条颜色", s.contours.ink, defaults.contours.ink);
        ink("底色", s.contours.paper, defaults.contours.paper);
        hint("沿相同亮度绘制曲线，亮度均匀的区域自然留白。");
        break;
    case WorkshopEffect::Feedback:
        number("逐层缩放", s.feedback.scale, defaults.feedback.scale, 55, 97, " %");
        number("旋转", s.feedback.rotation, defaults.feedback.rotation, -45, 45, " °");
        number("衰减", s.feedback.decay, defaults.feedback.decay, 0, 100, " %");
        number("输出最长边", s.feedback.edge, defaults.feedback.edge, 160, 1280, " px");
        resetters.append([&s, changed] {
            s.feedback.x = 50;
            s.feedback.y = 50;
            changed();
        });
        hint("在「效果」视图中点击或拖动反馈中心。照片生成 24 帧循环，GIF 使用原始帧时长。");
        break;
    default:
        break;
    }
    layout->addStretch();
    auto *reset = button("恢复本效果默认值", {}, "quiet");
    layout->addWidget(reset);
    QObject::connect(reset, &QPushButton::clicked, panel, [resetters] {
        for (const auto &restore : resetters)
            restore();
    });
    return panel;
}
