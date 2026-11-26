#pragma once
#include "workshop.h"

QImage sortPixels(const QImage &, const WorkshopSettings::PixelSort &, const CancelFlag &);
QImage dualImage(const QImage &light, const QImage &dark, const CancelFlag &);
QImage glyphPicture(const QImage &, const WorkshopSettings::Glyphs &, QString *text, const CancelFlag &);
QImage contourPoster(const QImage &, const WorkshopSettings::Contours &, const CancelFlag &);
QImage readWorkshopFrame(const PatinaInput &, int index, const CancelFlag &cancel = {});
QImage featherMask(const QImage &, int radius, const CancelFlag &);
QImage blendMotion(const QImage &base, const QImage &current, const QImage &mask, const CancelFlag &);
PatinaResult renderFeedback(const PatinaInput &, const WorkshopSettings::Feedback &, bool full,
                            const CancelFlag &, const PatinaProgress &);
