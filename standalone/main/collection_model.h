#pragma once
#include <stdbool.h>
#include <stdint.h>

// CG_DUEL opens the table straight into NABO DUEL; once open the page is CG_TABLE.
typedef enum { CG_HOME, CG_TABLE, CG_DUEL, CG_SETTINGS, CG_STATUS } cg_page_t;
typedef enum { CG_UP, CG_DOWN, CG_OK, CG_BACK } cg_key_t;
typedef struct { int volume, brightness, sleep_seconds; bool speaker; } cg_settings_t;
typedef struct {
    cg_page_t page;
    int home_cursor, settings_cursor, editing;
    cg_settings_t settings, saved_edit;
} cg_model_t;
enum { CG_CHANGED = 1, CG_SAVE = 2, CG_PAGE_CHANGED = 4 };
void cg_init(cg_model_t *m, const cg_settings_t *settings);
void cg_sanitize(cg_settings_t *settings);
int cg_key(cg_model_t *m, cg_key_t key);
