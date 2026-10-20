#include "collection_model.h"
#include <string.h>

static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static const int sleep_options[] = {0, 30, 60, 120, 300, 600};

void cg_sanitize(cg_settings_t *s) {
    s->volume = clamp(s->volume, 0, 100);
    s->brightness = clamp(s->brightness, 10, 100);
    s->sleep_seconds = clamp(s->sleep_seconds, 0, 600);
}

void cg_init(cg_model_t *m, const cg_settings_t *settings) {
    memset(m, 0, sizeof(*m));
    m->settings = *settings;
    cg_sanitize(&m->settings);
    m->editing = -1;
}

int cg_key(cg_model_t *m, cg_key_t key) {
    cg_page_t before = m->page;
    int result = CG_CHANGED;
    if (key == CG_BACK) {
        if (m->page == CG_SETTINGS && m->editing >= 0) {
            m->settings = m->saved_edit;
            m->editing = -1;
        } else {
            m->page = m->page == CG_STATUS ? CG_SETTINGS : CG_HOME;
        }
    } else if (m->page == CG_HOME) {
        if (key == CG_UP || key == CG_DOWN)
            m->home_cursor = (m->home_cursor + (key == CG_UP ? 2 : 1)) % 3;
        else if (key == CG_OK) {
            static const cg_page_t pages[] = {CG_TABLE, CG_DUEL, CG_SETTINGS};
            m->page = pages[m->home_cursor];
        }
    } else if (m->page == CG_SETTINGS) {
        if (m->editing >= 0) {
            if (key == CG_OK) { m->editing = -1; result |= CG_SAVE; }
            else {
                int d = key == CG_UP ? 1 : -1;
                if (m->editing == 0) m->settings.volume += d * 5;
                if (m->editing == 1) m->settings.brightness += d * 5;
                if (m->editing == 3) {
                    int i = 0;
                    while (i < 5 && sleep_options[i] < m->settings.sleep_seconds) i++;
                    i = clamp(i + d, 0, 5);
                    m->settings.sleep_seconds = sleep_options[i];
                }
                cg_sanitize(&m->settings);
            }
        } else if (key == CG_UP || key == CG_DOWN) {
            m->settings_cursor = (m->settings_cursor + (key == CG_UP ? 5 : 1)) % 6;
        } else if (key == CG_OK) {
            if (m->settings_cursor == 5) m->page = CG_HOME;
            else if (m->settings_cursor == 4) m->page = CG_STATUS;
            else if (m->settings_cursor == 2) {
                m->settings.speaker = !m->settings.speaker;
                result |= CG_SAVE;
            } else {
                m->saved_edit = m->settings;
                m->editing = m->settings_cursor;
            }
        }
    } else if (m->page == CG_STATUS) {
        m->page = CG_SETTINGS;
    } else return 0;  // game input is owned by its game, except CG_BACK above
    if (m->page != before) result |= CG_PAGE_CHANGED;
    return result;
}
