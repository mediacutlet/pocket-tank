/* upgrade042_dump.inc - one board as text, the same in the 0.4.2 writer (write042.c) and the 0.4.3 test
 * (selftest_upgrade042.c): the tank on the glass, then each tank on the shelf in turn (SWAP IN), then back. Every tank's
 * save after its boot is kept as sav-<shelf id>.bin in the board's directory (`keep`: the writer's; the test writes
 * got-<id>.bin). Needs T, swap_to and the 0.4.2 API only. */
static void up_tank(FILE *o) {
    const shelf_tank_t *g = shelf_now();
    fprintf(o, "tank id %u type %d variant %d time %d name %s\n", g->id, g->type, g->variant, g->time_mode, g->name);
    fprintf(o, "sd balance %d earned %d unlocks %08x shelved %08x locked %08x bg %d\n", (int)T.sd_balance, (int)T.sd_earned,
            T.sd_unlocks & 0x3FFFFFu, T.sd_shelved & 0x3FFFFFu, T.sd_locked & 0x3FFFFFu, (int)T.bg_now);
    for (int k = 0; k < 4; k++) fprintf(o, "critter %d name %s\n", k, T.crit_name[k]);
    fprintf(o, "breed owned %d slot0 %d slot1 %d\n", progression_breed_owned(&T), progression_breed_slot(&T, 0), progression_breed_slot(&T, 1));
    fprintf(o, "pieces castle %.1f %d coral %.1f %d %06x plant %.1f volcano %.1f cluster %.1f\n", T.castle_x, T.castle_z,
            T.coral_x, T.coral_z, T.coral_rgb & 0xffffff, T.plant_x, T.volcano_x, T.cluster_x);
    fprintf(o, "fish %d\n", T.n_fish);
    for (int i = 0; i < T.n_fish; i++) {
        const fish_t *f = &T.fish[i];
        fprintf(o, "fish %d name %s uid %08x parents %08x %08x preset %d stage %d genes %d %d %d color %06x fin %06x away %d eaten %d ms %08x trust %.3f from %u lived %.0f\n",
                i, f->name, f->uid, f->parent_uid[0], f->parent_uid[1], f->preset, f->stage, f->shape, f->fins, f->pattern,
                f->color & 0xffffff, f->fin & 0xffffff, f->away, (int)f->eaten, f->ms_bits, f->trust, progression_fish_from(&T, f), progression_lived_s(&T, i));
    }
    fprintf(o, "collection %d\n", collection_count());
    for (int i = 0; i < collection_count(); i++) { const coll_entry_t *e = collection_get(i); fprintf(o, "visitor %d uid %08x visits %u color %06x\n", i, e->uid, (unsigned)e->visits, e->color & 0xffffff); }
}
static void up_keep(const char *dir, const char *pre) {   /* this tank's save, as it stands after its boot */
    progression_save(&T);
    char cmd[1600]; snprintf(cmd, sizeof cmd, "cp '%s' '%s/%s-%u.bin'", getenv("POCKET_TANK_SAVE"), dir, pre, shelf_now()->id); (void)system(cmd);
}
static void up_board_as(FILE *o, const char *dir, bool (*swap)(int), const char *pre) {
    uint32_t home = shelf_now()->id, ids[16]; int n = shelf_count() < 16 ? shelf_count() : 16;
    for (int i = 0; i < n; i++) ids[i] = shelf_get(i)->id;
    fprintf(o, "glass %u shelf %d\n", home, n);
    up_tank(o); up_keep(dir, pre);
    for (int k = 0; k < n; k++) {
        int at = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == ids[k]) at = i;
        if (at < 0 || !swap(at)) { fprintf(o, "SWAP %u FAILED\n", ids[k]); continue; }
        up_tank(o); up_keep(dir, pre);
    }
    int at = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == home) at = i;
    if (n && (at < 0 || !swap(at))) fprintf(o, "SWAP BACK %u FAILED\n", home);
    fprintf(o, "back on %u\n", shelf_now()->id);
}
static inline void up_board(FILE *o, const char *dir, bool (*swap)(int)) { up_board_as(o, dir, swap, "sav"); }
