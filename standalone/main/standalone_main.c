// NABO TABLE · DUEL offline firmware: launcher, NABO 牌桌 and NABO 对决 without XiaoZhi.
// Derived from the NABO game collection launcher. Game PCM, page lifetimes and codec I/O share one owner lock.
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "collection_model.h"
#include "collection_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nabo_table.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "NABO-Offline"
#define CHUNK 320
extern const uint8_t table_pack_start[] asm("_binary_table_sprites_bin_start");
extern const uint8_t table_pack_end[] asm("_binary_table_sprites_bin_end");
extern const uint8_t duel_pack_start[] asm("_binary_duel_sprites_bin_start");
extern const uint8_t duel_pack_end[] asm("_binary_duel_sprites_bin_end");
typedef struct { bool command; int button, event; uint32_t ms; char line[96]; } input_t;
static QueueHandle_t s_inputs;
static SemaphoreHandle_t s_audio_lock;
static cg_model_t s_model;
static cg_page_t s_audio_page = CG_HOME;
static bool s_audio_ok;
static atomic_bool s_overflow;
static unsigned s_held, s_swallowed;
static int64_t s_last_input_us;

static void report_error(const char *what, esp_err_t err) {
    if (err != ESP_OK) ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
}

static cg_settings_t load_settings(void) {
    cg_settings_t s = {.volume = 70, .brightness = 80, .sleep_seconds = 300, .speaker = true};
    nvs_handle_t h;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        int32_t v; uint8_t b;
        if (nvs_get_i32(h, "volume", &v) == ESP_OK) s.volume = v;
        if (nvs_get_i32(h, "brightness", &v) == ESP_OK) s.brightness = v;
        if (nvs_get_i32(h, "sleep", &v) == ESP_OK) s.sleep_seconds = v;
        if (nvs_get_u8(h, "speaker", &b) == ESP_OK) s.speaker = b != 0;
        nvs_close(h);
    }
    cg_sanitize(&s);
    return s;
}

static void save_settings(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "volume", s_model.settings.volume);
        if (!err) err = nvs_set_i32(h, "brightness", s_model.settings.brightness);
        if (!err) err = nvs_set_i32(h, "sleep", s_model.settings.sleep_seconds);
        if (!err) err = nvs_set_u8(h, "speaker", s_model.settings.speaker);
        if (!err) err = nvs_commit(h);
        nvs_close(h);
    }
    report_error("settings save", err);
}

static void apply_settings(void) {
    xSemaphoreTake(s_audio_lock, portMAX_DELAY);
    if (s_audio_ok) bsp_audio_set_volume(s_model.settings.speaker ? s_model.settings.volume : 0);
    xSemaphoreGive(s_audio_lock);
    bsp_display_backlight(s_model.settings.brightness);
}

static void close_page(cg_page_t page) {
    if (page == CG_TABLE) nabo_table_close();
    else cg_ui_close();
}

static void switch_page(cg_page_t old) {
    // No audio fill can observe freed game state or continue across a page switch.
    xSemaphoreTake(s_audio_lock, portMAX_DELAY);
    close_page(old);
    bool ok;
    if (s_model.page == CG_TABLE || s_model.page == CG_DUEL) {
        nabo_table_start_on(s_model.page == CG_DUEL ? NABO_TABLE_START_DUEL : NABO_TABLE_START_COVER);
        nabo_table_set_duel_pack(duel_pack_start, duel_pack_end - duel_pack_start);
        s_model.page = CG_TABLE;   // the duel lives inside the table
        ok = nabo_table_open(bsp_display_io(), bsp_display_panel(), esp_random(), table_pack_start, table_pack_end - table_pack_start);
    } else {
        ok = cg_ui_open();
    }
    if (!ok) {
        ESP_LOGE(TAG, "page %d failed to open; returning to launcher", s_model.page);
        s_model.page = CG_HOME;
        if (!cg_ui_open()) { xSemaphoreGive(s_audio_lock); esp_restart(); return; }
    }
    s_audio_page = s_model.page;
    s_swallowed |= s_held;
    xSemaphoreGive(s_audio_lock);
    ESP_LOGI(TAG, "page=%d heap=%u", s_model.page, (unsigned)esp_get_free_heap_size());
}

static void menu_key(cg_key_t key) {
    cg_page_t old = s_model.page;
    int result = cg_key(&s_model, key);
    if (result & CG_PAGE_CHANGED) switch_page(old);
    if (result & CG_CHANGED) apply_settings();
    if (result & CG_SAVE) save_settings();
}

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    input_t in = {.button = button, .event = event, .ms = (uint32_t)(esp_timer_get_time() / 1000)};
    if (xQueueSend(s_inputs, &in, 0) != pdTRUE) atomic_store(&s_overflow, true);
}

static void handle_button(const input_t *in) {
    s_last_input_us = esp_timer_get_time();
    unsigned bit = 1u << in->button;
    if (in->event == BSP_BTN_PRESS) { s_held |= bit; s_swallowed &= ~bit; }
    if (in->event == BSP_BTN_RELEASE) s_held &= ~bit;
    if (s_swallowed & bit) return;
    cg_page_t page = s_model.page;
    if (in->button == BSP_BTN_UP && in->event == BSP_BTN_LONG &&
        (page != CG_TABLE || nabo_table_on_cover())) {
        s_swallowed |= bit;
        menu_key(CG_BACK);
        return;
    }
    if (page == CG_TABLE) {
        if (in->event == BSP_BTN_PRESS || in->event == BSP_BTN_RELEASE)
            nabo_table_button(in->button, in->event == BSP_BTN_PRESS, in->ms);
        else if (in->button == BSP_BTN_UP && in->event == BSP_BTN_DOUBLE && nabo_table_on_cover()) menu_key(CG_BACK);
    } else if (in->event == BSP_BTN_CLICK)
        menu_key(in->button == BSP_BTN_UP ? CG_UP : in->button == BSP_BTN_DOWN ? CG_DOWN : CG_OK);
}

static void status(void) {
    printf("@games {\"page\":%d,\"volume\":%d,\"brightness\":%d,\"sleep\":%d,\"speaker\":%d,\"heap\":%u,\"largest\":%u}\n",
           s_model.page, s_model.settings.volume, s_model.settings.brightness, s_model.settings.sleep_seconds, s_model.settings.speaker,
           (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

// The same commands as the XiaoZhi build, so tools/verify_duel.py drives both.
static void open_page(cg_page_t next) {
    cg_page_t old = s_model.page;
    s_model.page = next;
    switch_page(old);
}

static void command(const char *line) {
    if (!strcmp(line, ">games.status")) status();
    else if (!strncmp(line, ">games.open=", 12)) {
        const char *name = line + 12;
        int next = !strcmp(name, "home") ? CG_HOME : !strcmp(name, "table") ? CG_TABLE : !strcmp(name, "duel") ? CG_DUEL :
                   !strcmp(name, "settings") ? CG_SETTINGS : !strcmp(name, "status") ? CG_STATUS : -1;
        if (next < 0) { puts("@error unknown page"); return; }
        open_page((cg_page_t)next);
    } else if (!strncmp(line, ">games.key=", 11)) {
        const char *name = line + 11;
        int key = !strcmp(name, "up") ? 0 : !strcmp(name, "down") ? 1 : !strcmp(name, "ok") ? 2 : !strcmp(name, "back") ? 3 : -1;
        if (key < 0) { puts("@error unknown key"); return; }
        if (key == 3) menu_key(CG_BACK);
        else if (s_model.page == CG_TABLE) nabo_table_inject(key);
        else menu_key(key == 0 ? CG_UP : key == 1 ? CG_DOWN : CG_OK);
    } else if (!strncmp(line, ">games.edge=", 12)) {
        int button, pressed;
        if (sscanf(line + 12, "%d,%d", &button, &pressed) == 2 && button >= 0 && button <= 2 && (pressed == 0 || pressed == 1)) {
            input_t in = {.button = button, .event = pressed ? BSP_BTN_PRESS : BSP_BTN_RELEASE, .ms = (uint32_t)(esp_timer_get_time() / 1000)};
            handle_button(&in);
        }
    } else if (!strcmp(line, ">games.capture")) {
        if (s_model.page == CG_TABLE) nabo_table_capture();
        else cg_ui_capture();
    } else if (!strcmp(line, ">table.status")) {
        char out[1024]; nabo_table_status(out, sizeof(out)); printf("@table %s\n", out);
    } else if (!strncmp(line, ">table.key=", 11)) {
        int key; if (sscanf(line + 11, "%d", &key) == 1 && key >= 0 && key <= 3) nabo_table_inject(key);
    } else if (!strncmp(line, ">apps.open=", 11)) {
        const char *name = line + 11;
        open_page(!strcmp(name, "duel") ? CG_DUEL : !strcmp(name, "table") ? CG_TABLE : CG_HOME);
    } else if (!strcmp(line, ">apps.status")) {
        printf("@apps {\"page\":%d,\"heap\":%u,\"wifi\":0,\"offline\":1}\n", s_model.page, (unsigned)esp_get_free_heap_size());
    } else if (!strncmp(line, ">table.press=", 13)) {
        static const char *const names[] = {"up", "down", "ok", "long"};
        for (int k = 0; k < 4; k++)
            if (!strcmp(line + 13, names[k])) nabo_table_inject(k);
    } else if (!strcmp(line, ">table.capture")) {
        nabo_table_capture();
    } else if (!strcmp(line, ">table.duel.test")) {
        nabo_table_duel_test(true);
        puts("@duel test mode");
    } else puts("@error unknown command");
}

static void serial_task(void *arg) {
    (void)arg;
    char line[96]; size_t used = 0;
    for (;;) {
        int c = getchar();
        if (c == EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c == '\n' || c == '\r') {
            line[used] = 0;
            if (used && line[0] == '>') {
                input_t in = {.command = true}; memcpy(in.line, line, used + 1);
                if (xQueueSend(s_inputs, &in, 0) != pdTRUE) puts("@error command queue full");
            }
            used = 0;
        } else if (used + 1 < sizeof(line)) line[used++] = (char)c;
        else used = 0;
    }
}

static void audio_task(void *arg) {
    (void)arg;
    int16_t pcm[CHUNK];
    for (;;) {
        xSemaphoreTake(s_audio_lock, portMAX_DELAY);
        memset(pcm, 0, sizeof(pcm));
        if (s_audio_page == CG_TABLE) nabo_table_audio_fill(pcm, CHUNK);
        bool delay = !s_audio_ok || bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK;
        xSemaphoreGive(s_audio_lock);
        // A higher-priority producer must block after giving the mutex so a
        // waiting UI task can acquire it before the next PCM chunk. One tick
        // fits inside the I2S DMA buffer; taskYIELD alone cannot run that task.
        vTaskDelay(delay ? pdMS_TO_TICKS(20) : 1);
    }
}

static void sleep_now(void) {
    if (s_held || bsp_button_read_mv() < BSP_BTN_MV_RELEASED_MIN) return;
    xSemaphoreTake(s_audio_lock, portMAX_DELAY);
    close_page(s_model.page);
    bsp_display_backlight(0);
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK || !level) esp_restart();
    report_error("wake GPIO", esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));
    report_error("battery sleep", bsp_battery_sleep());
    if (s_audio_ok) {
        report_error("codec sleep", bsp_audio_sleep());
        report_error("I2S sleep", bsp_audio_prepare_deep_sleep());
    }
    report_error("I2C sleep", bsp_i2c_prepare_deep_sleep());
    report_error("panel sleep", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    esp_restart();
}

static void collection_task(void *arg) {
    (void)arg;
    int battery = -1, mv = -1;
    int64_t battery_at = 0;
    for (;;) {
        if (atomic_exchange(&s_overflow, false)) {
            ESP_LOGE(TAG, "input overflow; cancelling held actions at launcher");
            xQueueReset(s_inputs); s_held = 0; s_swallowed = 7;
            cg_page_t old = s_model.page; s_model.page = CG_HOME; switch_page(old);
        }
        input_t input;
        while (xQueueReceive(s_inputs, &input, 0) == pdTRUE) {
            if (input.command) command(input.line); else handle_button(&input);
        }
        int64_t us = esp_timer_get_time();
        uint32_t ms = (uint32_t)(us / 1000);
        if (us - battery_at >= 5000000) { battery = bsp_battery_soc(); mv = bsp_battery_mv(); battery_at = us; }
        if (s_model.page != CG_TABLE) cg_ui_draw(&s_model, ms, battery, mv);
        if (s_model.settings.sleep_seconds > 0 && us - s_last_input_us > (int64_t)s_model.settings.sleep_seconds * 1000000) sleep_now();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    // Preserve old settings/records. Never erase NVS to mask an initialization error.
    ESP_ERROR_CHECK(nvs_flash_init());
    s_inputs = xQueueCreate(32, sizeof(input_t));
    s_audio_lock = xSemaphoreCreateMutex();
    if (!s_inputs || !s_audio_lock) { ESP_LOGE(TAG, "startup allocation failed"); return; }
    cg_settings_t settings = load_settings(); cg_init(&s_model, &settings);
    ESP_ERROR_CHECK(bsp_display_init());
    report_error("battery", bsp_battery_init());
    s_audio_ok = bsp_audio_init() == ESP_OK && bsp_audio_set_format(16000, 16, 1) == ESP_OK;
    if (!s_audio_ok) ESP_LOGW(TAG, "codec unavailable; games will run silently");
    if (!cg_ui_open()) { ESP_LOGE(TAG, "launcher allocation failed"); return; }
    s_last_input_us = esp_timer_get_time();
    cg_ui_draw(&s_model, (uint32_t)(s_last_input_us / 1000), -1, -1);
    apply_settings();
    if (xTaskCreate(audio_task, "nabo_audio", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(collection_task, "nabo_ui", 10240, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(serial_task, "nabo_serial", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task allocation failed"); esp_restart();
    }
    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    ESP_LOGI(TAG, "offline table + duel ready; heap=%u", (unsigned)esp_get_free_heap_size());
}
