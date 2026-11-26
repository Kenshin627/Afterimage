#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>

class Community : public QObject {
  public:
    using Bytes = std::function<void(QByteArray, QString)>;
    using Done = std::function<void(QString)>;
    explicit Community(QObject *parent = nullptr, QUrl site = QUrl("https://magiconch.com/"));
    void presets(Bytes done);
    void sample(const QString &name, bool thumbnail, Bytes done);
    void submit(bool capture, QByteArray image, QByteArray mime, QString filename, QJsonObject style,
                QString author, Done done);

  private:
    QUrl site;
    QNetworkAccessManager net;
    QNetworkRequest request(const QUrl &url) const;
    void postJson(const QUrl &url, const QJsonObject &object, Bytes done);
    void receive(QNetworkReply *reply, Bytes done);
};
