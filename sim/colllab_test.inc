/* colllab_test.inc - --selftest-coll-lab (0.4.3 audit r1 SV-8, Strato 11-23: "lab grown fish should show their true form on
 * visitor collection pages"), included at the end of selftest_coll.c. A lab-made visitor is collected with its own body
 * and fin lengths, kept through a reboot, and drawn on the page as the tank drew it; a plain visitor draws as before; a
 * record that went through 0.4.2 (its own rec_read / rec_write, copied below from 4dae3468) draws the nearest named body. */
static void o42_put16(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void o42_put32(uint8_t *b, int o, uint32_t v) { o42_put16(b, o, v & 0xffff); o42_put16(b, o + 2, v >> 16); }
static uint32_t o42_get16(const uint8_t *b, int o) { return b[o] | (uint32_t)b[o + 1] << 8; }
static uint32_t o42_get32(const uint8_t *b, int o) { return o42_get16(b, o) | o42_get16(b, o + 2) << 16; }
static void o42_rec_write(const coll_entry_t *e, uint8_t *b) {   /* 0.4.2 (4dae3468 common/collection.c), verbatim but for names */
    memset(b, 0, 76);
    o42_put32(b, 0, e->uid); o42_put32(b, 4, e->home); o42_put32(b, 8, e->parent[0]); o42_put32(b, 12, e->parent[1]);
    memcpy(b + 16, e->name, FISH_NAME_MAX);
    o42_put32(b, 24, e->color & 0xffffff); o42_put32(b, 28, e->fin & 0xffffff); o42_put32(b, 32, e->accent & 0xffffff);
    b[36] = e->preset; b[37] = e->stage; b[38] = e->shape; b[39] = e->fins; b[40] = e->pattern; b[41] = (uint8_t)e->badge;
    o42_put32(b, 42, e->met_unix); o42_put32(b, 46, e->seen_unix); o42_put32(b, 50, e->here_s);
    o42_put16(b, 54, e->visits); b[56] = e->fry;
    memcpy(b + 57, e->home_name, 16);
}
static void o42_rec_read(coll_entry_t *e, const uint8_t *b, int rec) {
    memset(e, 0, sizeof *e);
    e->uid = o42_get32(b, 0); e->home = o42_get32(b, 4); e->parent[0] = o42_get32(b, 8); e->parent[1] = o42_get32(b, 12);
    memcpy(e->name, b + 16, FISH_NAME_MAX); e->name[FISH_NAME_MAX] = 0;
    e->color = o42_get32(b, 24) & 0xffffff; e->fin = o42_get32(b, 28) & 0xffffff; e->accent = o42_get32(b, 32) & 0xffffff;
    e->preset = (uint8_t)(b[36] % tank_roster_count()); e->stage = b[37] & 3;
    e->shape = b[38] < SHAPE_N ? b[38] : 0; e->fins = b[39] < FINS_N ? b[39] : 0; e->pattern = b[40] < PATTERN_N ? b[40] : 0;
    e->badge = (int8_t)b[41]; if (e->badge >= 4) e->badge = -1;
    e->met_unix = o42_get32(b, 42); e->seen_unix = o42_get32(b, 46); e->here_s = o42_get32(b, 50);
    e->visits = (uint16_t)o42_get16(b, 54); e->fry = b[56];
    if (rec >= 73) { char raw[16]; memcpy(raw, b + 57, 16); raw[15] = 0; passport_clean_tank_name(e->home_name, raw); }
}
#define LFAIL(...) do { printf("FAIL: selftest-coll-lab: " __VA_ARGS__); printf("\n"); return 1; } while (0)
/* a visitor in slot 0: made in the lab (its own body and fins) or plain (the same, its values cleared: the nearest name) */
static void lab_visitor(int j, bool lab) {
    fish_t *v = &s_tk.fish[N_FISH_MAX]; *v = s_tk.fish[0];
    v->uid = 0x1ab00000u + (uint32_t)j; v->stage = STAGE_ADULT; v->color = 0x38dcc7; v->fin = 0xffd166; v->accent = 0x9b72ff;
    v->fins = (uint8_t)(1 + j % (FINS_N - 1)); v->pattern = 0;
    static const float BD[4][2] = { { 21.0f, 11.7f }, { 13.0f, 7.8f }, { 17.5f, 13.0f }, { 24.0f, 8.0f } };
    static const float FL[4][3] = { { 1.45f, 0.6f, 1.3f }, { 0.55f, 1.4f, 0.7f }, { 1.2f, 1.2f, 1.5f }, { 0.8f, 0.5f, 0.9f } };
    tank_clear_body(v); tank_clear_fin_len(v);
    tank_set_body(v, BD[j % 4][0], BD[j % 4][1]);
    for (int k = 0; k < 3; k++) tank_set_fin_len(v, k, FL[j % 4][k]);
    if (j % 4 == 3) { v->stage = STAGE_ELDER; v->evo = EVO_BANNER; }   /* (a lab paragon too: its part and its form, review 11-23) */
    if (!lab) { tank_clear_body(v); tank_clear_fin_len(v); }   /* (its shape stays the nearest name: the page as it drew a lab fish before) */
    snprintf(v->name, sizeof v->name, "lab%d", j);
    s_tk.vis_used[0] = true; s_tk.vis_badge[0] = 1; s_tk.vis_home[0] = 0x7100; s_tk.vis_since[0] = s_tk.clock;
}
static uint32_t page_crc(uint16_t *fb) {
    render_collection_leave(); render_collection(&s_tk, fb, TANK_W);
    uint32_t h = 2166136261u;
    for (int i = 0; i < TANK_W * TANK_H; i++) h = (h ^ fb[i]) * 16777619u;
    return h;
}
static void coll_caches(void) {
    static uint16_t scene[TANK_W * TANK_H], card[RENDER_CARD_W * RENDER_CARD_H], give[GIFT_CARD_W * GIFT_CARD_H], gift[GIFT_CARD_W * GIFT_CARD_H];
    static uint8_t vig[TANK_W * TANK_H]; static uint32_t dirty[RENDER_DIRTY_WORDS];
    render_set_scene_cache(scene); render_set_vignette_cache(vig); render_set_card_cache(card); render_set_modal_cache(give, gift); render_set_dirty_mask(dirty);
}
int selftest_coll_lab(void) {
    static uint16_t fb[TANK_W * TANK_H];
    board_wipe(); board_boot();
    tank_init(&s_tk, 2026); tank_new_population(&s_tk); tank_ensure_ids(&s_tk);
    coll_caches();
    s_tk.clock = 100.0;
    /* the plain draw of each, first: what the page showed before (and shows for a record without the values) */
    uint32_t plain[4], plain42[4], labc[4];
    for (int j = 0; j < 4; j++) {
        collection_reset(); lab_visitor(j, false);
        if (!collection_add(&s_tk, 0)) LFAIL("collect refused (plain %d)", j);
        s_tk.vis_used[0] = false; plain[j] = plain42[j] = page_crc(fb);
        if (s_tk.fish[N_FISH_MAX].evo) {                     /* (0.4.2 never kept a paragon's part, byte 73: through it, a plain elder) */
            collection_reset(); lab_visitor(j, false); s_tk.fish[N_FISH_MAX].evo = 0;
            if (!collection_add(&s_tk, 0)) LFAIL("collect refused (plain %d, no part)", j);
            s_tk.vis_used[0] = false; plain42[j] = page_crc(fb);
        }
    }
    for (int j = 0; j < 4; j++) {
        collection_reset(); lab_visitor(j, true);
        const fish_t v = s_tk.fish[N_FISH_MAX];
        if (!tank_body_set(&v) || !tank_finl_set(&v)) LFAIL("the made-up lab visitor %d has no values", j);
        if (!collection_add(&s_tk, 0)) LFAIL("collect refused (lab %d)", j);
        s_tk.vis_used[0] = false;
        const coll_entry_t *e = collection_get(0);
        if (!e || memcmp(e->body, v.body, 2) || memcmp(e->finl, v.finl, 3))
            LFAIL("lab %d collected without its own form: body %d,%d (want %d,%d) fins %d,%d,%d (want %d,%d,%d)", j, e ? e->body[0] : -1, e ? e->body[1] : -1,
                  v.body[0], v.body[1], e ? e->finl[0] : -1, e ? e->finl[1] : -1, e ? e->finl[2] : -1, v.finl[0], v.finl[1], v.finl[2]);
        if (e->color != v.color || e->fin != v.fin || e->accent != v.accent) LFAIL("lab %d: its colors changed (%06x %06x %06x)", j, e->color, e->fin, e->accent);
        if (e->evo != v.evo) LFAIL("lab %d: its part %d (want %d)", j, e->evo, v.evo);
        labc[j] = page_crc(fb);
        if (labc[j] == plain[j]) LFAIL("lab %d draws on the page as the nearest named body (the same frame as plain)", j);
        board_boot(); coll_caches(); s_tk.clock = 100.0;         /* (the reboot: read back from the flash) */
        e = collection_get(0);
        if (!e || memcmp(e->body, v.body, 2) || memcmp(e->finl, v.finl, 3) || e->color != v.color || e->fin != v.fin || e->accent != v.accent)
            LFAIL("lab %d lost its form at the reboot", j);
        if (page_crc(fb) != labc[j]) LFAIL("lab %d draws differently after the reboot", j);
        /* through 0.4.2 and back (a rollback, then 0.4.3 again): 0.4.2 masks the color words' top bytes and writes nothing past
           72 - the nearest named body, its colors exactly, never garbage */
        uint8_t blob[COLL_BLOB_MAX]; size_t got = 0;
        if (!persist_port_blob_load(COLL_NVS_KEY, blob, sizeof blob, &got) || got < 6 + 76) LFAIL("no visitors blob (%d)", (int)got);
        const int rec = blob[4] | blob[5] << 8, n = blob[3];
        if (rec != 76 || n != 1) LFAIL("the blob's shape changed (rec %d, n %d): the save grew", rec, n);
        coll_entry_t o; o42_rec_read(&o, blob + 6, rec); o42_rec_write(&o, blob + 6);
        if (!persist_port_blob_save(COLL_NVS_KEY, blob, got)) LFAIL("blob save");
        board_boot(); coll_caches(); s_tk.clock = 100.0;
        e = collection_get(0);
        if (!e || e->body[0] || e->body[1] || e->finl[0] || e->finl[1] || e->finl[2] || e->color != v.color || e->fin != v.fin || e->accent != v.accent || e->shape != v.shape)
            LFAIL("lab %d through 0.4.2: body %d,%d fins %d,%d,%d colors %06x %06x %06x shape %d (want none, its colors, shape %d)", j, e ? e->body[0] : -1, e ? e->body[1] : -1,
                  e ? e->finl[0] : -1, e ? e->finl[1] : -1, e ? e->finl[2] : -1, e ? e->color : 0, e ? e->fin : 0, e ? e->accent : 0, e ? e->shape : -1, v.shape);
        if (page_crc(fb) != plain42[j]) LFAIL("lab %d through 0.4.2 does not draw as the nearest named body", j);
    }
    render_collection_leave(); collection_reset();
    printf("selftest-coll-lab: a lab-made visitor is collected with its own body and fins (4 forms), kept through a reboot and drawn as "
           "the tank drew it; through 0.4.2's own record code and back it draws the nearest named body, its colors exact; the record stays 76 B\n");
    return 0;
}
/* --coll-lab-shot <prefix>: the visitor page with four lab-made visitors and, beside them, the same four plain (as the page
   drew them before SV-8), for Strato's look on every board */
int coll_lab_shot(const char *prefix) {
    static uint16_t fb[TANK_W * TANK_H];
    char path[600];
    board_wipe(); board_boot();
    tank_init(&s_tk, 2026); tank_new_population(&s_tk); tank_ensure_ids(&s_tk);
    coll_caches(); s_tk.clock = 100.0;
    collection_reset();
    for (int j = 0; j < 8; j++) {
        lab_visitor(j % 4, j < 4);
        s_tk.fish[N_FISH_MAX].uid = 0x1ab00000u + (uint32_t)j;
        snprintf(s_tk.fish[N_FISH_MAX].name, sizeof s_tk.fish[N_FISH_MAX].name, j < 4 ? "lab%d" : "old%d", j % 4);
        collection_add(&s_tk, 0);
    }
    s_tk.vis_used[0] = false;
    render_collection_leave(); render_collection(&s_tk, fb, TANK_W);
    snprintf(path, sizeof path, "%s_lab_page.ppm", prefix); write_ppm(path, fb);
    render_collection_leave(); collection_reset();
    printf("coll-lab-shot: %s_lab_page.ppm\n", prefix);
    return 0;
}
