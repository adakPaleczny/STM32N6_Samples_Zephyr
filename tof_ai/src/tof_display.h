#pragma once

#include <zephyr/device.h>
#include "NanoEdgeAI.h"

int  tof_display_init(const struct device *disp_dev);
void tof_display_update_ai(int id_class,
                            const float probabilities[NEAI_NUMBER_OF_CLASSES]);
