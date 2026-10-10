/* Presentation only. IDs are persisted: never reorder or reuse them. */
#ifndef AQUA_THEME_H
#define AQUA_THEME_H
#include <stdint.h>
#include "icons.h"

typedef enum { THEME_ORIGINAL = 0, THEME_QUIET_LAGOON = 1,
               THEME_TIDEPOOL_CLUB = 2, THEME_BLACKWATER = 3, THEME_COUNT = 4 } theme_id_t;
typedef struct {
    const char *name;
    uint32_t background, panel, text, muted, accent, on_accent, border, gold;
    uint32_t water[3], grass[4], sand[4];
    int radius;
} theme_palette_t;

int theme_valid(int id);
const theme_palette_t *theme_palette(int id);
int theme_active(void);
int theme_asset_set(int id);   /* the icon + font set a theme draws with (Blackwater borrows the lagoon's: ~160 KB of flash each) */
void theme_activate(int id); /* renderer owns activation and cache invalidation */
uint32_t theme_ui_color(uint32_t original);
uint32_t theme_creature_color(uint32_t original, int fin);
const icon_t *theme_icon(const icon_t *original);

/* Generated, flash-resident artwork. Original icons are never replaced in place. */
const icon_t *theme_preview_icon(int theme);
const icon_t *theme_asset_icon(int theme, const icon_t *original);
/* Antialiased fixed-cell glyphs retain all existing layout/hit-test metrics. */
const uint8_t *theme_font_glyph(int theme, int scale, unsigned char ch);
#endif
