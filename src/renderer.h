#pragma once
#include <QImage>
#include <QJsonObject>
#include <atomic>
#include <memory>

using CancelFlag = std::shared_ptr<std::atomic_bool>;
struct Frame {
    QImage original;
    QImage processed;
    QByteArray jpeg;
    QString error;
};
QImage readImage(const QString &path, QString *error = nullptr);
QImage colorBars();
QImage watermarkImage(const QJsonObject &style);
Frame renderFrame(const QImage &source, const QJsonObject &style, const QImage &logo = {},
                  int thumbnailEdge = 0, const CancelFlag &cancel = {});
bool saveFrame(const Frame &frame, const QString &path, QString *error = nullptr);
bool jpegPass(QImage &image, int quality, QByteArray &data);
bool convolve(QImage &rgba, const QString &mode, const CancelFlag &cancel);
