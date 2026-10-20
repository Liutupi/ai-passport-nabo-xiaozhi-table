# NABO TABLE · DUEL offline firmware. The table and duel sources are the XiaoZhi board's own
# files, compiled from where they live, so both firmwares always run the same game code.
set(BOARD "${CMAKE_CURRENT_LIST_DIR}/../../xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo")
set(GAME_SOURCES
    nabo_dice_render.c nabo_duel.c nabo_duel_model.c nabo_link.cc nabo_table.c nabo_table_bid.c
    nabo_table_gfx.c nabo_table_model.c nabo_table_scene.c nabo_table_scene_duel.c nabo_table_scene_play.c
    nabo_table_scene_uc.c nabo_table_shapes.c nabo_table_sound.c nabo_table_sprites.c nabo_table_text.c
    nabo_table_uc.c nabo_uc_words.c
    nabo_table_font_12.c nabo_table_font_24.c nabo_menu_font_16.c nabo_ui_font_18.c nabo_mono_font_12.c
    nabo_poster_font_28.c nabo_duel_font_56.c)
list(TRANSFORM GAME_SOURCES PREPEND "${BOARD}/")
idf_component_register(
    SRCS "standalone_main.c" "collection_model.c" "collection_ui.c" "collection_portrait.c"
         "collection_font_12.c" "collection_font_16.c" "collection_font_18.c" ${GAME_SOURCES}
    INCLUDE_DIRS "." "${BOARD}"
    REQUIRES bsp lvgl esp_lcd esp_timer nvs_flash esp_rom esp_wifi esp_event esp_driver_gpio
    EMBED_FILES "${BOARD}/table_assets/table_sprites.bin" "${BOARD}/table_assets/duel_sprites.bin"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE NABO_STANDALONE=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
