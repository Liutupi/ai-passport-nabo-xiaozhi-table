#include "collection_ui.h"
#include "collection_portrait.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "nabo_table_gfx.h"

LV_FONT_DECLARE(collection_font_12)
LV_FONT_DECLARE(collection_font_16)
LV_FONT_DECLARE(collection_font_18)
LV_FONT_DECLARE(nabo_poster_font_28)
LV_FONT_DECLARE(nabo_mono_font_12)

extern const uint8_t table_pack_start[] asm("_binary_table_sprites_bin_start");
extern const uint8_t table_pack_end[] asm("_binary_table_sprites_bin_end");
static nts_pack_t s_pack;
static ntg_t *s_gfx;
static uint16_t *s_strip[2];
static uint8_t *s_sprite;
static bool s_capture;
static const uint16_t INK = 0x1927, PAPER = 0xef3b, GOLD = 0xfd70, WHITE = 0xffff;

static void send_strip(void *ctx, int x, int y, int w, int h, uint16_t *px) {
    (void)ctx;
    if (s_capture) {
        const uint8_t *p = (const uint8_t *)px;
        for (int row = 0; row < h; row++) {
            printf("@cap %d %d %d ", y + row, x, w);
            for (int col = 0; col < w; col++) {
                int i = 2 * (row * w + col);
                printf("%02x%02x", p[i], p[i + 1]);
            }
            putchar('\n');
        }
    }
    esp_lcd_panel_draw_bitmap(bsp_display_panel(), x, y, x + w, y + h, px);
    // The caller may reuse this DMA strip as soon as we return.
    esp_lcd_panel_io_tx_param(bsp_display_io(), 0x00, NULL, 0);
}

bool cg_ui_open(void) {
    if (s_gfx) return true;
    if (!nts_open(&s_pack, table_pack_start, table_pack_end - table_pack_start)) return false;
    s_gfx = calloc(1, sizeof(*s_gfx));
    size_t need = nts_buffer_need(&s_pack);
    s_sprite = malloc(need);
    for (int i = 0; i < 2; i++)
        s_strip[i] = heap_caps_malloc(BSP_LCD_W * NTG_STRIP * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_gfx || !s_sprite || !s_strip[0] || !s_strip[1]) {
        cg_ui_close();
        return false;
    }
    ntg_init(s_gfx, &s_pack, s_sprite, need, s_strip[0], s_strip[1], send_strip, NULL);
    return true;
}

void cg_ui_close(void) {
    esp_lcd_panel_io_tx_param(bsp_display_io(), 0x00, NULL, 0);
    free(s_gfx); s_gfx = NULL;
    free(s_sprite); s_sprite = NULL;
    for (int i = 0; i < 2; i++) { free(s_strip[i]); s_strip[i] = NULL; }
    s_capture = false;
}

static void text(const lv_font_t *font, const char *s, int x, int baseline, nt_align_t align, uint16_t color) {
    ntg_text(s_gfx, font, s, x, baseline, align, color);
}

void cg_ui_invalidate(void) {
    if (s_gfx) ntg_invalidate(s_gfx);
}

void cg_ui_capture(void) { cg_ui_invalidate(); s_capture = true; }

void cg_ui_draw(const cg_model_t *m, uint32_t ms, int battery, int mv) {
    (void)ms;
    if (!s_gfx) return;
    ntg_begin(s_gfx, m->page);
    ntg_rect(s_gfx, 0, 0, 240, 320, 0, INK, 255, 0, 0);
    text(&nabo_poster_font_28, m->page == CG_HOME ? "NABO TABLE" : m->page == CG_STATUS ? "STATUS" : "SETTINGS",
         120, 39, NT_ALIGN_CENTER, GOLD);
    if (m->page == CG_HOME) {
        text(&collection_font_16, "牌桌 · 对决 · 离线版", 120, 62, NT_ALIGN_CENTER, PAPER);
        ntg_bitmap(s_gfx, collection_portrait, 79, 66, COLLECTION_PORTRAIT_W, COLLECTION_PORTRAIT_H);
        static const char *const names[] = {"NABO 牌桌", "NABO 对决", "设置"};
        static const char *const notes[] = {"21 点 · 大话骰 · 谁是卧底", "两台工牌联机 · 单人练习", "音量 · 亮度 · 休眠"};
        for (int i = 0; i < 3; i++) {
            bool active = i == m->home_cursor;
            ntg_rect(s_gfx, 20, 156 + i * 46, 200, 40, 6, active ? GOLD : 0x2229, 255, active ? WHITE : 0x42ec, 1);
            text(&collection_font_18, names[i], 120, 176 + i * 46, NT_ALIGN_CENTER, active ? INK : PAPER);
            text(&collection_font_12, notes[i], 120, 191 + i * 46, NT_ALIGN_CENTER, active ? INK : 0xa514);
        }
        text(&collection_font_16, "上/下选择  OK进入", 120, 309, NT_ALIGN_CENTER, PAPER);
    } else if (m->page == CG_SETTINGS) {
        text(&collection_font_16, m->editing >= 0 ? "上/下调整 · OK保存" : "声音 · 显示 · 休眠", 120, 69, NT_ALIGN_CENTER, PAPER);
        static const char *const names[] = {"音量", "亮度", "扬声器", "自动休眠", "设备状态", "返回"};
        for (int i = 0; i < 6; i++) {
            bool active = i == m->settings_cursor;
            int y = 93 + 30 * i;
            ntg_rect(s_gfx, 16, y, 208, 27, 5, active ? GOLD : INK, 255, 0x42ec, active ? 1 : 0);
            text(&collection_font_18, names[i], 26, y + 21, NT_ALIGN_LEFT, active ? INK : PAPER);
            char value[24] = "";
            if (i == 0) snprintf(value, sizeof(value), "%d%%", m->settings.volume);
            if (i == 1) snprintf(value, sizeof(value), "%d%%", m->settings.brightness);
            if (i == 2) snprintf(value, sizeof(value), "%s", m->settings.speaker ? "开" : "关");
            if (i == 3) {
                if (m->settings.sleep_seconds) snprintf(value, sizeof(value), "%d秒", m->settings.sleep_seconds);
                else snprintf(value, sizeof(value), "不休眠");
            }
            text(&collection_font_16, value, 214, y + 20, NT_ALIGN_RIGHT, active ? INK : PAPER);
        }
        text(&collection_font_16, m->editing >= 0 ? "长按上取消本次调整" : "长按上返回首页", 120, 305, NT_ALIGN_CENTER, PAPER);
    } else {
        char value[48];
        text(&collection_font_18, "NABO 牌桌 · 对决", 120, 94, NT_ALIGN_CENTER, PAPER);
        text(&nabo_mono_font_12, "v1.0.0", 120, 119, NT_ALIGN_CENTER, GOLD);
        snprintf(value, sizeof(value), battery >= 0 ? "电量 %d%%" : "电量暂不可用", battery);
        text(&collection_font_18, value, 120, 157, NT_ALIGN_CENTER, PAPER);
        snprintf(value, sizeof(value), mv >= 0 ? "%d mV" : "-- mV", mv);
        text(&nabo_mono_font_12, value, 120, 182, NT_ALIGN_CENTER, GOLD);
        text(&collection_font_16, "无需配网和激活", 120, 223, NT_ALIGN_CENTER, PAPER);
        text(&collection_font_16, "对决用 ESP-NOW 直连", 120, 251, NT_ALIGN_CENTER, PAPER);
        text(&collection_font_16, "按任意键返回设置", 120, 305, NT_ALIGN_CENTER, PAPER);
    }
    ntg_flush(s_gfx);
    s_capture = false;
}

