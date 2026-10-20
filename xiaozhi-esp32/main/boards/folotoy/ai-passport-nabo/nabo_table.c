#include "nabo_table.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "config.h"
#include "esp_random.h"
#include "nabo_duel.h"
#include "nabo_link.h"
#include "nabo_table_scene.h"
#include "nabo_table_scene_duel.h"
#include "nabo_table_sound.h"
#include "nvs.h"

#define TAG "NABO-Table"
#define FRAME_US 33333          // 30 fps; NABO's sprites run at 12 fps
#define REPEAT_DELAY_MS 350     // UP / DOWN auto-repeat, as in the prototype
#define REPEAT_EVERY_MS 90
#define LONG_OK_MS 2000         // hold OK to pause

LV_FONT_DECLARE(nabo_table_font_12)
LV_FONT_DECLARE(nabo_menu_font_16)
LV_FONT_DECLARE(nabo_ui_font_18)
LV_FONT_DECLARE(nabo_table_font_24)
LV_FONT_DECLARE(nabo_mono_font_12)
LV_FONT_DECLARE(nabo_poster_font_28)
LV_FONT_DECLARE(nabo_duel_font_56)

typedef struct {
    nt_t model;
    nt_sound_t sound;
    nts_pack_t pack;
    nts_pack_t duel_pack;
    bool has_duel_pack;
} table_state_t;

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static table_state_t *s_st;
static ntg_t *s_gfx;
static uint16_t *s_strip[2];
static uint8_t *s_sprite_buf;
static size_t s_sprite_need;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static esp_timer_handle_t s_timer;
static atomic_bool s_stop, s_running, s_capture, s_capturing;
static uint32_t s_seed;
static atomic_int s_start_on;
static const void *s_duel_pack_data;
static size_t s_duel_pack_size;

// NABO DUEL (nabo_duel.c): allocated only while the duel is open. The table model stays on its
// cover meanwhile and the shared sprite player points at duel_sprites.bin.
static nabo_duel_t *s_duel;
static esp_timer_handle_t s_go_timer;
static uint32_t s_go_armed;               // go_at the timer is set for (0: none)
static bool s_inverted;                   // panel showing the draw
static atomic_uint s_duel_cues, s_wind_ms;
static nd_settings_t s_duel_saved;
static uint16_t s_duel_best_saved;
static atomic_bool s_duel_test;

// Input: button edges and injected keys, from other tasks.
#define EDGE_RING 32
typedef struct { uint8_t button, pressed, injected; uint32_t ms; } edge_t;
static edge_t s_edges[EDGE_RING];
static atomic_uint s_edge_head, s_edge_tail;
static atomic_uint s_events;   // sound events for the audio worker

static void push_edge(edge_t e)
{
    unsigned head = atomic_load(&s_edge_head);
    if (head - atomic_load(&s_edge_tail) >= EDGE_RING) return;
    s_edges[head % EDGE_RING] = e;
    atomic_store(&s_edge_head, head + 1);
}

void nabo_table_button(int button, bool pressed, uint32_t event_ms)
{
    if (!atomic_load(&s_running) || button < 0 || button > 2) return;
    push_edge((edge_t){(uint8_t)button, pressed, 0, event_ms});
}

void nabo_table_inject(int key)
{
    if (atomic_load(&s_running) && key >= 0 && key <= 3) push_edge((edge_t){(uint8_t)key, 1, 1, 0});
}

void nabo_table_audio_fill(int16_t *out, size_t count)
{
    table_state_t *st = s_st;
    if (!atomic_load(&s_running) || !st) return;
    uint32_t e = atomic_exchange(&s_events, 0);
    if (e) nt_sound_events(&st->sound, e);
    uint32_t cues = atomic_exchange(&s_duel_cues, 0);
    if (cues) nt_sound_duel(&st->sound, cues, atomic_load(&s_wind_ms));
    nt_sound_fill(&st->sound, out, count);
}

int nabo_table_status(char *out, size_t cap)
{
    table_state_t *st = s_st;
    if (!atomic_load(&s_running) || !st) return snprintf(out, cap, "{\"running\":0}");
    const nt_t *m = &st->model;
    int len = snprintf(out, cap, "{\"running\":1,\"scene\":%d,\"game\":%d,\"players\":%d,\"cfg_players\":%d,"
                                 "\"score\":[%d,%d,%d,%d],\"round\":%d,\"turn\":%d,\"paused\":%d,\"heap\":%u",
                       m->scene, m->game, m->np, m->cfg.players, m->score[0], m->score[1], m->score[2], m->score[3],
                       m->round, m->turn, m->paused, (unsigned)esp_get_free_heap_size());
    nabo_duel_t *u = s_duel;
    if (u && len > 0 && (size_t)len < cap) {
        const nd_t *d = &u->m;
        uint32_t rx = 0, dropped = 0, fail = 0;
        nabo_link_stats(&rx, &dropped, &fail);
        len += snprintf(out + len, cap - len, ",\"duel\":{\"scene\":%d,\"phase\":%d,\"sel\":%d,\"me\":%d,\"opp\":%d,"
                        "\"round\":%d,\"attempt\":%d,\"host\":%d,\"solo\":%d,\"peers\":%d,\"linked\":%d,"
                        "\"my\":[%d,%d],\"their\":[%d,%d],\"rtt\":%ld,\"offset\":%ld,\"best\":%u,\"radio\":%d,"
                        "\"rx\":%lu,\"rx_drop\":%lu,\"tx_fail\":%lu,\"in_drop\":%d,\"inverted\":%d}",
                        d->scene, d->scene == ND_DUEL ? (int)nd_phase(d, u->ticked) : -1, d->sel, d->match.me,
                        d->match.opp, d->match.round, d->match.attempt, d->match.host, d->match.solo, d->peer_n,
                        d->link.active, d->cur.has_me ? d->cur.me.k : -1, d->cur.me.r, d->cur.has_opp ? d->cur.opp.k : -1,
                        d->cur.opp.r, d->match.has_rtt ? (long)d->match.rtt : -1L, (long)d->match.offset, d->best,
                        nabo_link_active(), (unsigned long)rx, (unsigned long)dropped, (unsigned long)fail, u->in_dropped,
                        s_inverted);
    }
    if (m->game == NT_GAME_UC && len > 0 && (size_t)len < cap) {   // masks: bit per player
        unsigned uc = 0, alive = 0;
        for (int p = 0; p < m->uc.n; p++) uc |= (unsigned)m->uc.uc[p] << p, alive |= (unsigned)m->uc.alive[p] << p;
        len += snprintf(out + len, cap - len, ",\"uc\":{\"n\":%d,\"pair\":%d,\"uc\":%u,\"alive\":%u,\"round\":%d,"
                        "\"speaker\":%d,\"cursor\":%d,\"confirm\":%d,\"over\":%d,\"uc_won\":%d}",
                        m->uc.n, m->uc.pair, uc, alive, m->uc.round, m->uc.speaker, m->uc.cursor, m->uc.confirm,
                        m->result.has, m->uc.uc_won);
    }
    if (len > 0 && (size_t)len + 1 < cap) out[len++] = '}', out[len] = 0;
    return len;
}

/* ---------- settings ---------- */

// Target and player count live next to the other UI settings; older firmware never wrote
// table_players, so a missing key means the two-player table.
static void load_settings(nt_config_t *cfg)
{
    nvs_handle_t h;
    uint8_t target = 3, players = 2, uc_players = 5;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "table_target", &target);
        nvs_get_u8(h, "table_players", &players);
        nvs_get_u8(h, "uc_players", &uc_players);
        nvs_close(h);
    }
    cfg->target = target == 3 || target == 5 || target == 7 ? target : 3;
    cfg->players = players >= NT_MIN_PLAYERS && players <= NT_MAX_PLAYERS ? players : 2;
    cfg->uc_players = uc_players >= NT_UC_MIN && uc_players <= NT_UC_MAX ? uc_players : 5;
}

static void save_settings(const nt_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "table_target", cfg->target);
        if (err == ESP_OK) err = nvs_set_u8(h, "table_players", cfg->players);
        if (err == ESP_OK) err = nvs_set_u8(h, "uc_players", cfg->uc_players);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "settings save failed: %d", err);
}

/* ---------- NABO DUEL ---------- */

static uint32_t clock_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void duel_load_settings(nd_settings_t *set, uint16_t *best)
{
    nvs_handle_t h;
    uint8_t bo = 3, fake = 1, ai = 1;
    uint16_t b = 0;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "duel_bo", &bo);
        nvs_get_u8(h, "duel_fake", &fake);
        nvs_get_u8(h, "duel_ai", &ai);
        nvs_get_u16(h, "duel_best", &b);
        nvs_close(h);
    }
    set->bo = bo == 1 || bo == 3 || bo == 5 ? bo : 3;
    set->fake = fake != 0;
    set->ai = ai < 3 ? ai : 1;
    *best = b;
}

// Writes only what changed since the last save (NVS wear).
static void duel_save_settings(const nabo_duel_t *u)
{
    const nd_t *d = &u->m;
    if (d->suppress) return;   // scripted test
    if (!memcmp(&d->set, &s_duel_saved, sizeof(s_duel_saved)) && d->best == s_duel_best_saved) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "duel_bo", d->set.bo);
        if (err == ESP_OK) err = nvs_set_u8(h, "duel_fake", d->set.fake);
        if (err == ESP_OK) err = nvs_set_u8(h, "duel_ai", d->set.ai);
        if (err == ESP_OK && !d->suppress) err = nvs_set_u16(h, "duel_best", d->best);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "duel settings save failed: %d", err);
    s_duel_saved = d->set;
    s_duel_best_saved = d->best;
}

static void set_inverted(bool on)
{
    if (on == s_inverted) return;
    // The panel is wired inverted (DISPLAY_INVERT_COLOR): the draw turns that off, one command.
    esp_lcd_panel_invert_color(s_panel, on ? !DISPLAY_INVERT_COLOR : DISPLAY_INVERT_COLOR);
    s_inverted = on;
}

static void go_tick(void *arg)
{
    (void)arg;
    if (s_task) xTaskNotifyGive(s_task);   // wake the game task right at go_at
}

static void use_sprite_pack(const nts_pack_t *pack, uint8_t *buf, size_t need)
{
    nts_player_init(&s_gfx->sprite, pack, buf, need);
    ntg_invalidate(s_gfx);
}

// Swaps the sprite buffer to fit `pack`; keeps the old one if the heap cannot give a new one.
static bool resize_sprite_buf(size_t need)
{
    if (need <= s_sprite_need) return true;
    uint8_t *b = heap_caps_calloc(1, need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b) {
        heap_caps_free(s_sprite_buf);   // second try with the old buffer out of the way
        s_sprite_buf = NULL;
        b = heap_caps_calloc(1, need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!b) {
            s_sprite_buf = heap_caps_calloc(1, s_sprite_need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!s_sprite_buf) s_sprite_need = 0;   // the player then refuses buffered animations
            return false;
        }
    } else {
        heap_caps_free(s_sprite_buf);
    }
    s_sprite_buf = b;
    s_sprite_need = need;
    return true;
}

static void duel_leave(void)
{
    nabo_duel_t *u = s_duel;
    if (!u) return;
    if (s_go_timer) esp_timer_stop(s_go_timer);
    s_go_armed = 0;
    set_inverted(false);
    duel_save_settings(u);
    nabo_link_end();
    s_duel = NULL;
    heap_caps_free(u);
    // Back to the table's (smaller) buffer.
    size_t need = nts_buffer_need(&s_st->pack);
    uint8_t *b = heap_caps_calloc(1, need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (b) { heap_caps_free(s_sprite_buf); s_sprite_buf = b; s_sprite_need = need; }
    use_sprite_pack(&s_st->pack, s_sprite_buf, s_sprite_need);
    atomic_store(&s_duel_cues, 0);
    ESP_LOGI(TAG, "duel closed: free=%u largest=%u", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static void duel_enter(void)
{
    if (s_duel || !s_st->has_duel_pack) return;
    size_t need = nts_buffer_need(&s_st->duel_pack);
    nabo_duel_t *u = heap_caps_calloc(1, sizeof(*u), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!u || !resize_sprite_buf(need)) {
        heap_caps_free(u);
        use_sprite_pack(&s_st->pack, s_sprite_buf, s_sprite_need);   // the buffer may have moved
        ESP_LOGE(TAG, "duel: not enough memory (free=%u largest=%u need=%u)", (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), (unsigned)need);
        return;
    }
    if (!s_go_timer) {
        const esp_timer_create_args_t args = {.callback = go_tick, .name = "nabo_duel_go"};
        esp_timer_create(&args, &s_go_timer);
    }
    bool radio = nabo_link_begin();   // practice still works without it
    uint8_t mac[ND_MAC_LEN];
    nabo_link_mac(mac);
    nd_settings_t set;
    uint16_t best;
    duel_load_settings(&set, &best);
    s_duel_saved = set;
    s_duel_best_saved = best;
    ndu_init(u, mac, esp_random(), &set, best, clock_ms());
    u->m.suppress = atomic_load(&s_duel_test);
    s_duel = u;
    s_inverted = false;
    s_go_armed = 0;
    use_sprite_pack(&s_st->duel_pack, s_sprite_buf, s_sprite_need);
    ESP_LOGI(TAG, "duel open (radio %s): free=%u largest=%u", radio ? "up" : "FAILED", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

// One frame of the duel: radio in, the draw, the model, radio out, screen.
static void duel_frame(const nt_fonts_t *fonts)
{
    nabo_duel_t *u = s_duel;
    nabo_link_rx_t rx;
    while (nabo_link_poll(&rx)) {
        nd_pkt_t p;
        if (nd_decode(rx.data, rx.len, &p)) ndu_rx(u, rx.mac, &p, rx.rssi, rx.t);
    }
    uint32_t go_at, now = clock_ms();
    if (ndu_go_pending(u, &go_at) && (int32_t)(now - go_at) >= 0) {
        set_inverted(true);
        ndu_flipped(u, clock_ms());   // reaction times count from this instant, on both badges
    }
    ndu_advance(u, clock_ms());
    for (int i = 0; i < u->m.out_n; i++) {
        uint8_t wire[ND_WIRE_MAX];
        size_t n = nd_encode(&u->m.out[i].pkt, wire, sizeof(wire));
        if (n) nabo_link_send(u->m.out[i].broadcast ? NULL : u->m.out[i].to, wire, n);
    }
    u->m.out_n = 0;
    nabo_link_set_session(u->m.link.active ? u->m.link.session : 0);
    if (ndu_go_pending(u, &go_at)) {   // wake exactly at the draw
        int32_t wait = (int32_t)(go_at - clock_ms());
        if (s_go_armed != go_at && wait > 0) {
            esp_timer_stop(s_go_timer);
            esp_timer_start_once(s_go_timer, (uint64_t)wait * 1000);
            s_go_armed = go_at;
        }
    }
    set_inverted(ndu_inverted(u));
    uint32_t cues = ndu_take_cues(u);
    if (cues & NDU_CUE_WIND) atomic_store(&s_wind_ms, u->wind_ms);
    if (cues) atomic_fetch_or(&s_duel_cues, cues);
    if (u->m.scene == ND_END || u->m.scene == ND_COVER) duel_save_settings(u);
    nt_scene_duel(s_gfx, u, fonts, &nabo_duel_font_56);
}

/* ---------- game task ---------- */

static void send(void *ctx, int x, int y, int w, int h, uint16_t *px)
{
    (void)ctx;
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, px);
    if (atomic_load(&s_capturing)) {   // serial screenshot: the strip exactly as sent (big-endian RGB565)
        static const char HEX[] = "0123456789abcdef";
        char line[NTG_W * 4 + 1];
        for (int r = 0; r < h; r++) {
            const uint8_t *b = (const uint8_t *)(px + r * w);
            for (int i = 0; i < w * 2; i++) { line[2 * i] = HEX[b[i] >> 4]; line[2 * i + 1] = HEX[b[i] & 15]; }
            line[w * 4] = 0;
            printf("@cap %d %d %d %s\n", y + r, x, w, line);
        }
    }
}

void nabo_table_capture(void) { atomic_store(&s_capture, true); }

void nabo_table_start_on(int where) { atomic_store(&s_start_on, where); }

void nabo_table_duel_test(bool on)
{
    atomic_store(&s_duel_test, on);
    nabo_duel_t *u = s_duel;
    if (u) u->m.suppress = on;
}

void nabo_table_set_duel_pack(const void *pack, size_t pack_size)
{
    s_duel_pack_data = pack;
    s_duel_pack_size = pack_size;
}

static void tick(void *arg)
{
    (void)arg;
    if (s_task) xTaskNotifyGive(s_task);
}

typedef struct {
    bool down;
    uint32_t since, next_repeat;
    bool long_sent;
} key_state_t;

// Turns edges into model keys: UP / DOWN act on press and repeat while held; OK acts on release,
// or once as a long press after LONG_OK_MS.
static void handle_input(nt_t *m, key_state_t keys[3], uint32_t now_ms)
{
    while (atomic_load(&s_edge_tail) != atomic_load(&s_edge_head)) {
        unsigned tail = atomic_load(&s_edge_tail);
        edge_t e = s_edges[tail % EDGE_RING];
        atomic_store(&s_edge_tail, tail + 1);
        if (s_duel) {   // the duel reads raw, time-stamped edges (OK fires on press)
            if (e.injected && e.button <= NABO_TABLE_OK) {
                ndu_key(s_duel, (nd_key_t)e.button, true, now_ms);
                ndu_key(s_duel, (nd_key_t)e.button, false, now_ms + 20);
            } else if (!e.injected) {
                ndu_key(s_duel, (nd_key_t)e.button, e.pressed, e.ms);
            }
            continue;
        }
        if (e.injected) {
            nt_key(m, e.button == 3 ? NT_KEY_OK_LONG : (nt_key_t)e.button);
            continue;
        }
        key_state_t *k = &keys[e.button];
        if (e.pressed && !k->down) {
            k->down = true;
            k->since = e.ms;
            k->long_sent = false;
            k->next_repeat = e.ms + REPEAT_DELAY_MS;
            if (e.button != NABO_TABLE_OK) nt_key(m, e.button == NABO_TABLE_UP ? NT_KEY_UP : NT_KEY_DOWN);
        } else if (!e.pressed && k->down) {
            k->down = false;
            if (e.button == NABO_TABLE_OK && !k->long_sent) nt_key(m, NT_KEY_OK);
        }
    }
    if (s_duel) return;
    for (int b = 0; b < 2; b++)
        while (keys[b].down && (int32_t)(now_ms - keys[b].next_repeat) >= 0) {
            keys[b].next_repeat += REPEAT_EVERY_MS;
            nt_key(m, b == NABO_TABLE_UP ? NT_KEY_UP : NT_KEY_DOWN);
        }
    key_state_t *ok = &keys[NABO_TABLE_OK];
    if (ok->down && !ok->long_sent && now_ms - ok->since >= LONG_OK_MS && nt_can_pause(m)) {
        ok->long_sent = true;
        nt_key(m, NT_KEY_OK_LONG);
    }
}

static void game_task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait until nabo_table_open() finished allocating
    if (atomic_load(&s_stop)) {
        xSemaphoreGive(s_done);
        vTaskDelete(NULL);
        return;
    }
    nt_fonts_t fonts = {&nabo_table_font_12, &nabo_menu_font_16, &nabo_ui_font_18,
                        &nabo_table_font_24, &nabo_mono_font_12, &nabo_poster_font_28};
    nt_config_t cfg = {.dice_per = 5, .min_open = 2, .wild = true};
    load_settings(&cfg);
    // One-shot lengths come from the sprites, so scene timing matches what NABO is doing.
    static const struct { nt_anim_t anim; int sprite; } LEN[] = {
        {NT_ANIM_SHUFFLE, NTS_SMALL_CUT}, {NT_ANIM_DEAL, NTS_SMALL_DEAL}, {NT_ANIM_SLAM, NTS_SMALL_SLAM},
        {NT_ANIM_COVER_IN, NTS_LARGE_COVER_IN}, {NT_ANIM_OPEN, NTS_SMALL_OPEN}, {NT_ANIM_SURPRISE, NTS_SMALL_SURPRISE}};
    for (size_t i = 0; i < sizeof(LEN) / sizeof(LEN[0]); i++) {
        nts_anim_t a;
        if (nts_anim(&s_st->pack, LEN[i].sprite, &a) && a.fps) cfg.anim_s[LEN[i].anim] = (float)a.frames / a.fps;
    }
    nt_t *m = &s_st->model;
    nt_init(m, &cfg, s_seed);
    int start_on = atomic_exchange(&s_start_on, NABO_TABLE_START_COVER);
    if (start_on == NABO_TABLE_START_UNDERCOVER) nt_uc_setup(m);
    if (start_on == NABO_TABLE_START_DUEL) {
        while (m->menu != NT_MENU_DUEL) nt_key(m, NT_KEY_DOWN);
        nt_take_events(m);
        duel_enter();
    }
    nt_sound_reset(&s_st->sound);
    key_state_t keys[3] = {0};
    int64_t last = esp_timer_get_time(), stats_at = last;
    uint32_t frames = 0, work_us = 0, max_us = 0, bytes = 0;
    while (!atomic_load(&s_stop)) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        int64_t now = esp_timer_get_time();
        double dt = (now - last) / 1e6;
        last = now;
        if (dt > 0.1) dt = 0.1;
        handle_input(m, keys, (uint32_t)(now / 1000));
        bool capture = atomic_exchange(&s_capture, false);
        if (capture) {
            ntg_invalidate(s_gfx);
            atomic_store(&s_capturing, true);
            printf("\n@cap begin\n");
        }
        int64_t t0 = esp_timer_get_time();
        if (s_duel) {
            duel_frame(&fonts);
            if (s_duel->m.exit_req) {
                duel_leave();
                memset(keys, 0, sizeof(keys));
                nt_scene_build(s_gfx, m, &fonts);
            }
        } else {
            nt_update(m, dt);
            uint32_t ev = nt_take_events(m);
            if (ev & NT_EV_SETTINGS) save_settings(&m->cfg);
            if (ev & NT_EV_DUEL) duel_enter();
            if (ev) atomic_fetch_or(&s_events, ev & ~(uint32_t)NT_EV_DUEL);
            if (s_duel) duel_frame(&fonts);
            else nt_scene_build(s_gfx, m, &fonts);
        }
        ntg_flush(s_gfx);
        if (capture) {
            atomic_store(&s_capturing, false);
            printf("@cap end\n");
        }
        uint32_t used = (uint32_t)(esp_timer_get_time() - t0);
        ++frames, work_us += used, bytes += s_gfx->bytes;
        if (used > max_us) max_us = used;
        if (now - stats_at >= 5000000) {
            ESP_LOGI(TAG, "scene=%d fps=%lu frame_us mean=%lu max=%lu bytes=%lu ops=%d glyphs=%d heap=%u",
                     (int)m->scene, (unsigned long)(frames * 1000000ULL / (now - stats_at)),
                     (unsigned long)(work_us / frames), (unsigned long)max_us, (unsigned long)(bytes / frames),
                     s_gfx->n, s_gfx->glyphs.used, (unsigned)esp_get_free_heap_size());
            stats_at = now, frames = work_us = max_us = bytes = 0;
        }
    }
    duel_leave();                                      // radio back to XiaoZhi, panel colours normal
    esp_lcd_panel_io_tx_param(s_io, 0x00, NULL, 0);   // NOP: returns once the last strip has gone out
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

/* ---------- lifecycle ---------- */

static void release(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    heap_caps_free(s_gfx);
    heap_caps_free(s_strip[0]);
    heap_caps_free(s_strip[1]);
    if (s_go_timer) {
        esp_timer_stop(s_go_timer);
        esp_timer_delete(s_go_timer);
        s_go_timer = NULL;
    }
    heap_caps_free(s_sprite_buf);
    heap_caps_free(s_st);
    s_gfx = NULL, s_strip[0] = s_strip[1] = NULL, s_sprite_buf = NULL, s_st = NULL;
    s_sprite_need = 0;
}

static void log_heap(const char *what)
{
    ESP_LOGI(TAG, "%s: free=%u largest=%u", what, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static void *alloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static void *alloc_dma(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL); }

bool nabo_table_open(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, uint32_t seed, const void *pack,
                     size_t pack_size)
{
    if (atomic_load(&s_running)) return true;
    s_io = io, s_panel = panel, s_seed = seed;
    log_heap("open");
    if (!s_done) s_done = xSemaphoreCreateBinary();
    atomic_store(&s_stop, false);
    atomic_store(&s_events, 0);
    atomic_store(&s_edge_tail, atomic_load(&s_edge_head));
    s_task = NULL;
    // Largest blocks first while the heap still has big holes, then the task stack.
    s_gfx = alloc(sizeof(ntg_t));
    s_st = alloc(sizeof(table_state_t));
    bool ok = s_gfx && s_st && nts_open(&s_st->pack, pack, pack_size);
    size_t need = ok ? nts_buffer_need(&s_st->pack) : 0;
    if (ok) s_st->has_duel_pack = s_duel_pack_data && nts_open(&s_st->duel_pack, s_duel_pack_data, s_duel_pack_size);
    if (ok) {
        s_sprite_need = need;
        s_sprite_buf = alloc(need);
        s_strip[0] = alloc_dma(NTG_W * NTG_STRIP * 2);
        s_strip[1] = alloc_dma(NTG_W * NTG_STRIP * 2);
        ok = s_sprite_buf && s_strip[0] && s_strip[1];
    }
    if (!ok || !s_done || xTaskCreate(game_task, "nabo_table", 6144, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
        log_heap(ok ? "open: task failed" : "open: buffers or sprite pack failed");
        release();
        return false;
    }
    ntg_init(s_gfx, &s_st->pack, s_sprite_buf, need, s_strip[0], s_strip[1], send, NULL);
    const esp_timer_create_args_t args = {.callback = tick, .name = "nabo_table"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        atomic_store(&s_stop, true);
        xTaskNotifyGive(s_task);   // the task sees s_stop before touching any buffer
        xSemaphoreTake(s_done, pdMS_TO_TICKS(500));
        s_task = NULL;
        release();
        return false;
    }
    atomic_store(&s_running, true);
    xTaskNotifyGive(s_task);
    esp_timer_start_periodic(s_timer, FRAME_US);
    log_heap("open: running");
    return true;
}

void nabo_table_close(void)
{
    if (!atomic_load(&s_running)) return;
    atomic_store(&s_stop, true);
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) ESP_LOGE(TAG, "close: game task did not stop");
    s_task = NULL;
    atomic_store(&s_running, false);   // the audio worker stops touching s_st first
    vTaskDelay(pdMS_TO_TICKS(40));      // let a fill already in progress (20 ms blocks) finish
    release();
    log_heap("closed");
}

bool nabo_table_running(void) { return atomic_load(&s_running); }

bool nabo_table_on_cover(void)
{
    table_state_t *st = s_st;
    return !atomic_load(&s_running) || !st || (st->model.scene == NT_MENU && !st->model.paused && !s_duel);
}
