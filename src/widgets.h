#pragma once
#include "renderer.h"
#include <QAbstractButton>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
class QHBoxLayout;
class QVBoxLayout;
QIcon glyph(const QString &key, QColor color = QColor("#cbd0c5"));
QPushButton *button(const QString &text, const QString &icon = {}, const QString &kind = {});
QLabel *label(const QString &text, const QString &kind = {});
QHBoxLayout *horizontal(QWidget *host, int margin = 0, int spacing = 0);
QVBoxLayout *vertical(QWidget *host, int margin = 0, int spacing = 0);
QWidget *rule(bool vertical = false);
QFont typeface(const QString &family, int pixels, QFont::Weight weight = QFont::Normal);
void applyTheme();

class Canvas : public QWidget {
    Q_OBJECT
  public:
    explicit Canvas(QWidget *parent = nullptr);
    void showFrame(const Frame &frame);
    void setMode(int value);
    void setZoom(double value);
    void resetView();
    void setPreviewBackground(int gray);
    void setFeedbackAnchor(bool enabled,QPointF normalized);
    double zoom() const {
        return zoomValue;
    }
  signals:
    void zoomChanged(double);
    void anchorPlaced(QPointF normalized);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    QRectF picture() const;
    void moveSplit(qreal x);
    void limitPan();
    QImage before, after;
    int mode = 1;
    double zoomValue = 0, split = .5;
    QPointF pan, last;
    bool draggingSplit = false, draggingPan = false;
    bool anchorEnabled=false,draggingAnchor=false;
    int previewBackground=-1;
    QPointF anchor{.5,.5};
    void moveAnchor(QPointF position);
};
class PresetDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return {160, 58};
    }
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
};
class SampleTile : public QAbstractButton {
  public:
    SampleTile(QString name, QImage image, QWidget *parent = nullptr);
    QSize sizeHint() const override {
        return {115, 65};
    }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QImage thumb;
};
