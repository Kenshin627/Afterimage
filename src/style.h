#pragma once
#include <QJsonObject>
#include <QSize>
#include <QStringList>
#include <QVector>

struct Dial {
    const char *key;
    const char *title;
    double minimum, maximum, step, initial;
};

const QVector<Dial> &dials();
QJsonObject completeStyle(const QJsonObject &input = {});
QJsonObject shareableStyle(const QJsonObject &style);
bool isPreset(const QJsonObject &candidate);
bool enabledFor(const QString &key, const QJsonObject &style);
QSize canvasSize(QSize original, const QJsonObject &style);
QVector<QJsonObject> readCollection(const QString &path);
bool writeAtomically(const QString &path, const QByteArray &data, QString *error = nullptr);
QVector<QJsonObject> studioPresets();
