#pragma once
#include "style.h"
#include <QWidget>
#include <functional>
class QVBoxLayout;
class Histogram;
class Inspector : public QWidget {
    Q_OBJECT
  public:
    explicit Inspector(QWidget *parent = nullptr);
    void sync(const QJsonObject &style);
    void setHistogram(const QImage &image);
  signals:
    void edit(QString key, QJsonValue value);
    void beginGesture();
    void endGesture();
    void chooseLogo();
    void resetLogo();
    void resetStyle();

  private:
    QVBoxLayout *sections = nullptr, *group = nullptr;
    Histogram *histogram = nullptr;
    QList<std::function<void(const QJsonObject &)>> bindings;
    void section(const QString &name, const QString &icon, bool open = false);
    void sliders(std::initializer_list<const char *> keys);
    void toggle(const QString &key, const QString &name);
    void choices(const QString &key, const QString &name, const QStringList &titles,
                 const QStringList &values);
};
