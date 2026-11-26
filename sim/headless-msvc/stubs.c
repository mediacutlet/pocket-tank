/* link-level stubs for the headless --snapshot build (no SDL, no LVGL, no LLM worker) */
#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"
#include "advisor.h"
Uint32 SDL_GetTicks(void) { return 0; } void SDL_Delay(Uint32 ms) { (void)ms; } const char *SDL_GetError(void) { return "stub"; }
int SDL_InitSubSystem(Uint32 f) { (void)f; return -1; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *a, int b, const SDL_AudioSpec *c, SDL_AudioSpec *d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p) { (void)d; (void)p; } void SDL_LockAudioDevice(SDL_AudioDeviceID d) { (void)d; } void SDL_UnlockAudioDevice(SDL_AudioDeviceID d) { (void)d; }
const Uint8 *SDL_GetKeyboardState(int *n) { static Uint8 k[512]; (void)n; return k; } Uint32 SDL_GetMouseState(int *x, int *y) { *x = *y = 0; return 0; }
void lv_init(void) {} void lv_tick_set_cb(lv_tick_get_cb_t cb) { (void)cb; }
lv_display_t *lv_sdl_window_create(int32_t w, int32_t h) { (void)w; (void)h; return 0; } void lv_sdl_window_set_title(lv_display_t *d, const char *t) { (void)d; (void)t; }
lv_result_t lv_draw_buf_init(lv_draw_buf_t *b, uint32_t w, uint32_t h, lv_color_format_t cf, uint32_t stride, void *data, uint32_t sz) { (void)b; (void)w; (void)h; (void)cf; (void)stride; (void)data; (void)sz; return LV_RESULT_OK; }
lv_obj_t *lv_canvas_create(lv_obj_t *p) { (void)p; return 0; } void lv_canvas_set_draw_buf(lv_obj_t *o, lv_draw_buf_t *b) { (void)o; (void)b; }
lv_obj_t *lv_display_get_screen_active(lv_display_t *d) { (void)d; return 0; } void lv_obj_align(lv_obj_t *o, lv_align_t a, int32_t x, int32_t y) { (void)o; (void)a; (void)x; (void)y; }
void lv_obj_invalidate(const lv_obj_t *o) { (void)o; }
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t p, void *ud) { (void)cb; (void)p; (void)ud; return 0; } uint32_t lv_timer_handler(void) { return 16; }
bool advisor_llm_narrate;
bool advisor_llm_init(const char *m, const char *t) { (void)m; (void)t; return false; }
goal_t advisor_llm(const tank_t *t, int i, bool r) { (void)r; return advisor_rules(t, i, true); }
void advisor_llm_debug(const tank_t *t, int i) { (void)t; (void)i; }
/* selftest-only POSIX bits; the snapshot path never reaches them */
int setenv(const char *k, const char *v, int o) { (void)o; return _putenv_s(k, v); }
int truncate(const char *p, long long n) { (void)p; (void)n; return -1; }
lv_obj_t *lv_screen_active(void) { return 0; } void lv_obj_center(lv_obj_t *o) { (void)o; }
int __builtin_popcount(unsigned v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
