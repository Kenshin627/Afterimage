#pragma once
#include "workshop.h"

QImage renderSpatialEffect(const QImage &source, WorkshopEffect effect, const WorkshopSettings &settings,
                           const CancelFlag &cancel);
