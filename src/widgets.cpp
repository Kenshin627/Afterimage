#include "widgets.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

QFont typeface(const QString &family, int pixels, QFont::Weight weight) {
    QFont f(family);
    f.setPixelSize(pixels);
    f.setWeight(weight);
    return f;
}
QIcon glyph(const QString &key, QColor color) {
    QPixmap pix(48, 48);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    auto path = [&](std::initializer_list<QPointF> points) {
        QPolygonF polygon;
        for (auto a : points)
            polygon << a;
        p.drawPolyline(polygon);
    };
    auto line = [&](int a, int b, int c, int d) { p.drawLine(a, b, c, d); };
    if (key == "open") {
        path({{3, 18}, {3, 6}, {9, 6}, {11, 8}, {20, 8}, {20, 10}});
        path({{3, 18}, {6, 10}, {22, 10}, {19, 18}, {3, 18}});
    } else if (key == "export") {
        line(12, 3, 12, 14);
        path({{8, 10}, {12, 14}, {16, 10}});
        path({{4, 15}, {4, 20}, {20, 20}, {20, 15}});
    } else if (key == "undo" || key == "redo") {
        if (key == "redo") {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        path({{8, 5}, {3, 10}, {8, 15}});
        QPainterPath c;
        c.moveTo(4, 10);
        c.lineTo(14, 10);
        c.quadTo(20, 10, 20, 17);
        p.drawPath(c);
    } else if (key == "search") {
        p.drawEllipse(QRectF(4, 4, 11, 11));
        line(14, 14, 20, 20);
    } else if (key == "more") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int x : {5, 12, 19})
            p.drawEllipse(QPointF(x, 12), 1.5, 1.5);
    } else if (key == "plus") {
        line(12, 5, 12, 19);
        line(5, 12, 19, 12);
    } else if (key == "down") {
        path({{7, 10}, {12, 15}, {17, 10}});
    } else if (key == "next") {
        path({{9, 6}, {15, 12}, {9, 18}});
    } else if (key == "reset") {
        p.drawArc(QRectF(5, 5, 14, 14), -70 * 16, 288 * 16);
        path({{3, 5}, {4, 10}, {9, 8}});
    } else if (key == "image") {
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        p.drawEllipse(QPointF(8, 9), 1.4, 1.4);
        path({{4, 18}, {10, 13}, {13, 16}, {16, 11}, {20, 15}});
    } else if (key == "sliders") {
        line(3, 7, 9, 7);
        line(13, 7, 21, 7);
        line(3, 17, 13, 17);
        line(17, 17, 21, 17);
        p.drawEllipse(QPointF(11, 7), 2, 2);
        p.drawEllipse(QPointF(15, 17), 2, 2);
    } else if (key == "tone") {
        p.drawEllipse(QRectF(5, 5, 14, 14));
        p.setBrush(color);
        p.drawPie(QRectF(5, 5, 14, 14), 90 * 16, 180 * 16);
    } else if (key == "color") {
        p.drawEllipse(QPointF(9, 11), 5, 5);
        p.drawEllipse(QPointF(15, 13), 5, 5);
    } else if (key == "wave") {
        path({{2, 13}, {6, 13}, {8, 6}, {11, 18}, {15, 8}, {17, 13}, {22, 13}});
    } else if (key == "grain") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int y : {6, 12, 18})
            for (int x : {6, 12, 18})
                p.drawEllipse(QPointF(x, y), 1.4, 1.4);
    } else if (key == "frame") {
        path({{4, 10}, {4, 5}, {9, 5}});
        path({{15, 5}, {20, 5}, {20, 10}});
        path({{20, 14}, {20, 19}, {15, 19}});
        path({{9, 19}, {4, 19}, {4, 14}});
    } else if (key == "stamp") {
        path({{5, 20}, {19, 20}});
        path({{4, 16},
              {20, 16},
              {18, 12},
              {14, 12},
              {15, 7},
              {14, 4},
              {10, 4},
              {9, 7},
              {10, 12},
              {6, 12},
              {4, 16}});
    } else if (key == "lines") {
        line(4, 7, 20, 7);
        line(7, 12, 18, 12);
        line(4, 17, 15, 17);
    } else if (key == "grid") {
        for (int x : {4, 13})
            for (int y : {4, 13})
                p.drawRoundedRect(QRectF(x, y, 6, 6), 1, 1);
    } else if (key == "fit") {
        p.drawRect(QRectF(3, 6, 18, 12));
    }
    pix.setDevicePixelRatio(2);
    return QIcon(pix);
}
QPushButton *button(const QString &text, const QString &icon, const QString &kind) {
    auto *b = new QPushButton(text);
    b->setObjectName(kind);
    b->setCursor(Qt::PointingHandCursor);
    if (!icon.isEmpty())
        b->setIcon(glyph(icon, kind == "primary" ? QColor("#1c2515") : QColor("#c3c8bd")));
    b->setIconSize({17, 17});
    if (text.isEmpty())
        b->setFixedSize(32, 32);
    return b;
}
QLabel *label(const QString &text, const QString &kind) {
    auto *l = new QLabel(text);
    l->setObjectName(kind);
    if (kind == "count")
        l->setFixedHeight(20);
    return l;
}
QHBoxLayout *horizontal(QWidget *host, int margin, int spacing) {
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(margin, margin, margin, margin);
    l->setSpacing(spacing);
    return l;
}
QVBoxLayout *vertical(QWidget *host, int margin, int spacing) {
    auto *l = new QVBoxLayout(host);
    l->setContentsMargins(margin, margin, margin, margin);
    l->setSpacing(spacing);
    return l;
}
QWidget *rule(bool v) {
    auto *r = new QWidget;
    r->setObjectName("rule");
    if (v)
        r->setFixedWidth(1);
    else
        r->setFixedHeight(1);
    return r;
}
void applyTheme() {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#202224"));
    pal.setColor(QPalette::WindowText, QColor("#eff0e9"));
    pal.setColor(QPalette::Base, QColor("#17191a"));
    pal.setColor(QPalette::AlternateBase, QColor("#222526"));
    pal.setColor(QPalette::Button, QColor("#292c2d"));
    pal.setColor(QPalette::ButtonText, QColor("#e0e3d9"));
    pal.setColor(QPalette::Text, QColor("#eff0e9"));
    pal.setColor(QPalette::Highlight, QColor("#cce982"));
    pal.setColor(QPalette::HighlightedText, QColor("#192111"));
    pal.setColor(QPalette::PlaceholderText, QColor("#8a8e90"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#696e70"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#696e70"));
    qApp->setPalette(pal);
    QFontDatabase::addApplicationFont(":/BarlowCondensed-SemiBold.ttf");
    qApp->setFont(typeface("Microsoft YaHei UI", 13));
    QFile css(":/theme.qss");
    if (css.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(css.readAll()));
}

Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(120, 120);
    setAccessibleName("图像预览画布");
}
void Canvas::showFrame(const Frame &f) {
    before = f.original;
    after = f.processed;
    limitPan();
    update();
}
void Canvas::setMode(int value) {
    mode = value;
    update();
}
void Canvas::setPreviewBackground(int gray) {
    previewBackground = gray;
    update();
}
void Canvas::setFeedbackAnchor(bool enabled, QPointF normalized) {
    anchorEnabled = enabled;
    anchor = normalized;
    draggingAnchor = false;
    update();
}
void Canvas::moveAnchor(QPointF position) {
    const auto box = picture();
    if (box.isEmpty())
        return;
    anchor = {std::clamp((position.x() - box.left()) / box.width(), 0., 1.),
              std::clamp((position.y() - box.top()) / box.height(), 0., 1.)};
    update();
}
void Canvas::setZoom(double value) {
    zoomValue = value;
    pan = {};
    limitPan();
    update();
    emit zoomChanged(value);
}
void Canvas::resetView() {
    split = .5;
    setZoom(0);
}
QRectF Canvas::picture() const {
    if (after.isNull())
        return {};
    const double fit = std::min(double(std::max(1, width() - 48)) / after.width(),
                                double(std::max(1, height() - 48)) / after.height());
    const double factor = zoomValue > 0 ? zoomValue / devicePixelRatioF() : fit;
    QRectF dst(0, 0, after.width() * factor, after.height() * factor);
    dst.moveCenter(QRectF(rect()).center() + pan);
    return dst;
}
void Canvas::limitPan() {
    const auto dst = picture();
    const double x = std::max(0., (dst.width() - width() + 44) / 2),
                 y = std::max(0., (dst.height() - height() + 44) / 2);
    pan.setX(std::clamp(pan.x(), -x, x));
    pan.setY(std::clamp(pan.y(), -y, y));
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#141617"));
    p.setPen(QColor("#292c2c"));
    for (int y = 12; y < height(); y += 16)
        for (int x = 12; x < width(); x += 16)
            p.drawPoint(x, y);
    const QRectF dst = picture();
    if (dst.isEmpty())
        return;
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoomValue < 2);
    QRectF originalRect(QPointF(), QSizeF(before.size()).scaled(dst.size(), Qt::KeepAspectRatio));
    originalRect.moveCenter(dst.center());
    if (mode == 0)
        p.drawImage(originalRect, before);
    else {
        if (previewBackground >= 0)
            p.fillRect(dst, QColor(previewBackground, previewBackground, previewBackground));
        p.drawImage(dst, after);
    }
    const double bar = dst.left() + dst.width() * split;
    if (mode == 2) {
        p.save();
        p.setClipRect(QRectF(dst.topLeft(), QSizeF(dst.width() * split, dst.height())));
        p.fillRect(dst, QColor("#141617"));
        p.drawImage(originalRect, before);
        p.restore();
        p.setPen(QPen(QColor("#eff6da"), 1.5));
        p.drawLine(QPointF(bar, dst.top()), QPointF(bar, dst.bottom()));
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#d4ed94"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(bar - 11, dst.center().y() - 13, 22, 26), 7, 7);
        p.setPen(QPen(QColor("#37402a"), 1.5));
        for (int dx : {-2, 2})
            p.drawLine(QPointF(bar + dx, dst.center().y() - 4), QPointF(bar + dx, dst.center().y() + 4));
        p.setFont(typeface("Microsoft YaHei UI", 11, QFont::DemiBold));
        auto tag = [&](double x, QString text) {
            QRectF a(x, dst.top() + 9, 36, 21);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(19, 21, 20, 210));
            p.drawRoundedRect(a, 4, 4);
            p.setPen(Qt::white);
            p.drawText(a, Qt::AlignCenter, text);
        };
        if (bar - dst.left() > 55)
            tag(dst.left() + 9, "原图");
        if (dst.right() - bar > 55)
            tag(dst.right() - 45, "效果");
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    if (anchorEnabled && mode == 1 && (underMouse() || draggingAnchor)) {
        const QPointF at(dst.left() + anchor.x() * dst.width(), dst.top() + anchor.y() * dst.height());
        for (auto pen : {QPen(Qt::black, 3), QPen(Qt::white, 1)}) {
            p.setPen(pen);
            p.drawLine(at - QPointF(8, 0), at + QPointF(8, 0));
            p.drawLine(at - QPointF(0, 8), at + QPointF(0, 8));
        }
    }
    p.setPen(QPen(QColor("#666d65"), 1));
    const QRectF ext = dst.adjusted(-5, -5, 5, 5);
    for (int h : {0, 1})
        for (int v : {0, 1}) {
            QPointF pos(h ? ext.right() : ext.left(), v ? ext.bottom() : ext.top());
            p.drawLine(pos, pos + QPointF(h ? -7 : 7, 0));
            p.drawLine(pos, pos + QPointF(0, v ? -7 : 7));
        }
}
void Canvas::moveSplit(qreal x) {
    auto d = picture();
    if (d.width() > 0)
        split = std::clamp((x - d.left()) / d.width(), 0., 1.);
    update();
}
void Canvas::mousePressEvent(QMouseEvent *e) {
    last = e->position();
    auto d = picture();
    if (e->button() == Qt::LeftButton && mode == 1 && anchorEnabled && d.contains(last)) {
        draggingAnchor = true;
        moveAnchor(last);
        setCursor(Qt::CrossCursor);
    } else if (e->button() == Qt::LeftButton && mode == 2 && d.contains(last)) {
        draggingSplit = true;
        moveSplit(last.x());
        setCursor(Qt::SplitHCursor);
    } else if (zoomValue > 0) {
        draggingPan = true;
        setCursor(Qt::ClosedHandCursor);
    }
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (draggingAnchor)
        moveAnchor(e->position());
    else if (draggingSplit)
        moveSplit(e->position().x());
    else if (draggingPan) {
        pan += e->position() - last;
        limitPan();
        update();
    } else
        setCursor(mode == 2                    ? Qt::SplitHCursor
                  : anchorEnabled && mode == 1 ? Qt::CrossCursor
                                               : (zoomValue > 0 ? Qt::OpenHandCursor : Qt::ArrowCursor));
    last = e->position();
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (draggingAnchor) {
        moveAnchor(event->position());
        draggingAnchor = false;
        emit anchorPlaced(anchor);
    }
    draggingSplit = false;
    draggingPan = false;
    unsetCursor();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *) { setZoom(zoomValue == 0 ? 1 : 0); }
void Canvas::wheelEvent(QWheelEvent *e) {
    if (after.isNull() || !e->angleDelta().y())
        return;
    double curr = zoomValue > 0 ? zoomValue : picture().width() / after.width() * devicePixelRatioF();
    setZoom(std::clamp(curr * (e->angleDelta().y() > 0 ? 1.25 : .8), .1, 8.));
    e->accept();
}

static void cropped(QPainter &p, QRectF box, const QPixmap &pix, double radius) {
    if (pix.isNull())
        return;
    QSizeF cut = pix.size();
    const double ar = box.width() / box.height();
    if (cut.width() / cut.height() > ar)
        cut.setWidth(cut.height() * ar);
    else
        cut.setHeight(cut.width() / ar);
    QRectF src(QPointF(0, 0), cut);
    src.moveCenter(QRectF(pix.rect()).center());
    p.save();
    QPainterPath mask;
    mask.addRoundedRect(box, radius, radius);
    p.setClipPath(mask);
    p.drawPixmap(box, pix, src);
    p.restore();
}
void PresetDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const {
    p->save();
    p->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRectF box = QRectF(opt.rect).adjusted(1, 2, -2, -2);
    bool active = opt.state.testFlag(QStyle::State_Selected);
    if (active || opt.state.testFlag(QStyle::State_MouseOver)) {
        p->setPen(active ? QPen(QColor("#65764a"), 1) : QPen(Qt::NoPen));
        p->setBrush(active ? QColor("#353d2d") : QColor("#2a2d2d"));
        p->drawRoundedRect(box, 6, 6);
    }
    QRectF cover(box.left() + 7, box.top() + 7, 53, box.height() - 14);
    cropped(*p, cover, index.data(Qt::DecorationRole).value<QPixmap>(), 3);
    const int left = int(cover.right() + 11), avail = std::max(10, int(box.right() - left - 12));
    p->setFont(typeface("Microsoft YaHei UI", 13, QFont::DemiBold));
    p->setPen(active ? QColor("#e1efc9") : QColor("#e0e2dc"));
    p->drawText(QRect(left, int(box.top() + 6), avail, 21), Qt::AlignVCenter,
                p->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, avail));
    p->setFont(typeface("Segoe UI", 11));
    p->setPen(active ? QColor("#b8c7a3") : QColor("#929996"));
    p->drawText(
        QRect(left, int(box.top() + 27), avail, 16), Qt::AlignVCenter,
        p->fontMetrics().elidedText(index.data(Qt::UserRole + 1).toString(), Qt::ElideRight, avail));
    if (active) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#d2ec91"));
        p->drawEllipse(QPointF(box.right() - 9, box.center().y()), 2, 2);
    }
    p->restore();
}
SampleTile::SampleTile(QString name, QImage img, QWidget *parent)
    : QAbstractButton(parent), thumb(std::move(img)) {
    setText(name);
    setAccessibleName(name);
    setToolTip(name);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(64);
    setMinimumWidth(44);
}
void SampleTile::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    cropped(p, box, QPixmap::fromImage(thumb), 5);
    QLinearGradient shade(0, 10, 0, height());
    shade.setColorAt(0, Qt::transparent);
    shade.setColorAt(1, QColor(0, 0, 0, 200));
    p.setPen(Qt::NoPen);
    p.setBrush(shade);
    p.drawRoundedRect(box, 5, 5);
    p.setPen(QColor("#f1f1e9"));
    p.setFont(typeface("Microsoft YaHei UI", 11, QFont::DemiBold));
    p.drawText(box.adjusted(8, 0, -5, -5), Qt::AlignBottom | Qt::AlignLeft,
               p.fontMetrics().elidedText(text(), Qt::ElideRight, int(box.width() - 13)));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(isChecked()                  ? QColor("#d1ed87")
                  : underMouse() || hasFocus() ? QColor("#92998b")
                                               : QColor("#4a4d4c"),
                  isChecked() ? 2 : 1));
    p.drawRoundedRect(box, 5, 5);
}
