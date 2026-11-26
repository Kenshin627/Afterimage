#include "style.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

const QVector<Dial> &dials() {
    static const QVector<Dial> list{{"light", "画面亮度", 0, 4, .01, 1},
                                    {"contrast", "画面反差", 0, 4, .01, 1},
                                    {"darkFade", "暗部褪色", 0, 128, 1, 0},
                                    {"brightFade", "亮部褪色", 0, 128, 1, 0},
                                    {"vividU", "紫鲜艳度", .1, 4, .01, 1},
                                    {"vividV", "青鲜艳度", .1, 4, .01, 1},
                                    {"shiftU", "紫色偏移", -200, 200, 1, 0},
                                    {"shiftV", "青色偏移", -200, 200, 1, 0},
                                    {"level", "色彩断层", 1, 255, 1, 4},
                                    {"shiftX", "横向偏移", 0, 1000, 1, 0},
                                    {"shiftY", "纵向偏移", 0, 1000, 1, 0},
                                    {"replace", "信息置换", 0, 3, 1, 0},
                                    {"interlaced", "隔行扫描", 0, 8, 1, 0},
                                    {"interlacedLine", "扫描周期", 2, 8, 2, 4},
                                    {"interlacedLight", "扫描亮度差", 0, 1, .01, 0},
                                    {"transposeX", "横向漂移", 0, 10, .01, 0},
                                    {"transposePow", "漂移曲线", 1, 8, .1, 4},
                                    {"transposeNoise", "漂移杂讯", 0, 2, .01, .04},
                                    {"zoom", "像素合并", 1, 20, .01, 1},
                                    {"quality", "信号质量", 0, 1, .05, .6},
                                    {"lightNoise", "信号噪声", 0, 500, 1, 0},
                                    {"darkNoise", "胶片颗粒", 0, 500, 1, 0},
                                    {"yuv420Noise", "色度采样间隔", 1, 8, .01, 1},
                                    {"ratio", "比例变形", .5, 2, .1, 1},
                                    {"border", "边缘偏差", 0, 30, .1, 0},
                                    {"watermarkLeft", "水印水平位置", 0, 1, .01, .5},
                                    {"watermarkTop", "水印垂直位置", 0, 1, .01, .75},
                                    {"watermarkAlpha", "水印透明度", 0, 1, .01, 1},
                                    {"watermarkSize", "水印比例", .3, 1.5, .01, .7},
                                    {"styleLeft", "署名水平位置", 0, 1, .01, .07},
                                    {"styleTop", "署名垂直位置", 0, 1, .01, .16},
                                    {"styleSize", "署名比例", .3, 1.5, .01, .7}};
    return list;
}

QJsonObject completeStyle(const QJsonObject &input) {
    QJsonObject result;
    for (const Dial &dial : dials()) {
        double n = input.value(dial.key).toDouble(dial.initial);
        if (!std::isfinite(n))
            n = dial.initial;
        result[dial.key] = std::clamp(n, dial.minimum, dial.maximum);
    }
    for (auto key :
         {"sharpen", "yuv420", "snow", "sizeOrigin", "watermark", "styleName", "invertLight", "none"})
        result[key] = input.value(key).toBool(QByteArray(key) == "sharpen" || QByteArray(key) == "yuv420");
    const auto select = [&](const char *key, const QStringList &options) {
        const QString choice = input.value(key).toString();
        result[key] = options.contains(choice) ? choice : options.first();
    };
    select("fit", {"cover", "contain", "fill"});
    select("wide", {"4:3", "16:9", "21:9", "1:1"});
    select("convoluteName", {"右倾", "左倾", "桑拿", "浮雕"});
    QString name = input.value("name").toString().simplified().left(60);
    result["name"] = name.isEmpty() ? QStringLiteral("自定义") : name;
    if (input.value("logoPng").isString() && input.value("logoPng").toString().size() <= 24 * 1024 * 1024)
        result["logoPng"] = input.value("logoPng");
    return result;
}

bool isPreset(const QJsonObject &candidate) {
    if (candidate.value("name").toString().trimmed().isEmpty())
        return false;
    for (const Dial &dial : dials())
        if (candidate.value(dial.key).isDouble())
            return true;
    for (auto key :
         {"watermark", "sharpen", "sizeOrigin", "snow", "styleName", "none", "yuv420", "invertLight"})
        if (candidate.value(key).isBool())
            return true;
    return candidate.value("wide").isString() || candidate.value("fit").isString();
}

QJsonObject shareableStyle(const QJsonObject &style) {
    QJsonObject result;
    const QJsonObject base = completeStyle();
    for (auto it = style.begin(); it != style.end(); ++it)
        if (it.key() != "logoPng" &&
            (it.key() == "name" || (base.contains(it.key()) && base[it.key()] != it.value())))
            result[it.key()] = it.value();
    return result;
}

bool enabledFor(const QString &key, const QJsonObject &s) {
    if (key.startsWith("watermark") && key != "watermark")
        return s["watermark"].toBool();
    if (key.startsWith("style") && key != "styleName")
        return s["styleName"].toBool();
    if (key == "transposePow" || key == "transposeNoise")
        return s["transposeX"].toDouble() > 0;
    if (key == "convoluteName")
        return s["sharpen"].toBool();
    if (key == "wide" || key == "fit")
        return !s["sizeOrigin"].toBool();
    if (key == "ratio")
        return s["fit"].toString() != "fill";
    if (key == "yuv420Noise")
        return s["yuv420"].toBool();
    return true;
}

QSize canvasSize(QSize original, const QJsonObject &style) {
    if (style["sizeOrigin"].toBool())
        return original;
    const QString ratio = style["wide"].toString();
    if (ratio == "16:9")
        return {720, 405};
    if (ratio == "21:9")
        return {748, 320};
    if (ratio == "1:1")
        return {500, 500};
    return {640, 480};
}

QVector<QJsonObject> readCollection(const QString &path) {
    QFile file(path);
    QVector<QJsonObject> result;
    if (!file.open(QIODevice::ReadOnly))
        return result;
    for (auto entry : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto object = entry.toObject();
        if (!object.value("name").toString().isEmpty())
            result.append(completeStyle(object));
    }
    return result;
}

bool writeAtomically(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

QVector<QJsonObject> studioPresets() {
    return {completeStyle({{"name", "海岸来信"},
                           {"wide", "16:9"},
                           {"sizeOrigin", true},
                           {"light", 1.04},
                           {"contrast", 1.08},
                           {"darkFade", 9},
                           {"shiftX", 2},
                           {"shiftU", 6},
                           {"shiftV", 8},
                           {"vividU", 1.04},
                           {"vividV", 1.10},
                           {"interlaced", 1},
                           {"interlacedLight", .13},
                           {"darkNoise", 12},
                           {"sharpen", false},
                           {"quality", .9}}),
            completeStyle({{"name", "午夜频率"},
                           {"wide", "16:9"},
                           {"shiftX", 5},
                           {"shiftU", 17},
                           {"shiftV", 8},
                           {"contrast", 1.14},
                           {"vividV", 1.15},
                           {"interlaced", 1},
                           {"interlacedLight", .25},
                           {"darkNoise", 21},
                           {"quality", .8}}),
            completeStyle({{"name", "银盐日记"},
                           {"wide", "16:9"},
                           {"vividU", .1},
                           {"vividV", .1},
                           {"contrast", 1.14},
                           {"darkFade", 14},
                           {"brightFade", 8},
                           {"darkNoise", 29},
                           {"sharpen", false},
                           {"quality", .9}})};
}
