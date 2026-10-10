#include "theme.h"

static int active;
static const theme_palette_t palettes[THEME_COUNT] = {
    {"ORIGINAL", 0x031015,0x04141a,0xffffff,0x2a3f45,0x9fd8e2,0x031015,0x2a3f45,0xf2b65b,
     {0x0a3c46,0x08272f,0x031015}, {0x3f8b55,0x2e7d4f,0x8dbb48,0x6c9d38},
     {0x2e3b2c,0x22301f,0x1a2418,0x101a12},0},
    /* 2026-10-10: both new themes ~20 % darker than designed - an AMOLED spends power on every lit
       pixel and a bright water field all day risks burn-in; the text, accent and gold stay */
    {"QUIET LAGOON",0x0c292c,0x123234,0xedf0df,0xadd0bf,0xa5c8ae,0x163d34,0x456761,0xd6b286,
     {0x386a63,0x164446,0x0b282f}, {0x3c6b4c,0x193e33,0x6c8a60,0x3c6b4c},
     {0x929380,0x798471,0x5a705f,0x35524b},7},
    {"TIDEPOOL CLUB",0xc8b89c,0xd1c3a9,0x423e33,0x65624f,0x337966,0xfffaf0,0x968a6c,0xa6532d,
     {0x999772,0x7b886e,0x5e7a6a}, {0x416d50,0x2e5c41,0xa09f5c,0x71844a},   /* 2026-10-10 evening: the water 16 % and the sand 12 % darker still (Alvin: "make the rest pop") */
     {0xad9874,0xa18b66,0x92815f,0x7f7455},14},
    /* Blackwater (2026-10-10, Alvin: "a realistic version of the creatures ... let the water and the
       sand be darker ... the creatures brighter for contrast"): near-black water over dark volcanic
       sand, the sunlight's caustics the only brightness; cyan accents, amber gold */
    {"BLACKWATER",0x04090e,0x0c171f,0xe6f2f5,0x56707c,0x3fd8e6,0x04090e,0x1b2f3b,0xffb648,
     {0x0a2434,0x05141e,0x02080c}, {0x1f5c3c,0x123a2a,0x3d8a55,0x24633c},
     {0x33302c,0x262421,0x1a1917,0x100f0e},5}
};
int theme_valid(int id) { return id >= 0 && id < THEME_COUNT ? id : THEME_ORIGINAL; }
const theme_palette_t *theme_palette(int id) { return &palettes[theme_valid(id)]; }
int theme_active(void) { return active; }
int theme_asset_set(int id) { return id == THEME_BLACKWATER ? THEME_QUIET_LAGOON : id; }
void theme_activate(int id) { active = theme_valid(id); }
uint32_t theme_ui_color(uint32_t rgb) {
    if (!active) return rgb;
    const theme_palette_t *p = &palettes[active];
    switch (rgb) {
    case 0x031015: return p->background;
    case 0x04141a: case 0x0e2229: case 0x1c2f36: case 0x0c2027: return p->panel;
    case 0xffffff: return p->text;
    case 0x9fd8e2: case 0x38dcc7: return p->accent;
    case 0x2a3f45: case 0x2c4a52: case 0x5f8a92: case 0x9fb4b8: return p->muted;
    case 0x155e58: return p->accent;
    case 0xf2b65b: return p->gold;
    default: return rgb;
    }
}
uint32_t theme_creature_color(uint32_t rgb, int fin) {
    if (!active) return rgb;
    if (active == THEME_BLACKWATER) {
        /* vivid against the dark water: the brightest channel lifted to ~240 (fins a little less, they
           are translucent) and the saturation pushed a quarter - the hue the keeper chose stays */
        int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
        int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m < 8) return rgb;
        int target = fin ? 222 : 240;
        r = r * target / m; g = g * target / m; b = b * target / m;
        int mean = (r + g + b) / 3;
        r = mean + (r - mean) * 5 / 4; g = mean + (g - mean) * 5 / 4; b = mean + (b - mean) * 5 / 4;
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        return (uint32_t)(r << 16 | g << 8 | b);
    }
    /* Preserve each creature's chosen hue and earned markings; soften saturation
       with a warm tint instead of changing the saved body/fin/accent colours. */
    uint32_t tint = active == THEME_QUIET_LAGOON ? 0x92b6a4 : 0xf1c69b;
    unsigned a = active == THEME_QUIET_LAGOON ? (fin ? 90 : 140) : (fin ? 24 : 66);
    unsigned r = (((rgb >> 16) & 255) * (256-a) + ((tint >> 16) & 255) * a) >> 8;
    unsigned g = (((rgb >> 8) & 255) * (256-a) + ((tint >> 8) & 255) * a) >> 8;
    unsigned b = ((rgb & 255) * (256-a) + (tint & 255) * a) >> 8;
    return (r << 16) | (g << 8) | b;
}
const icon_t *theme_icon(const icon_t *original) {
    if(original==&icon_shop_jellyfish)return jellyfish_theme_icon(active);
    if(original==&icon_shop_wreck)return wreck_theme_icon(active);
    if(original==&icon_shop_frogman)return frogman_theme_icon(active);
    if(original==&icon_shop_sub)return sub_theme_icon(active);
    return active ? theme_asset_icon(theme_asset_set(active), original) : original;
}
