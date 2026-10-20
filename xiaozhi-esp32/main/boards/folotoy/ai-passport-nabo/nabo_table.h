#pragma once
// NABO TABLE: 21 points and liar's dice for 2..4 players and who is the undercover for 3..8,
// with NABO as the dealer. Like DIVE, it owns
// the panel while open (LVGL refresh paused) and runs its own game task.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { NABO_TABLE_UP = 0, NABO_TABLE_DOWN = 1, NABO_TABLE_OK = 2 };
enum { NABO_TABLE_START_COVER = 0, NABO_TABLE_START_UNDERCOVER = 1, NABO_TABLE_START_DUEL = 2 };

// Where the next nabo_table_open() starts (voice "打开谁是卧底" goes straight to its setup).
void nabo_table_start_on(int where);
// NABO DUEL's sprite pack (duel_sprites.bin), set before nabo_table_open(); without it the
// duel entry on the cover does nothing.
void nabo_table_set_duel_pack(const void *pack, size_t pack_size);
// Scripted tests: the duel neither records a best time nor saves settings (until reboot).
void nabo_table_duel_test(bool on);

// pack: table_sprites.bin as mapped from the assets partition.
bool nabo_table_open(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, uint32_t seed, const void *pack,
                     size_t pack_size);
void nabo_table_close(void);
bool nabo_table_running(void);
// True on the table's cover menu (not paused, not in the duel): the only place long UP leaves the app.
bool nabo_table_on_cover(void);
// Raw button edges, safe from any task. The game does its own repeat and long press.
void nabo_table_button(int button, bool pressed, uint32_t event_ms);
// Serial diagnostics: a short key (0 up, 1 down, 2 ok, 3 long ok) and a one-line state report.
void nabo_table_inject(int key);
int nabo_table_status(char *out, size_t cap);
// Redraws the whole screen once and also prints it over serial (@cap lines).
void nabo_table_capture(void);
// Mixes 16 kHz mono PCM into out for the apps worker.
void nabo_table_audio_fill(int16_t *out, size_t count);

#ifdef __cplusplus
}
#endif
