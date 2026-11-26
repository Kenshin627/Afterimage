#include "maskeditor.h"
#include "creativeeffects.h"
#include "widgets.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
class MaskSurface final : public QWidget {
  public:
    MaskSurface(const QImage &source, const QImage &initial, QWidget *parent)
        : QWidget(parent), frame(source), overlay(source.size(), QImage::Format_ARGB32_Premultiplied) {
        setObjectName("motionMaskCanvas");
        setAccessibleName("运动区域画布");
        setMouseTracking(true);
        setMinimumSize(250, 160);
        setCursor(Qt::CrossCursor);
        if (overlay.isNull())
            return;
        overlay.fill(Qt::transparent);
        if (initial.size() == source.size()) {
            auto mask = initial.convertToFormat(QImage::Format_Grayscale8);
            if (!mask.isNull())
                for (int y = 0; y < mask.height(); ++y) {
                    auto *d = reinterpret_cast<QRgb *>(overlay.scanLine(y));
                    auto *m = mask.constScanLine(y);
                    for (int x = 0; x < mask.width(); ++x)
                        d[x] = qPremultiply(qRgba(148, 235, 104, m[x]));
                }
        }
    }
    bool valid() const { return !overlay.isNull(); }
    void changeFrame(const QImage &image) {
        frame = image;
        update();
    }
    void setBrush(int diameter) {
        brush = diameter;
        update();
    }
    void setErase(bool value) { erase = value; }
    void setTint(bool value) {
        tint = value;
        update();
    }
    void undo() {
        if (!previous.isNull()) {
            overlay.swap(previous);
            update();
        }
    }
    void clear() {
        previous = overlay;
        overlay.fill(Qt::transparent);
        update();
    }
    QImage mask() const {
        QImage m(overlay.size(), QImage::Format_Grayscale8);
        if (!m.isNull())
            for (int y = 0; y < m.height(); ++y) {
                auto *dst = m.scanLine(y);
                const auto *src = reinterpret_cast<const QRgb *>(overlay.constScanLine(y));
                for (int x = 0; x < m.width(); ++x)
                    dst[x] = uchar(qAlpha(src[x]));
            }
        return m;
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#141617"));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        auto box = placement();
        p.drawImage(box, frame);
        if (tint) {
            p.setOpacity(.48);
            p.drawImage(box, overlay);
            p.setOpacity(1.);
        }
        if (underMouse() && box.contains(cursorPos)) {
            auto r = brush * .5 * box.width() / frame.width();
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 3));
            p.drawEllipse(cursorPos, r, r);
            p.setPen(QPen(Qt::white, 1));
            p.drawEllipse(cursorPos, r, r);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        cursorPos = e->position();
        if (e->button() != Qt::LeftButton || !placement().contains(cursorPos))
            return;
        previous = overlay;
        painting = true;
        last = coordinate(cursorPos);
        stroke(last, last);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        cursorPos = e->position();
        if (painting) {
            QPointF now = coordinate(cursorPos);
            stroke(last, now);
            last = now;
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (painting && e->button() == Qt::LeftButton) {
            stroke(last, coordinate(e->position()));
            painting = false;
        }
    }
    void leaveEvent(QEvent *) override { update(); }

  private:
    QRectF placement() const {
        auto bounds = rect().adjusted(8, 8, -8, -8);
        QRectF box(QPointF(), QSizeF(frame.size()).scaled(bounds.size(), Qt::KeepAspectRatio));
        box.moveCenter(QRectF(bounds).center());
        return box;
    }
    QPointF coordinate(QPointF pos) const {
        auto rect = placement();
        return {(pos.x() - rect.left()) * frame.width() / rect.width(),
                (pos.y() - rect.top()) * frame.height() / rect.height()};
    }
    void stroke(QPointF a, QPointF b) {
        QPainter p(&overlay);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        QColor color = erase ? QColor(Qt::transparent) : QColor(148, 235, 104);
        p.setPen(QPen(color, brush, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (a == b) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(a, brush * .5, brush * .5);
        } else
            p.drawLine(a, b);
        update();
    }
    QImage frame, overlay, previous;
    QPointF last, cursorPos;
    bool painting = false, erase = false, tint = true;
    int brush = 32;
};
} // namespace

bool editMotionMask(QWidget *parent, const PatinaInput &doc, WorkshopSettings::Cinemagraph &s) {
    if (!doc.animated()) {
        QMessageBox::information(parent, "局部动图", "先打开一张多帧 GIF，再绘制运动区域。");
        return false;
    }
    bool same = s.sourceKey == doc.image.cacheKey() && s.mask.size() == doc.image.size();
    QDialog dlg(parent);
    dlg.setWindowTitle("绘制运动区域");
    dlg.resize(800, 590);
    auto *v = vertical(&dlg, 14, 8);
    auto *toolbar = new QWidget;
    auto *r = horizontal(toolbar, 0, 7);
    v->addWidget(toolbar);
    r->addWidget(label("静止底帧"));
    auto *frame = new QSpinBox;
    frame->setRange(1, int(doc.timing.delays.size()));
    frame->setKeyboardTracking(false);
    frame->setValue(same ? s.base + 1 : 1);
    frame->setAccessibleName("静止底帧");
    r->addWidget(frame);
    auto *tool = new QComboBox;
    tool->addItems({"画笔", "橡皮擦"});
    r->addWidget(tool);
    r->addWidget(label("笔径"));
    auto *width = new QSpinBox;
    width->setRange(1, 800);
    width->setSuffix(" px");
    width->setValue(std::clamp(doc.image.width() / 14, 4, 120));
    width->setAccessibleName("笔径");
    r->addWidget(width);
    r->addStretch();
    auto *undo = button("撤销");
    auto *clear = button("清空");
    r->addWidget(undo);
    r->addWidget(clear);
    auto *surface = new MaskSurface(doc.image, same ? s.mask : QImage{}, &dlg);
    if (!surface->valid()) {
        QMessageBox::warning(parent, "无法创建蒙版", "可用内存不足。");
        return false;
    }
    surface->setBrush(width->value());
    v->addWidget(surface, 1);
    auto *foot = new QWidget;
    auto *f = horizontal(foot, 0, 8);
    auto *show = new QCheckBox("显示运动区域");
    show->setChecked(true);
    f->addWidget(show);
    auto *status = label("涂绿的区域播放，其余区域停在所选底帧。", "subtle");
    status->setWordWrap(true);
    f->addWidget(status, 1);
    auto *back = button("取消");
    auto *done = button("应用蒙版", {}, "primary");
    f->addWidget(back);
    f->addWidget(done);
    v->addWidget(foot);
    QObject::connect(back, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(done, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(undo, &QPushButton::clicked, surface, &MaskSurface::undo);
    QObject::connect(clear, &QPushButton::clicked, surface, &MaskSurface::clear);
    QObject::connect(new QShortcut(QKeySequence::Undo, &dlg), &QShortcut::activated, surface,
                     &MaskSurface::undo);
    QObject::connect(show, &QCheckBox::toggled, surface, &MaskSurface::setTint);
    QObject::connect(width, &QSpinBox::valueChanged, surface, &MaskSurface::setBrush);
    QObject::connect(tool, &QComboBox::currentIndexChanged, surface,
                     [surface](int i) { surface->setErase(i == 1); });
    CancelFlag reading;
    auto load = [&](int number) {
        if (reading)
            reading->store(true);
        done->setEnabled(false);
        status->setText("正在读取底帧…");
        reading = std::make_shared<std::atomic_bool>(false);
        auto flag = reading;
        auto *task = new QFutureWatcher<QImage>(&dlg);
        QObject::connect(task, &QFutureWatcher<QImage>::finished, &dlg, [=] {
            auto image = task->result();
            task->deleteLater();
            if (flag->load())
                return;
            if (image.isNull()) {
                status->setText("读取失败，请选择其他帧。");
                return;
            }
            surface->changeFrame(image);
            done->setEnabled(true);
            status->setText("涂绿的区域播放，其余区域停在所选底帧。");
        });
        task->setFuture(
            QtConcurrent::run([doc, number, flag] { return readWorkshopFrame(doc, number - 1, flag); }));
    };
    QObject::connect(frame, &QSpinBox::valueChanged, &dlg, load);
    if (frame->value() > 1)
        load(frame->value());
    auto accepted = dlg.exec() == QDialog::Accepted;
    if (reading)
        reading->store(true);
    if (accepted) {
        auto mask = surface->mask();
        if (mask.isNull())
            return false;
        s.mask = std::move(mask);
        s.base = frame->value() - 1;
        s.sourceKey = doc.image.cacheKey();
    }
    return accepted;
}
