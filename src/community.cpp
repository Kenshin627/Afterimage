#include "community.h"
#include <QDir>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

Community::Community(QObject *parent, QUrl url) : QObject(parent), site(std::move(url)), net(this) {
    auto *cache = new QNetworkDiskCache(&net);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/network");
    cache->setMaximumCacheSize(256LL * 1024 * 1024);
    net.setCache(cache);
}

QNetworkRequest Community::request(const QUrl &url) const {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader,
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Afterimage/1.3");
    r.setRawHeader("Referer", site.resolved(QUrl("vaporwave/")).toEncoded());
    r.setTransferTimeout(25000);
    return r;
}

void Community::receive(QNetworkReply *reply, Bytes done) {
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        QString error;
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
            error = QString("服务请求失败（%1） %2").arg(status).arg(reply->errorString());
        reply->deleteLater();
        done(data, error);
    });
}

void Community::presets(Bytes done) {
    auto r = request(site.resolved(QUrl("api/vaporwave/style")));
    r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    receive(net.get(r), std::move(done));
}

void Community::sample(const QString &name, bool thumbnail, Bytes done) {
    if (name != QFileInfo(name).fileName() || !name.endsWith(".jpg")) {
        done({}, "样片名称无效。");
        return;
    }
    auto r = request(site.resolved(QUrl("vaporwave/captures/" + QString(thumbnail ? "w300/" : "") + name)));
    r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    receive(net.get(r), std::move(done));
}

void Community::postJson(const QUrl &url, const QJsonObject &object, Bytes done) {
    auto r = request(url);
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    receive(net.post(r, QJsonDocument(object).toJson(QJsonDocument::Compact)), std::move(done));
}

void Community::submit(bool capture, QByteArray image, QByteArray mime, QString filename, QJsonObject style,
                       QString author, Done done) {
    const QString plan = capture ? "capture" : "output";
    postJson(
        site.resolved(QUrl("api/upyun/authorization")), {{"plan", plan}}, [=](QByteArray raw, QString err) {
            if (!err.isEmpty()) {
                done(err);
                return;
            }
            const QJsonObject grant = QJsonDocument::fromJson(raw).object();
            const QString endpoint = grant["api"].toString() + grant["bucket"].toString();
            const QUrl target = site.resolved(QUrl(endpoint));
            const bool localTest =
                site.host() == "127.0.0.1" && site.host() == target.host() && site.port() == target.port();
            if (endpoint.isEmpty() || (!localTest && target.scheme() != "https") ||
                target.host().isEmpty() || grant["policy"].toString().isEmpty() ||
                grant["authorization"].toString().isEmpty()) {
                done("原站没有返回有效的上传授权。");
                return;
            }
            auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
            const auto add = [form](const QByteArray &name, const QByteArray &value) {
                QHttpPart part;
                part.setHeader(QNetworkRequest::ContentDispositionHeader,
                               "form-data; name=\"" + name + "\"");
                part.setBody(value);
                form->append(part);
            };
            add("policy", grant["policy"].toString().toUtf8());
            add("authorization", grant["authorization"].toString().toUtf8());
            QHttpPart part;
            QString safeName = QFileInfo(filename).fileName();
            safeName.replace('"', '_').replace('\r', '_').replace('\n', '_');
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           "form-data; name=\"file\"; filename=\"" + safeName + "\"");
            part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
            part.setBody(image);
            form->append(part);
            auto *reply = net.post(request(target), form);
            form->setParent(reply);
            receive(reply, [=](QByteArray uploaded, QString uploadError) {
                if (!uploadError.isEmpty()) {
                    done(uploadError);
                    return;
                }
                const auto obj = QJsonDocument::fromJson(uploaded).object();
                if (obj["url"].toString().isEmpty() || obj["code"].toInt(200) != 200) {
                    done("图片服务未确认上传。" + obj["message"].toString());
                    return;
                }
                const QString imageUrl =
                    site.resolved(QUrl(grant["prefix"].toString() + obj["url"].toString())).toString();
                QJsonObject payload;
                if (capture)
                    payload["src"] = imageUrl;
                else {
                    payload["preview"] = imageUrl;
                    payload["style"] = style;
                }
                if (!author.trimmed().isEmpty())
                    payload["author"] = author.trimmed();
                postJson(
                    site.resolved(QUrl(capture ? "api/vaporwave/capture" : "api/vaporwave/style")), payload,
                    [done](QByteArray body, QString error) {
                        if (!error.isEmpty()) {
                            done(error);
                            return;
                        }
                        QJsonParseError parse;
                        const auto envelope = QJsonDocument::fromJson("[" + body + "]", &parse).array();
                        const auto response = envelope.isEmpty() ? QJsonValue() : envelope.first();
                        const auto ack = response.toObject();
                        const int code = ack["code"].toInt(200);
                        const auto problem = ack.value("error");
                        const bool failed = problem.isObject() || problem.isArray() || problem.toBool() ||
                                            !problem.toString().isEmpty() || problem.toDouble() != 0;
                        const bool accepted =
                            response.isObject() || response.isArray() || response.toBool() ||
                            response.toDouble() > 0 ||
                            response.toString().compare("ok", Qt::CaseInsensitive) == 0 ||
                            response.toString().compare("success", Qt::CaseInsensitive) == 0;
                        if (parse.error != QJsonParseError::NoError || envelope.size() != 1 || !accepted ||
                            failed || (ack.contains("success") && !ack["success"].toBool()) ||
                            (code != 0 && (code < 200 || code >= 300))) {
                            done("原站未确认收件。" + ack["message"].toString());
                            return;
                        }
                        done({});
                    });
            });
        });
}
