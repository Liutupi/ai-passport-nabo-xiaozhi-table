#pragma once
#include "collection_model.h"
#include <stdbool.h>
#include <stdint.h>

bool cg_ui_open(void);
void cg_ui_close(void);
void cg_ui_draw(const cg_model_t *model, uint32_t ms, int battery, int battery_mv);
void cg_ui_capture(void);
void cg_ui_invalidate(void);
