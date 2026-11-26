#pragma once
#include "renderer.h"
#include "animatedgif.h"
#include <functional>

struct PatinaSettings {
    bool green = true, watermark = true, random = true, thumbnail = true, pop = false, superMode = false;
    int years = 12, quality = 60, grid = 4, maxEdge = 500, zoom = 100, shiftX = 0, shiftY = 0;
    double blur = 1, noise = 0, grain = 0, light = 0, contrast = 1, greenBoost = 0, greenLuma = 1;
    double stampSize = 1, shadow = .6;
    int positions = 2;
    QString kernel;
    QStringList names = {"海岸来信", "像素电台_0000", "_午夜放送_",
                         "余像档案", "网络过客0000",  "旧图收藏家"};
    quint32 seed = 1;
};
struct PatinaInput {
    QImage image;
    QString name;
    QByteArray gif;
    GifTiming timing;
    bool animated() const { return timing.delays.size() > 1; }
};
struct PatinaResult {
    Frame frame;
    QByteArray gif;
    QString plainText;
};
using PatinaProgress = std::function<void(int completed, int total)>;
PatinaInput openPatinaInput(const QString &path, QString *error);
PatinaInput gifPatinaInput(const QByteArray &bytes, const QString &name, QString *error);
PatinaResult renderPatina(const PatinaInput &input, const PatinaSettings &settings, bool allFrames,
                          const CancelFlag &cancel = {}, const PatinaProgress &progress = {});
