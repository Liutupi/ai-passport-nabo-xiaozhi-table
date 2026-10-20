#include "nabo_apps.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "application.h"
#include "assets.h"
#include "backlight.h"
#include "board.h"
#include "codecs/es8311_audio_codec.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "mcp_server.h"
#include "nabo_display.h"
#include "nvs.h"
#include "settings.h"
#include "src/draw/lv_image_decoder_private.h"
#include "ssid_manager.h"
#include "wifi_manager.h"
extern "C" {
#include "nabo_jump_model.h"
#include "nabo_jump_sound.h"
#include "nabo_table.h"
#include "nabo_voice_commands.h"
LV_FONT_DECLARE(nabo_menu_font_16);
LV_FONT_DECLARE(nabo_mono_font_12);
}
#include "nabo_menu_view.h"

static std::atomic<int> wanted{NABO_HOME}, request_revision{0}, setting_command{-1};
static std::atomic<int> detail_parent{NABO_SETTINGS};
static std::atomic<int64_t> open_after{0}, last_input{0};
static std::atomic<bool> ready{false}, screen_off{false}, speaker{true}, wifi_on{true};
static std::atomic<int> volume{70}, brightness{80}, sleep_seconds{120}, mic_gain{30}, battery{-1};
static std::atomic<uint32_t> best_score{0};
static std::atomic<bool> best_dirty{false};
static std::atomic<int64_t> best_changed_at{0};
static std::atomic<int> jump_sound{0};
static lv_obj_t* app_screen;
static NaboApp page = NABO_HOME;
static int selected = 1, settings_selected = 1, edit = -1, shown_revision = -1;
static bool initialized = false, engaged = false, restored = true;
static int64_t last_battery = 0;
static char notice[96] = "";
static nabo_jump_t jump_game, jump_frame;
static uint32_t jump_frame_time;
static std::atomic<uint32_t> jump_frames{0}, jump_fps{0};
struct JumpTiming {
    uint32_t frames = 0, intervals = 0, max_us = 0, slow = 0;
    uint64_t sum_us = 0;
    uint64_t draw_us = 0;
    uint32_t max_draw_us = 0;
};
static JumpTiming jump_timing[6];
static uint32_t jump_draw_us = 0;
static int64_t jump_previous_flush = 0;
static jump_phase_t jump_previous_phase = JUMP_READY;
static bool jump_test_mode = false;
static std::atomic<bool> jump_round_over{false};
void nabo_apps_record_frame() {
    if (nabo_apps_is_jump()) {
        jump_frames.fetch_add(1);
        auto phase = jump_frame.phase;
        auto& timing = jump_timing[phase];
        ++timing.frames;
        timing.draw_us += jump_draw_us;
        timing.max_draw_us = std::max(timing.max_draw_us, jump_draw_us);
        jump_draw_us = 0;
        int64_t now = esp_timer_get_time();
        if (jump_previous_flush && phase == jump_previous_phase) {
            uint32_t elapsed = now - jump_previous_flush;
            ++timing.intervals;
            timing.sum_us += elapsed;
            timing.max_us = std::max(timing.max_us, elapsed);
            if (elapsed > 50000)
                ++timing.slow;
        }
        jump_previous_flush = now;
        jump_previous_phase = phase;
        // At 100 Hz the LVGL port otherwise sleeps twice for at least one tick
        // after a long scroll redraw. Keep its display event pending so the
        // next due frame can run after the normal yield, without that extra wait.
        if (phase == JUMP_SCROLLING)
            lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);
    }
}
static lv_obj_t *jump_panel, *jump_image, *jump_score, *jump_best, *jump_station, *jump_hint;
static lv_image_dsc_t jump_source = {};
static lv_draw_buf_t* jump_strip;
static int64_t jump_last_frame;

static const char* tools[] = {"返回 NABO", "跳一跳", "NABO 牌桌", "更多设置"};
static constexpr int kToolCount = sizeof(tools) / sizeof(*tools);
static const NaboApp tool_apps[] = {NABO_HOME, NABO_JUMP, NABO_TABLE, NABO_SETTINGS};
enum Setting {
    ST_BACK,
    ST_VOLUME,
    ST_SPEAKER,
    ST_BRIGHTNESS,
    ST_SLEEP,
    ST_MIC,
    ST_WIFI_ADD,
    ST_WIFI,
    ST_STATE,
    ST_BATTERY,
    ST_SCREEN,
    ST_POWER,
    ST_COUNT
};
static const char* settings_names[] = {"返回",     "音量",       "扬声器",   "屏幕亮度",
                                       "自动休眠", "麦克风增益", "添加网络", "无线网络",
                                       "设备状态", "电池信息",   "关闭屏幕", "关机"};
extern "C" bool muse_settings_speaker_on(void) { return speaker.load(); }
extern "C" bool muse_wifi_connected(void) {
    return wifi_on.load() && WifiManager::GetInstance().IsConnected();
}

void nabo_apps_open(NaboApp app, int delay_ms) {
    if (app < NABO_HOME || app > NABO_WIFI)
        return;
    int origin = wanted.load();
    if ((app == NABO_STATUS || app == NABO_BATTERY || app == NABO_WIFI) && origin != app)
        detail_parent = origin == NABO_HOME       ? NABO_HOME
                        : origin == NABO_SETTINGS ? NABO_SETTINGS
                                                  : NABO_TOOLS;
    open_after.store(esp_timer_get_time() + static_cast<int64_t>(delay_ms) * 1000);
    wanted.store(app);
    request_revision.fetch_add(1);
    last_input.store(esp_timer_get_time());
}
bool nabo_apps_active() { return wanted.load() != NABO_HOME || ready.load(); }
bool nabo_apps_is_jump() { return wanted.load() == NABO_JUMP && ready.load(); }
bool nabo_apps_is_fast() {
    return wanted.load() == NABO_JUMP && ready.load();
}

static void save_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "volume", volume.load());
        if (!err)
            err = nvs_set_i32(h, "brightness", brightness.load());
        if (!err)
            err = nvs_set_i32(h, "sleep", sleep_seconds.load());
        if (!err)
            err = nvs_set_i32(h, "mic", mic_gain.load());
        if (!err)
            err = nvs_set_u8(h, "speaker", speaker.load());
        if (!err)
            err = nvs_set_u8(h, "wifi", wifi_on.load());
        if (!err)
            err = nvs_commit(h);
        nvs_close(h);
    }
    if (err) {
        snprintf(notice, sizeof(notice), "设置未保存，请重试");
        ESP_LOGE("NABO-Apps", "settings save: %d", err);
    }
}
static void load_settings() {
    auto& board = Board::GetInstance();
    volume = board.GetAudioCodec()->output_volume();
    brightness = board.GetBacklight()->brightness();
    nvs_handle_t h;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        int32_t v;
        uint8_t flag;
        if (!nvs_get_i32(h, "volume", &v))
            volume = std::clamp<int>(v, 0, 100);
        if (!nvs_get_i32(h, "brightness", &v))
            brightness = std::clamp<int>(v, 10, 100);
        if (!nvs_get_i32(h, "sleep", &v))
            sleep_seconds = std::clamp<int>(v, 0, 600);
        if (!nvs_get_i32(h, "mic", &v))
            mic_gain = std::clamp<int>(v, 0, 36);
        if (!nvs_get_u8(h, "speaker", &flag))
            speaker = flag != 0;
        if (!nvs_get_u8(h, "wifi", &flag))
            wifi_on = flag != 0;
        uint32_t score;
        if (!nvs_get_u32(h, "jump_best", &score))
            best_score = score;
        nvs_close(h);
    }
    board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
    board.GetAudioCodec()->SetInputGain(mic_gain.load());
    board.GetBacklight()->SetBrightness(brightness.load(), false);
}
static void output16(const int16_t* input, size_t count) {
    if (!speaker.load() || !ready.load() || !count)
        return;
    auto codec = Board::GetInstance().GetAudioCodec();
    static std::vector<int16_t> pcm;
    pcm.assign(input, input + count);
    codec->EnableOutput(true);
    codec->OutputData(pcm);
}
static void apply_setting(int id) {
    auto& board = Board::GetInstance();
    switch (id) {
        case ST_VOLUME:
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_SPEAKER:
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_BRIGHTNESS:
            board.GetBacklight()->SetBrightness(brightness.load(), true);
            break;
        case ST_MIC:
            board.GetAudioCodec()->SetInputGain(mic_gain.load());
            break;
        case ST_WIFI:
            if (wifi_on)
                WifiManager::GetInstance().StartStation();
            else
                WifiManager::GetInstance().StopStation();
            break;
        case ST_WIFI_ADD:
            WifiManager::GetInstance().StartConfigAp();
            break;
        case ST_SCREEN:
            screen_off = true;
            board.GetBacklight()->SetBrightness(0, false);
            break;
        case ST_POWER:
            save_settings();
            board.GetBacklight()->SetBrightness(0, false);
            board.GetAudioCodec()->EnableInput(false);
            board.GetAudioCodec()->EnableOutput(false);
            esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << 0,
                                                                ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
            break;
        default:
            break;
    }
    save_settings();
}
static void diagnostic(const char* line) {
    auto& app = Application::GetInstance();
    if (!strcmp(line, ">apps.status")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        printf(
            "\n@apps "
            "{\"wanted\":%d,\"page\":%d,\"ready\":%d,\"local\":%d,\"heap\":%u,\"table\":%d,"
            "\"fps\":%lu,\"volume\":%d,\"brightness\":%d,\"sleep\":%d,\"score\":%lu,\"best\":%lu,"
            "\"wifi\":%d,\"phase\":%d,\"gap\":%d,\"queued\":%d,\"holding\":%d,\"held_ms\":%lu,"
            "\"distance\":%d,\"edit\":%d,\"detail_parent\":%d}"
            "\n",
            wanted.load(), (int)page, ready.load(), app.IsLocalAppActive(),
            (unsigned)esp_get_free_heap_size(), nabo_table_running(),
            (unsigned long)jump_fps.load(), volume.load(), brightness.load(), sleep_seconds.load(),
            (unsigned long)jump_game.score, (unsigned long)best_score.load(),
            WifiManager::GetInstance().IsConnected(), (int)jump_game.phase, jump_game.gap,
            jump_game.pending_charge, nabo_jump_charging(&jump_game),
            (unsigned long)nabo_jump_charge_ms(&jump_game, esp_timer_get_time() / 1000),
            jump_game.distance, edit, detail_parent.load());
    } else if (!strcmp(line, ">jump.metrics")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        for (int phase = 0; phase < 6; ++phase) {
            const auto& timing = jump_timing[phase];
            printf(
                "\n@jump-timing "
                "{\"phase\":%d,\"frames\":%lu,\"intervals\":%lu,\"mean_us\":%lu,\"max_us\":%lu,"
                "\"over_50ms\":%lu,\"mean_draw_us\":%lu,\"max_draw_us\":%lu}\n",
                phase, (unsigned long)timing.frames, (unsigned long)timing.intervals,
                (unsigned long)(timing.intervals ? timing.sum_us / timing.intervals : 0),
                (unsigned long)timing.max_us, (unsigned long)timing.slow,
                (unsigned long)(timing.frames ? timing.draw_us / timing.frames : 0),
                (unsigned long)timing.max_draw_us);
        }
    } else if (!strcmp(line, ">jump.metrics.reset")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        for (auto& timing : jump_timing)
            timing = {};
        jump_previous_flush = 0;
    } else if (!strcmp(line, ">jump.test=1") || !strcmp(line, ">jump.test=0")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        jump_test_mode = line[11] == '1';
        jump_game.best = best_score.load();
    } else if (!strncmp(line, ">jump.best=", 11)) {
        char* end;
        unsigned long restored_best = strtoul(line + 11, &end, 10);
        if (*end || restored_best > 1000000)
            return;
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        best_score = restored_best;
        jump_game.best = restored_best;
        best_dirty = true;
        best_changed_at = esp_timer_get_time();
    } else if (!strncmp(line, ">apps.open=", 11)) {
        const char* name = line + 11;
        struct {
            const char* name;
            NaboApp app;
        } aliases[] = {{"home", NABO_HOME},     {"tools", NABO_TOOLS}, {"settings", NABO_SETTINGS},
                       {"table", NABO_TABLE},   {"jump", NABO_JUMP},   {"status", NABO_STATUS},
                       {"undercover", NABO_TABLE}, {"duel", NABO_TABLE}};
        bool found = false;
        for (const auto& item : aliases)
            if (!strcmp(name, item.name)) {
                if (!strcmp(name, "undercover"))
                    nabo_table_start_on(NABO_TABLE_START_UNDERCOVER);
                if (!strcmp(name, "duel"))
                    nabo_table_start_on(NABO_TABLE_START_DUEL);
                nabo_apps_open(item.app);
                found = true;
                break;
            }
        printf("\n@open %s\n", found ? "queued" : "invalid");
    } else if (!strncmp(line, ">apps.voice=", 12)) {
        const char* name = line + 12;
        if (strlen(name) > 20 || strspn(name, "abcdefghijklmnopqrstuvwxyz") != strlen(name))
            return;
        std::string message =
            "{\"jsonrpc\":\"2.0\",\"id\":919,\"method\":\"tools/"
            "call\",\"params\":{\"name\":\"self.apps.open\",\"arguments\":{\"app\":\"" +
            std::string(name) + "\"}}}";
        app.Schedule([message]() {
            McpServer::GetInstance().ParseMessage(message, [](const std::string& response) {
                printf("\n@mcp %s\n", response.c_str());
            });
        });
    } else if (!strncmp(line, ">apps.key=", 10)) {
        muse_menu_key_t key = MUSE_MENU_SELECT;
        if (!strcmp(line + 10, "up"))
            key = MUSE_MENU_UP;
        else if (!strcmp(line + 10, "down"))
            key = MUSE_MENU_DOWN;
        else if (!strcmp(line + 10, "back"))
            key = MUSE_MENU_BACK;
        else if (!strcmp(line + 10, "parent"))
            key = MUSE_MENU_LEFT;
        else if (strcmp(line + 10, "ok"))
            return;
        app.Schedule([key]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            nabo_apps_key(key);
        });
    } else if (!strncmp(line, ">jump.hold=", 11)) {
        char* end;
        long hold = strtol(line + 11, &end, 10);
        if (*end || hold < 0 || hold > 1400)
            return;
        app.Schedule([hold]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            if (nabo_apps_is_jump()) {
                uint32_t now = esp_timer_get_time() / 1000;
                nabo_jump_press(&jump_game, now - hold);
                nabo_jump_release(&jump_game, now);
                jump_sound = 1;
            }
        });
    } else if (!strncmp(line, ">table.press=", 13)) {
        static const char* names[] = {"up", "down", "ok", "long"};
        for (int k = 0; k < 4; ++k)
            if (!strcmp(line + 13, names[k]))
                nabo_table_inject(k);
    } else if (!strcmp(line, ">table.capture")) {
        nabo_table_capture();
    } else if (!strcmp(line, ">table.duel.test")) {
        nabo_table_duel_test(true);   // scripted duel: never touch the record or the saved settings
        printf("\n@duel test mode\n");
    } else if (!strcmp(line, ">table.status")) {
        static char text[1024];   // with the duel block (serial task only)
        nabo_table_status(text, sizeof(text));
        printf("\n@table %s\n", text);
    } else if (!strcmp(line, ">apps.capture")) {
        app.Schedule(
            []() { static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->CaptureLCD(); });
    } else if (!strcmp(line, ">jump.press") || !strcmp(line, ">jump.release")) {
        bool pressed = !strcmp(line, ">jump.press");
        app.Schedule([pressed]() {
            DisplayLockGuard lock(Board::GetInstance().GetDisplay());
            nabo_apps_charge(pressed);
        });
    }
}
static void worker(void*) {
    vTaskDelay(pdMS_TO_TICKS(350));
    load_settings();
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = 2048, .rx_buffer_size = 512};
        usb_serial_jtag_driver_install(&cfg);
    }
    usb_serial_jtag_vfs_use_driver();
    char command_line[96] = {};
    unsigned command_len = 0;
    int16_t samples[320];
    bool usb_attached = false;
    nabo_jump_sound_t jump_audio = {};
    int64_t measured_at = esp_timer_get_time();
    uint32_t measured_frames = 0;
    for (;;) {
        char data[64];
        int received = usb_serial_jtag_read_bytes(data, sizeof(data), 0);
        for (int i = 0; i < received; ++i) {
            char c = data[i];
            if (c == '\n' || c == '\r') {
                command_line[command_len] = 0;
                if (command_len)
                    diagnostic(command_line);
                command_len = 0;
            } else if (command_len < sizeof(command_line) - 1)
                command_line[command_len++] = c;
            else
                command_len = 0;
        }
        auto& app = Application::GetInstance();
        int desired = wanted.load();
        int64_t now = esp_timer_get_time();
        if (desired != NABO_HOME && now >= open_after.load() && !engaged &&
            !app.IsLocalAppActive() && !app.IsProtocolInitializing() &&
            app.GetDeviceState() != kDeviceStateActivating) {
        // Starting (no network yet) is fine: the programs need no network, and XiaoZhi
        // resumes its boot flow when the program closes.
            engaged = true;
            restored = false;
            app.BeginLocalApp();
        }
        if (engaged && !ready.load() && app.IsLocalAppActive()) {
            if (app.GetAudioService().WaitStopped(2000)) {
                vTaskDelay(pdMS_TO_TICKS(30));
                auto codec = static_cast<Es8311AudioCodec*>(Board::GetInstance().GetAudioCodec());
                codec->EnableInput(false);
                codec->EnableOutput(false);
                if (!codec->SetDuplexSampleRate(16000)) {
                    nabo_apps_open(NABO_HOME);
                    continue;
                }
                ready = true;
                ESP_LOGI("NABO-Apps", "local audio ownership ready, heap=%u",
                         (unsigned)esp_get_free_heap_size());
            } else {
                ESP_LOGE("NABO-Apps", "audio stop timeout; local playback withheld");
                nabo_apps_open(NABO_HOME);
            }
        }
        bool audio_clocked = false;
        if (ready.load()) {
            if (desired != NABO_JUMP)
                static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())
                    ->ReleaseGameDrawBuffer();
            memset(samples, 0, sizeof(samples));
            int tone = jump_sound.exchange(0);
            if (tone)
                nabo_jump_sound_start(&jump_audio, tone, 1);
            if (desired == NABO_JUMP)
                nabo_jump_sound_fill(&jump_audio, samples, 320);
            else
                nabo_jump_sound_start(&jump_audio, 0, 0);
            if (desired == NABO_TABLE)
                nabo_table_audio_fill(samples, 320);
            // A continuous 20 ms clock prevents underruns and clears each effect's
            // DMA tail. Do not sleep again after the blocking I2S write.
            if (desired != NABO_HOME && speaker.load()) {
                output16(samples, 320);
                audio_clocked = true;
            }
            // The table frees its buffers on the LVGL task; XiaoZhi's restore needs that memory back first.
            if (desired == NABO_HOME && !nabo_table_running()) {
                Board::GetInstance().GetAudioCodec()->EnableOutput(false);
                ready = false;
                engaged = false;
                if (!restored) {
                    restored = true;
                    app.EndLocalApp();
                }
            }
        }
        int command = setting_command.exchange(-1);
        if (command >= 0)
            apply_setting(command);
        if (best_dirty.load() &&
            (desired != NABO_JUMP ||
             (jump_round_over.load() && now - best_changed_at.load() >= 2000000)) &&
            best_dirty.exchange(false)) {
            nvs_handle_t h;
            if (!nvs_open("nabo_ui", NVS_READWRITE, &h)) {
                nvs_set_u32(h, "jump_best", best_score.load());
                nvs_commit(h);
                nvs_close(h);
            }
        }
        if (now - last_battery > 3000000) {
            int pct;
            bool charging, discharging;
            if (Board::GetInstance().GetBatteryLevel(pct, charging, discharging))
                battery = pct;
            last_battery = now;
        }
        if (now - measured_at >= 5000000) {
            uint32_t frames = jump_frames.load();
            jump_fps = (frames - measured_frames) * 1000000ULL / (now - measured_at);
            if (desired == NABO_JUMP)
                ESP_LOGI("NABO-Jump", "display refresh fps=%lu", (unsigned long)jump_fps.load());
            else if (desired == NABO_HOME && !app.IsLocalAppActive() &&
                     !app.IsProtocolInitializing())
                app.GetAudioService().LogMemoryStats();
            measured_frames = frames;
            measured_at = now;
        }
        bool usb = usb_serial_jtag_is_connected();
        if (usb != usb_attached) {
            usb_attached = usb;
            last_input.store(now);
            if (usb && screen_off.exchange(false))
                Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        }
        if (!usb && wanted.load() == NABO_HOME && app.GetDeviceState() == kDeviceStateIdle &&
            !screen_off && sleep_seconds > 0 &&
            now - last_input.load() > sleep_seconds.load() * 1000000LL) {
            screen_off = true;
            Board::GetInstance().GetBacklight()->SetBrightness(0, false);
        }
        if (!audio_clocked)
            vTaskDelay(pdMS_TO_TICKS(20));
        else
            taskYIELD();
    }
}
void nabo_apps_initialize() {
    if (initialized)
        return;
    initialized = true;
    last_input = esp_timer_get_time();
    if (xTaskCreate(worker, "nabo_apps", 6144, nullptr, 5, nullptr) != pdPASS) {
        initialized = false;
        ESP_LOGE("NABO-Apps", "worker allocation failed");
    }
}

static lv_obj_t* label(lv_obj_t* parent, uint32_t color, const char* text, int x, int y,
                       int width = 220) {
    auto obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, &nabo_menu_font_16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_text(obj, text);
    return obj;
}
static void show_text(lv_obj_t* obj, const char* text) {
    if (strcmp(lv_label_get_text(obj), text))
        lv_label_set_text(obj, text);
}
static lv_result_t jump_info(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc,
                             lv_image_header_t* header) {
    if (dsc->src != &jump_source)
        return LV_RESULT_INVALID;
    *header = jump_source.header;
    return LV_RESULT_OK;
}
static lv_result_t jump_open(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc) {
    if (dsc->src != &jump_source || !jump_strip)
        return LV_RESULT_INVALID;
    dsc->args.no_cache = true;
    dsc->decoded = nullptr;
    return LV_RESULT_OK;
}
static lv_result_t jump_area(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc,
                             const lv_area_t* full, lv_area_t* area) {
    if (full->x1 < 0 || full->x2 >= 240 || full->y1 < 0 || full->y2 >= 320)
        return LV_RESULT_INVALID;
    int y = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y > full->y2)
        return LV_RESULT_INVALID;
    int end = std::min(y + 19, static_cast<int>(full->y2));
    auto buf = lv_draw_buf_reshape(jump_strip, LV_COLOR_FORMAT_RGB565, full->x2 - full->x1 + 1,
                                   end - y + 1, LV_STRIDE_AUTO);
    if (!buf)
        return LV_RESULT_INVALID;
    int64_t draw_start = esp_timer_get_time();
    for (int row = y; row <= end; ++row)
        nabo_jump_draw_row(
            &jump_frame, jump_frame_time, row, full->x1, full->x2,
            reinterpret_cast<uint16_t*>(buf->data) + (row - y) * buf->header.stride / 2);
    jump_draw_us += esp_timer_get_time() - draw_start;
    *area = {full->x1, y, full->x2, end};
    dsc->decoded = buf;
    return LV_RESULT_OK;
}
void nabo_apps_build(lv_obj_t* screen) {
    if (app_screen)
        return;
    app_screen = screen;
    jump_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(jump_panel);
    lv_obj_set_size(jump_panel, 240, 320);
    lv_obj_set_style_bg_color(jump_panel, lv_color_hex(0xdfe1eb), 0);
    lv_obj_set_style_bg_opa(jump_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(jump_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
    jump_image = lv_image_create(jump_panel);
    lv_obj_set_size(jump_image, 240, 320);
    jump_source.header.magic = LV_IMAGE_HEADER_MAGIC;
    jump_source.header.cf = LV_COLOR_FORMAT_RGB565;
    jump_source.header.w = 240;
    jump_source.header.h = 320;
    jump_source.header.stride = 480;
    jump_source.data = reinterpret_cast<const uint8_t*>(&jump_source);
    auto decoder = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(decoder, jump_info);
    lv_image_decoder_set_open_cb(decoder, jump_open);
    lv_image_decoder_set_get_area_cb(decoder, jump_area);
    jump_score = label(jump_panel, 0x344251, "0", 16, 14, 112);
    jump_best = label(jump_panel, 0xffffff, "BEST 0", 147, 17, 79);
    lv_obj_set_style_text_font(jump_best, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(jump_best, lv_color_hex(0x344251), 0);
    lv_obj_set_style_bg_opa(jump_best, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(jump_best, 7, 0);
    lv_obj_set_style_pad_all(jump_best, 5, 0);
    lv_obj_set_style_text_align(jump_best, LV_TEXT_ALIGN_CENTER, 0);
    jump_station = label(jump_panel, 0x798295, "下一站 · NABO 靶心", 16, 70);
    jump_hint =
        label(jump_panel, 0x596779, "按住 OK 蓄力，松开起跳\n上：返回  下：换角色", 16, 286);
    lv_obj_set_style_text_font(jump_hint, &nabo_menu_font_16, 0);
    lv_obj_set_pos(jump_hint, 10, 286);
    lv_obj_set_width(jump_hint, 230);
    lv_obj_set_style_text_font(jump_score, &lv_font_montserrat_28, 0);
    nabo_apps_initialize();
    nabo_apps_register_tools();
    ESP_LOGI("NABO-Apps", "menus, table and jump ready");
}
static void change_page(NaboApp next) {
    if (page == NABO_TABLE) {
        nabo_table_close();
        static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->EndDirectDraw();
    }
    if (page == NABO_JUMP) {
        lv_obj_add_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
        if (jump_strip) {
            lv_draw_buf_destroy(jump_strip);
            jump_strip = nullptr;
        }
    }
    page = next;
    edit = -1;
    selected = next == NABO_TOOLS ? 1 : 0;
    bool menu = !(next == NABO_HOME || next == NABO_JUMP || next == NABO_TABLE);
    // The menu panel exists only while a menu page is shown: LVGL allocates
    // from the C3's system heap, which XiaoZhi's TLS and Opus need at home.
    if (menu) {
        menu_view::Build(app_screen);
    } else
        menu_view::Free();
    if (next == NABO_TABLE) {
        // The table draws straight to the panel; LVGL refresh pauses meanwhile. Its sprites are
        // read in place from the assets partition.
        void* pack = nullptr;
        size_t pack_size = 0;
        bool have_pack = Assets::GetInstance().GetAssetData("table_sprites.bin", pack, pack_size);
        // NABO DUEL's sprites; an older assets partition without them simply has no duel.
        void* duel_pack = nullptr;
        size_t duel_size = 0;
        if (!Assets::GetInstance().GetAssetData("duel_sprites.bin", duel_pack, duel_size))
            duel_pack = nullptr, duel_size = 0;
        nabo_table_set_duel_pack(duel_pack, duel_size);
        auto display = static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay());
        esp_lcd_panel_io_handle_t io;
        esp_lcd_panel_handle_t panel;
        display->BeginDirectDraw(&io, &panel);
        if (!have_pack || !nabo_table_open(io, panel, esp_random(), pack, pack_size)) {
            display->EndDirectDraw();
            snprintf(notice, sizeof(notice), have_pack ? "内存不足，请退出重试" : "缺少牌桌素材，请更新资源分区");
            nabo_apps_open(NABO_TOOLS);
        }
    }
    if (next == NABO_JUMP) {
        jump_strip = lv_draw_buf_create(240, 20, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (jump_strip) {
            nabo_jump_reset(&jump_game, esp_random(), best_score.load());
            jump_frame = jump_game;
            jump_frame_time = esp_timer_get_time() / 1000;
            lv_image_set_src(jump_image, &jump_source);
            lv_obj_remove_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(jump_panel);
            jump_last_frame = 0;
        } else {
            snprintf(notice, sizeof(notice), "内存不足，请退出重试");
            nabo_apps_open(NABO_TOOLS);
        }
    }
    ESP_LOGI("NABO-Apps", "page=%d", static_cast<int>(next));
}
void nabo_apps_tick() {
    if (!app_screen)
        return;
    int revision = request_revision.load();
    if (revision != shown_revision && ((wanted.load() == NABO_HOME && !ready) || ready.load())) {
        shown_revision = revision;
        change_page(static_cast<NaboApp>(wanted.load()));
    }
    if (page == NABO_HOME)
        return;
    if (page == NABO_TABLE)
        return;   // the table runs on its own task
    if (page == NABO_JUMP) {
        uint32_t now = esp_timer_get_time() / 1000;
        auto previous = jump_game.phase;
        nabo_jump_tick(&jump_game, now);
        jump_round_over = jump_game.phase == JUMP_OVER;
        if (jump_game.sound) {
            jump_game.sound = false;
            jump_sound = jump_game.pending_charge && !jump_game.pending_released ? 5
                         : jump_game.centered                                    ? 2
                                                                                 : 3;
        }
        if (previous == JUMP_SCROLLING && jump_game.phase == JUMP_FLYING)
            jump_sound = 1;
        if (jump_game.phase == JUMP_FALLING && previous != JUMP_FALLING)
            jump_sound = 4;
        if (!jump_test_mode && jump_game.best > best_score.load()) {
            best_score = jump_game.best;
            best_dirty = true;
            best_changed_at = esp_timer_get_time();
        }
        char text[80];
        snprintf(text, sizeof(text), "%lu", (unsigned long)jump_game.score);
        show_text(jump_score, text);
        snprintf(text, sizeof(text), "BEST\n%lu", (unsigned long)jump_game.best);
        show_text(jump_best, text);
        static const char* station_names[] = {"NABO 医院", "NABO 靶心", "床旁书房",
                                              "功德木鱼",  "NABO 播客", "值班咖啡"};
        snprintf(text, sizeof(text), "下一站 · %s", station_names[jump_game.next_skin]);
        show_text(jump_station, text);
        show_text(jump_hint, jump_game.phase == JUMP_OVER ? "游戏结束  OK：再来一局\n上：返回"
                             : jump_game.phase == JUMP_SCROLLING && jump_game.centered
                                 ? "正中！连击加分\n上：返回  下：换角色"
                                 : "按住 OK 蓄力，松开起跳\n上：返回  下：换角色");
        if (esp_timer_get_time() - jump_last_frame >= 14000) {
            jump_last_frame = esp_timer_get_time();
            int x0, y0, x1, y1, a0, b0, a1, b1;
            auto bounds = nabo_jump_camera_moving(&jump_frame, jump_frame_time) ||
                                  nabo_jump_camera_moving(&jump_game, now)
                              ? nabo_jump_scene_bounds
                              : nabo_jump_character_bounds;
            bounds(&jump_frame, jump_frame_time, &x0, &y0, &x1, &y1);
            bounds(&jump_game, now, &a0, &b0, &a1, &b1);
            lv_area_t area = {std::min(x0, a0), std::min(y0, b0), std::max(x1, a1),
                              std::max(y1, b1)};
            bool charge_bar = nabo_jump_charging(&jump_frame) || nabo_jump_charging(&jump_game);
            jump_frame = jump_game;
            jump_frame_time = now;
            lv_obj_invalidate_area(jump_image, &area);
            if (charge_bar) {
                lv_area_t bar = {30, 281, 210, 286};
                lv_obj_invalidate_area(jump_image, &bar);
            }
        }
        return;
    }
    if (!menu_view::Built())
        return;
    namespace mv = menu_view;
    char text[640], counter[32];
    const char* hint = "OK 进入 · 双击上 返回";
    mv::Begin();
    if (page == NABO_TOOLS) {
        snprintf(counter, sizeof(counter), "%02d/%02d", selected, kToolCount - 1);
        mv::Header("TOOLS", "NABO 工具箱", mv::kOrange, counter);
        mv::Rows(tools, kToolCount, selected, false);
    } else if (page == NABO_SETTINGS) {
        snprintf(counter, sizeof(counter), "%02d/%02d", settings_selected, ST_COUNT - 1);
        mv::Header(edit >= 0 ? "EDIT" : "SETTINGS",
                   edit >= 0 ? "上/下 调整，OK 保存" : "声音、显示与网络", mv::kBlue, counter);
        mv::Rows(settings_names, ST_COUNT, settings_selected, edit >= 0);
        int first = mv::First(settings_selected, ST_COUNT);
        for (int i = 0; i < mv::kRowsVisible && first + i < ST_COUNT; ++i) {
            int item = first + i;
            text[0] = 0;
            if (item == ST_VOLUME)
                snprintf(text, sizeof(text), "%d%%", volume.load());
            else if (item == ST_BRIGHTNESS)
                snprintf(text, sizeof(text), "%d%%", brightness.load());
            else if (item == ST_MIC)
                snprintf(text, sizeof(text), "%d dB", mic_gain.load());
            else if (item == ST_SLEEP) {
                int seconds = sleep_seconds.load();
                if (!seconds)
                    snprintf(text, sizeof(text), "不休眠");
                else if (seconds % 60)
                    snprintf(text, sizeof(text), "%d 秒", seconds);
                else
                    snprintf(text, sizeof(text), "%d 分钟", seconds / 60);
            } else if (item == ST_SPEAKER)
                snprintf(text, sizeof(text), speaker ? "开" : "关");
            else if (item == ST_WIFI)
                snprintf(text, sizeof(text), wifi_on ? "开" : "关");
            if (item == edit && text[0]) {
                char editing[48];
                snprintf(editing, sizeof(editing), "< %.32s >", text);
                mv::Value(i, editing);
            } else
                mv::Value(i, text);
        }
        if (edit >= 0)
            hint = "上/下 调整 · OK 保存";
    } else if (page == NABO_STATUS) {
        auto& wifi = WifiManager::GetInstance();
        char memory[24], ip[24], ssid[40];
        snprintf(memory, sizeof(memory), "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
        snprintf(ip, sizeof(ip), "%s", wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
        snprintf(ssid, sizeof(ssid), "%s", wifi.IsConnected() ? wifi.GetSsid().c_str() : "-");
        char power[16] = "-";
        if (battery.load() >= 0)
            snprintf(power, sizeof(power), "%d%%", battery.load());
        const char* names[] = {"Wi-Fi", "网络", "IP", "电量", "可用内存", "固件"};
        const char* texts[] = {wifi.IsConnected() ? "已连接" : "未连接", ssid, ip, power, memory,
                               "NABO 1.2"};
        mv::Header("STATUS", "设备状态", mv::kInk, nullptr);
        mv::Info(names, texts, 6);
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_BATTERY) {
        int pct = battery.load();
        mv::Header("BATTERY", "电池信息", mv::kYellow, nullptr);
        if (pct >= 0) {
            snprintf(text, sizeof(text), "%d%%", pct);
            mv::Big(text, 96);
            mv::Meter(pct, 160);
        } else
            mv::Body("暂时无法读取电量", 100);
        const char* names[] = {"电量芯片"};
        const char* texts[] = {"CW2017"};
        mv::Info(names, texts, 1, 204);
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_WIFI) {
        snprintf(text, sizeof(text), "1  手机连接热点\n    %s\n\n2  浏览器打开\n    %s\n\n3  配网完成后按 OK 返回",
                 WifiManager::GetInstance().GetApSsid().c_str(),
                 WifiManager::GetInstance().GetApWebUrl().c_str());
        mv::Header("WI-FI", "添加网络", mv::kBlue, nullptr);
        mv::Body(text, 96);
        hint = "OK 返回设置";
    }
    mv::Footer(notice[0] ? notice : hint);
    mv::End();
}
static void adjust(int item, int direction) {
    if (item == ST_VOLUME)
        volume = std::clamp(volume.load() + direction * 10, 0, 100);
    else if (item == ST_BRIGHTNESS)
        brightness = std::clamp(brightness.load() + direction * 10, 10, 100);
    else if (item == ST_MIC)
        mic_gain = std::clamp(mic_gain.load() + direction * 6, 0, 36);
    else if (item == ST_SLEEP) {
        static const int times[] = {0, 30, 60, 120, 300, 600};
        int i = 0;
        while (i < 5 && times[i] < sleep_seconds)
            i++;
        i = (i + (direction > 0 ? 1 : 5)) % 6;
        sleep_seconds = times[i];
    }
    setting_command = item;
}
static void return_one_level() {
    NaboApp current = static_cast<NaboApp>(wanted.load());
    if (current == NABO_HOME)
        return;
    bool settled = ready.load() && current == page;
    if (settled && current == NABO_SETTINGS && edit >= 0) {
        edit = -1;
        return;
    }
    if (current == NABO_STATUS || current == NABO_BATTERY || current == NABO_WIFI) {
        if (current == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(static_cast<NaboApp>(detail_parent.load()));
    } else
        nabo_apps_open(current == NABO_TOOLS ? NABO_HOME : NABO_TOOLS);
}
bool nabo_apps_key(muse_menu_key_t key) {
    last_input = esp_timer_get_time();
    if (screen_off.exchange(false)) {
        Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        return true;
    }
    // In the table, UP is a game key: tapping it twice or holding it to scroll a bid must not
    // throw the players out. Double or long UP leave only from the table's cover menu.
    if (page == NABO_TABLE && wanted.load() == NABO_TABLE &&
        (key == MUSE_MENU_LEFT || key == MUSE_MENU_BACK) && !nabo_table_on_cover())
        return true;
    // Double UP returns through nested menus. Long UP keeps its direct HOME path.
    if (key == MUSE_MENU_LEFT) {
        return_one_level();
        return true;
    }
    if (!nabo_apps_active()) {
        if (key == MUSE_MENU_DOWN) {
            nabo_apps_open(NABO_TOOLS);
            return true;
        }
        if (key == MUSE_MENU_UP) {
            nabo_apps_open(NABO_STATUS);
            return true;
        }
        return false;
    }
    if (key == MUSE_MENU_BACK) {
        nabo_apps_open(NABO_HOME);
        return true;
    }
    if (!ready)
        return true;
    if (page == NABO_TABLE)
        return true;   // the table reads raw button edges (repeat, long OK) itself
    if (page == NABO_JUMP) {
        if (key == MUSE_MENU_UP)
            nabo_apps_open(NABO_TOOLS);
        else if (key == MUSE_MENU_DOWN)
            jump_game.character = 1 - jump_game.character;
        else if (key == MUSE_MENU_RIGHT)
            nabo_jump_reset(&jump_game, esp_random(), best_score.load());
        return true;
    }
    if (page == NABO_TOOLS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN)
            selected = (selected + (key == MUSE_MENU_UP ? kToolCount - 1 : 1)) % kToolCount;
        else if (key == MUSE_MENU_SELECT)
            nabo_apps_open(tool_apps[selected]);
    } else if (page == NABO_SETTINGS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            if (edit >= 0)
                adjust(edit, key == MUSE_MENU_UP ? 1 : -1);
            else
                settings_selected =
                    (settings_selected + (key == MUSE_MENU_UP ? ST_COUNT - 1 : 1)) % ST_COUNT;
        } else if (key == MUSE_MENU_SELECT) {
            if (edit >= 0) {
                edit = -1;
                return true;
            }
            int item = settings_selected;
            if (item == ST_BACK)
                nabo_apps_open(NABO_TOOLS);
            else if (item == ST_VOLUME || item == ST_BRIGHTNESS || item == ST_MIC ||
                     item == ST_SLEEP)
                edit = item;
            else if (item == ST_SPEAKER) {
                speaker = !speaker.load();
                setting_command = item;
            } else if (item == ST_WIFI) {
                wifi_on = !wifi_on.load();
                setting_command = item;
            } else if (item == ST_WIFI_ADD) {
                setting_command = item;
                nabo_apps_open(NABO_WIFI);
            } else if (item == ST_STATE)
                nabo_apps_open(NABO_STATUS);
            else if (item == ST_BATTERY)
                nabo_apps_open(NABO_BATTERY);
            else if (item == ST_SCREEN)
                setting_command = item;
            else if (item == ST_POWER)
                setting_command = item;
        }
    } else if (key == MUSE_MENU_SELECT || key == MUSE_MENU_UP) {
        if (page == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(NABO_SETTINGS);
    }
    return true;
}
void nabo_apps_down_pressed(uint32_t event_ms) {
    if (page == NABO_TABLE && ready.load() && wanted.load() == NABO_TABLE)
        nabo_table_button(NABO_TABLE_DOWN, true, event_ms);
}
void nabo_apps_down_released(uint32_t event_ms) {
    if (page == NABO_TABLE)
        nabo_table_button(NABO_TABLE_DOWN, false, event_ms);
}
void nabo_apps_up_edge(bool pressed, uint32_t event_ms) {
    last_input = esp_timer_get_time();
    if (page == NABO_TABLE && (!pressed || (ready.load() && wanted.load() == NABO_TABLE)))
        nabo_table_button(NABO_TABLE_UP, pressed, event_ms);
}
void nabo_apps_charge(bool pressed, uint32_t event_ms) {
    last_input = esp_timer_get_time();
    if (!event_ms)
        event_ms = esp_timer_get_time() / 1000;
    if (page == NABO_TABLE && (!pressed || (ready.load() && wanted.load() == NABO_TABLE))) {
        nabo_table_button(NABO_TABLE_OK, pressed, event_ms);
        return;
    }
    if (!nabo_apps_is_jump())
        return;
    if (pressed) {
        nabo_jump_press(&jump_game, event_ms);
        jump_round_over = jump_game.phase == JUMP_OVER;
        if (nabo_jump_charging(&jump_game))
            jump_sound = 5;
    } else {
        bool charging = jump_game.phase == JUMP_CHARGING;
        bool pending = jump_game.pending_charge && !jump_game.pending_released;
        nabo_jump_release(&jump_game, event_ms);
        if (charging)
            jump_sound = 1;
        else if (pending)
            jump_sound = 6;
    }
}
void nabo_apps_register_tools() {
    auto& server = McpServer::GetInstance();
    server.AddTool(
        "self.apps.open",
        "打开工牌内置程序。app "
        "可用：跳一跳(jump)、NABO 牌桌(table，21 点、大话骰)、谁是卧底(undercover，在牌桌里)、"
        "NABO 对决(duel，两台工牌联机拔枪，也能单人练习，在牌桌里)"
        "、设置(settings)、工具(tools)。进入程序会暂停语音对话；双击上键返回上一级，长按上键回到 "
        "NABO "
        "恢复小智。",
        PropertyList({Property("app", kPropertyTypeString).SetMaxLength(40)}),
        [](const PropertyList& properties) -> ToolResult {
            const auto& name = properties["app"].value<std::string>();
            NaboApp app = NABO_HOME;
            if (name == "jump" || name == "跳一跳")
                app = NABO_JUMP;
            else if (name == "table" || name == "牌桌" || name == "NABO 牌桌" || name == "21点" ||
                     name == "21 点" || name == "大话骰")
                app = NABO_TABLE;
            else if (name == "undercover" || name == "谁是卧底" || name == "卧底") {
                app = NABO_TABLE;
                nabo_table_start_on(NABO_TABLE_START_UNDERCOVER);
            }
            else if (name == "duel" || name == "对决" || name == "联机对决" || name == "快枪手") {
                app = NABO_TABLE;
                nabo_table_start_on(NABO_TABLE_START_DUEL);
            }
            else if (name == "settings" || name == "设置")
                app = NABO_SETTINGS;
            else if (name == "tools" || name == "工具")
                app = NABO_TOOLS;
            else
                return std::unexpected(
                    "Unknown app. Use jump, table, undercover, duel, settings or tools.");
            nabo_apps_open(app, 900);
            return std::string("已安排打开 ") + name + "。程序中使用实体按键；长按上键返回小智。";
        });
}
bool nabo_apps_voice_command(const char* transcript) {
    if (nabo_apps_active())
        return false;
    const char* name = nabo_voice_launch_app(transcript);
    if (!name)
        return false;
    if (!strcmp(name, "undercover"))
        nabo_table_start_on(NABO_TABLE_START_UNDERCOVER);
    if (!strcmp(name, "duel"))
        nabo_table_start_on(NABO_TABLE_START_DUEL);
    NaboApp target = !strcmp(name, "table") || !strcmp(name, "undercover") || !strcmp(name, "duel") ? NABO_TABLE
                     : !strcmp(name, "jump")     ? NABO_JUMP
                     : !strcmp(name, "settings") ? NABO_SETTINGS
                                                 : NABO_TOOLS;
    nabo_apps_open(target);
    ESP_LOGI("NABO-Voice", "recognized launch -> %s", name);
    return true;
}
