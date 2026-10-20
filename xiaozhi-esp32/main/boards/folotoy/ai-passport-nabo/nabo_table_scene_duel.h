#pragma once
// NABO DUEL screens on the table renderer (the sprite player must point at duel_sprites.bin).
#include "nabo_duel.h"
#include "nabo_table_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// big: the 56 px font for the shout on the draw ("拔！", "拔？", "抢跑！").
void nt_scene_duel(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f, const lv_font_t *big);

#ifdef __cplusplus
}
#endif
