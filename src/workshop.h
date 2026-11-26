#pragma once
#include "patina.h"
#include <QColor>

enum class WorkshopEffect {
    Patina,
    Photocopy,
    Duotone,
    TimeSlice,
    CRT,
    Thermal,
    Exposure,
    PixelSort,
    DualImage,
    Cinemagraph,
    Glyphs,
    Contours,
    Feedback
};
struct WorkshopSettings {
    struct Photocopy {
        int passes = 4, toner = 54, skew = 35;
    } copy;
    struct Duotone {
        QColor first{"#f04f83"}, second{"#234eb0"};
        int pitch = 6, offset = 3;
    } print;
    struct CRT {
        int mask = 0, glow = 45, curvature = 24;
    } crt;
    struct Thermal {
        int width = 384, heat = 55, missing = 2;
    } thermal;
    struct TimeSlice {
        int direction = 0, spanMs = 320;
    } slice;
    struct Exposure {
        int spanMs = 450, decay = 40;
    } trail;
    struct PixelSort {
        int low = 72, high = 229, direction = 0, length = 120;
    } sort;
    struct DualImage {
        QImage dark;
        QString name;
        int background = 255;
    } dual;
    struct Cinemagraph {
        QImage mask;
        qint64 sourceKey = 0;
        int base = 0, feather = 4;
    } cinema;
    struct Glyphs {
        QString characters = " .:-=+*#%@";
        int fontSize = 13, leading = 2;
    } glyphs;
    struct Contours {
        int levels = 12, width = 2;
        QColor paper{"#eee7d1"}, ink{"#234b4d"};
    } contours;
    struct Feedback {
        int x = 50, y = 50, scale = 85, rotation = 8, decay = 24, edge = 480;
    } feedback;
};
bool requiresMotion(WorkshopEffect effect);
bool animatedOutput(WorkshopEffect effect, const PatinaInput &input);
QString workshopName(WorkshopEffect effect);
PatinaResult renderWorkshop(const PatinaInput &input, WorkshopEffect effect,
                            const WorkshopSettings &settings, bool allFrames, const CancelFlag &cancel = {},
                            const PatinaProgress &progress = {});
