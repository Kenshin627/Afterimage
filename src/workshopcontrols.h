#pragma once
#include "workshop.h"
#include <functional>
class QWidget;
QWidget *workshopControls(WorkshopEffect effect, WorkshopSettings &settings, std::function<void()> changed,
                          std::function<const PatinaInput &()> source,
                          std::function<void()> previewChanged);
