#include "touch_port.h"
bool touch_port_init(void) { return false; }
bool touch_port_deep_sleep(void) { return false; }
void touch_port_poll(tank_t *t) { (void)t; }
int  touch_port_selected(void) { return -1; }
static bool s_ms;
bool touch_port_milestones(void) { return s_ms; }
void touch_port_show_milestones(bool on) { s_ms = on; }
void touch_port_set_inverted(bool inverted) { (void)inverted; }
/* no glass to tap: the prompt can only be answered by the director */
static bool s_cf; static int s_cf_ans;
void touch_port_confirm_open(void) { s_cf = true; s_cf_ans = 0; }
bool touch_port_confirm_answer(int ans) { if (!s_cf) return false; s_cf = false; s_cf_ans = ans > 0 ? 1 : -1; return true; }
bool touch_port_confirm_up(void) { return s_cf; }
bool touch_port_down(void) { return false; }
float touch_port_confirm_frac(void) { return s_cf ? 1.0f : 0.0f; }
int  touch_port_confirm_take(void) { int a = s_cf_ans; s_cf_ans = 0; return a; }
bool touch_port_pressed_since(int64_t us) { (void)us; return false; }
static bool s_set;
bool touch_port_settings(void) { return s_set; }
void touch_port_show_settings(bool on) { s_set = on; }
int  touch_port_take_setting(int *value) { (void)value; return 0; }
static bool s_shop;
bool touch_port_shop(void) { return s_shop; }
void touch_port_show_shop(bool on) { s_shop = on; }
int  touch_port_take_shop(void) { return 0; }
static bool s_bat;
void touch_port_set_pill(bool up) { (void)up; }
bool touch_port_battery(void) { return s_bat; }
void touch_port_show_battery(bool on) { s_bat = on; }
void touch_port_dismiss(void) { s_ms = false; s_bat = false; }
static bool s_upd;
bool touch_port_updates(void) { return s_upd; }
void touch_port_show_updates(bool on) { s_upd = on; }
int  touch_port_take_update(void) { return 0; }
bool touch_port_read_raw(float *x, float *y) { (void)x; (void)y; return false; }
void touch_port_set_bias(int px) { (void)px; }
int  touch_port_bias(void) { return 0; }
void touch_port_set_log(bool on) { (void)on; }
void touch_port_set_lift(int ms, int said) { (void)ms; (void)said; }
int  touch_port_lift_ms(void) { return 0; }
bool touch_port_lift_said(void) { return false; }
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1) { *x0 = *y0 = 0; *x1 = *y1 = -1; }
