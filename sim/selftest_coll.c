#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_coll.c - the VISITOR COLLECTION's pages (0.4, Strato 11-03): the first page in NVS as it always was, pages
 * 2..10 in the tank's VISITOR BOOK (a slot of its own in storage, common/shelf.h).
 *
 *   ./fishsim --selftest-coll-pages     (make check, every world)
 *   ./fishsim --coll-shot <prefix>      the paged look: page 1 of a full collection, page 2 of 10, the last page with
 *                                       room, a visitor's passport with every page full (PPM, 1:1)
 *
 * Both run on a scratch board in $PT_SHELF_DIR (default /tmp/pt-shelf, the shelf selftests' own), never the sim's tank. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tank.h"
#include "progression.h"
#include "collection.h"
#include "shelf.h"
#include "render.h"
#include "advisor.h"

int shelf_port_sim_cut(int writes);                /* persist_port_sim.c: the power cut after n writes (-1 = never) */
void persist_port_sim_block(bool on);              /* ... the tank's own keys refusing writes (NVS full, a load gone wrong) */
const char *sim_shelf_dir(void);                   /* main.c: $PT_SHELF_DIR or /tmp/pt-shelf */

static tank_t s_tk;
#define CFAIL(...) do { printf("FAIL: coll-pages: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)

static void board_wipe(void) {
    char cmd[700], p[600]; const char *d = sim_shelf_dir();
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", d, d); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", d); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", d); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", d); setenv("POCKET_TANK_FRAME", p, 1);
}
static void board_boot(void) {                      /* the board restarting: the shelf, then the tank (its collection loads) */
    tank_init(&s_tk, 5);
    shelf_boot(clock_port_now_unix());
    progression_boot(&s_tk);
    progression_swap_carry(&s_tk);
}
/* a visitor in slot 0 of the tank, its own look: COLLECT on its passport */
static bool collect(int j) {
    fish_t *v = &s_tk.fish[N_FISH_MAX]; *v = s_tk.fish[0];
    v->uid = 0x77000000u + (uint32_t)j; v->stage = (stage_t)(1 + j % 3);
    v->color = 0x102030u * (uint32_t)(j % 7 + 1) & 0xffffff; v->shape = (uint8_t)(j % SHAPE_N); v->fins = (uint8_t)(j % FINS_N);
    snprintf(v->name, sizeof v->name, "v%d", j);
    s_tk.vis_used[0] = true; s_tk.vis_badge[0] = (int8_t)(j % 4); s_tk.vis_home[0] = 0x7100 + (uint32_t)j; s_tk.vis_since[0] = s_tk.clock;
    snprintf(s_tk.vis_home_name[0], sizeof s_tk.vis_home_name[0], "%s", j % 5 ? "" : "SAM'S REEF");
    bool ok = collection_add(&s_tk, 0);
    s_tk.vis_used[0] = false;
    return ok;
}
static int storage_books(int *slot) {               /* slots whose group 0 (sector 0 or 1) wears the book's magic, read off the "flash" */
    int n = 0; uint8_t m[4];
    for (int k = 0; k < (int)(shelf_port_size() / SHELF_SLOT_SIZE) && k < SHELF_SLOTS_MAX; k++)
        for (int s = 0; s < 2; s++)
            if (shelf_port_read((uint32_t)k * SHELF_SLOT_SIZE + (uint32_t)s * 4096, m, 4) && (m[0] | m[1] << 8 | m[2] << 16 | (uint32_t)m[3] << 24) == SHELF_BOOK_MAGIC) {
                if (slot) *slot = k; n++; break;
            }
    return n;
}
static uint32_t coll_sum(void) {                    /* who is in, in order, and what they did here */
    uint32_t h = 2166136261u;
    for (int i = 0; i < collection_count(); i++) {
        const coll_entry_t *e = collection_get(i);
        uint32_t v[5] = { e->uid, e->visits, e->fry, e->color, (uint32_t)e->home_name[0] };
        for (int k = 0; k < 5; k++) { h ^= v[k]; h *= 16777619u; }
    }
    return h;
}
static bool swap_to(int i) {                        /* SWAP IN, as main.c does it */
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    for (int k = 0; k < w * h; k++) thumb[k] = (uint16_t)k;
    if (!progression_save(&s_tk)) return false;
    progression_carry_t c; progression_carry_take(&s_tk, &c);
    return shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix());
}
static int shelf_of(uint32_t id) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == id) return i; return -1; }
static double ms_since(clock_t t0) { return (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC; }

/* inside the glass by `in` px: the bowl's circle, the watch's 100 px and the square's 40 px corner arcs, the rect's edges */
static bool in_glass(int x, int y, int in) {
    if (x < in || y < in || x >= TANK_W - in || y >= TANK_H - in) return false;
#if defined(TANK_ROUND)
    float dx = x + 0.5f - TANK_W / 2.0f, dy = y + 0.5f - TANK_H / 2.0f;
    return dx * dx + dy * dy <= (TANK_W / 2.0f - in) * (TANK_W / 2.0f - in);
#elif defined(TANK_WATCH) || defined(TANK_SQUARE)
    const float R = TANK_CORNER_R;
    float cx = x < R ? R : x > TANK_W - R ? TANK_W - R : (float)x, cy = y < R ? R : y > TANK_H - R ? TANK_H - R : (float)y;
    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
    return dx * dx + dy * dy <= (R - in) * (R - in);
#else
    return true;
#endif
}

int selftest_coll_pages(void) {
    board_wipe(); board_boot();
    const uint32_t A = shelf_now()->id;
    if (collection_count() || collection_pages() != 1 || collection_full()) CFAIL("a new board: %d collected, %d pages", collection_count(), collection_pages());
    if (render_collection_arrow(true, NULL, NULL)) CFAIL("arrows with one page");
    /* 1. the first page: as before pages - the "visitors" blob, no book */
    for (int j = 0; j < COLL_PAGE; j++) if (!collect(j)) CFAIL("COLLECT %d refused", j);
    uint8_t blob[2048]; size_t got = 0;
    if (!persist_port_blob_load(COLL_NVS_KEY, blob, sizeof blob, &got) || got != COLL_BLOB_MAX || blob[3] != COLL_PAGE) CFAIL("the first page's blob is %zu B (want %d)", got, COLL_BLOB_MAX);
    if (shelf_book_count() || storage_books(NULL)) CFAIL("a book with 12 collected");
    if (collection_pages() != 2 || collection_full()) CFAIL("12 collected: %d pages (want 2: the next has room), full %d", collection_pages(), collection_full());
    /* 2. pages 2..10: the book (one slot), the first page's blob never grows; the 121st refused */
    clock_t t0 = clock(); int writes = 0;
    for (int j = COLL_PAGE; j < COLL_MAX; j++) { if (!collect(j)) CFAIL("COLLECT %d refused", j); writes++; }
    double add_ms = ms_since(t0) / writes;
    if (collection_count() != COLL_MAX || !collection_full() || collection_pages() != COLL_PAGES) CFAIL("120: %d collected, %d pages, full %d", collection_count(), collection_pages(), collection_full());
    if (collect(COLL_MAX)) CFAIL("the 121st was collected");
    if (shelf_book_count() != 1 || storage_books(NULL) != 1) CFAIL("%d books (%d on the flash), want 1", shelf_book_count(), storage_books(NULL));
    if (!persist_port_blob_load(COLL_NVS_KEY, blob, sizeof blob, &got) || got != COLL_BLOB_MAX || blob[3] != COLL_PAGE) CFAIL("the first page's blob grew to %zu B / %d", got, blob[3]);
    if (shelf_free_places() != (int)(shelf_port_size() / SHELF_SLOT_SIZE) - 2) CFAIL("the book is not a used slot: %d places free", shelf_free_places());
    uint32_t sum = coll_sum();
    /* 3. a reboot: all 120 back, in order */
    board_boot();
    if (collection_count() != COLL_MAX || coll_sum() != sum) CFAIL("after a reboot: %d collected (or changed)", collection_count());
    if (strcmp(collection_get(100)->name, "v100") || strcmp(collection_get(5)->home_name, "SAM'S REEF") || strcmp(collection_get(55)->home_name, "SAM'S REEF"))
        CFAIL("the entries read back wrong: '%s' '%s' '%s'", collection_get(100)->name, collection_get(5)->home_name, collection_get(55)->home_name);
    /* 4. a moment on page 7 saved in its own group (a visit, a fry), kept over a reboot */
    uint32_t u80 = collection_get(80)->uid; int v80 = collection_get(80)->visits;
    collection_visit(u80); t0 = clock(); collection_fry(u80); double fry_ms = ms_since(t0);
    board_boot();
    if (collection_get(80)->visits != v80 + 1 || collection_get(80)->fry != 1) CFAIL("page 7's visit / fry lost (%d visits, %d fry)", collection_get(80)->visits, collection_get(80)->fry);
    sum = coll_sum();
    /* 5. the power cut mid book write: the older copy reads (the fry before the cut), nothing else lost */
    uint32_t u50 = collection_get(50)->uid;
    shelf_port_sim_cut(1); collection_fry(u50); shelf_port_sim_cut(-1);   /* (the erase went through, the record did not) */
    board_boot();
    if (collection_count() != COLL_MAX || coll_sum() != sum) CFAIL("a cut mid book write: %d collected (or the older copy did not read)", collection_count());
    collection_fry(u50); board_boot();
    if (collection_get(50)->fry != 1) CFAIL("after the cut, the next write did not land (%d fry)", collection_get(50)->fry);
    sum = coll_sum();
    /* 6. a swap out and back: the book stays where it is, each tank reads its own */
    int b = shelf_new(0, clock_port_now_unix());
    if (b < 0) CFAIL("no new tank");
    uint32_t B = shelf_get(b)->id;
    if (!swap_to(b)) CFAIL("the swap to tank %u", (unsigned)B);
    board_boot();
    if (shelf_now()->id != B || collection_count() != 0 || shelf_book_count() != 1) CFAIL("tank B on the glass: %d collected, %d books", collection_count(), shelf_book_count());
    for (int j = 0; j < COLL_PAGE + 3; j++) if (!collect(500 + j)) CFAIL("tank B's COLLECT %d", j);
    if (shelf_book_count() != 2) CFAIL("tank B's book: %d books", shelf_book_count());
    if (!swap_to(shelf_of(A))) CFAIL("the swap back to tank A");
    board_boot();
    if (shelf_now()->id != A || collection_count() != COLL_MAX || coll_sum() != sum) CFAIL("tank A back: %d collected (or changed)", collection_count());
    /* 7. a tank removed: its book with it */
    if (!shelf_remove(shelf_of(B)) || shelf_book_count() != 1 || storage_books(NULL) != 1) CFAIL("tank B removed: %d books (%d on the flash)", shelf_book_count(), storage_books(NULL));
    /* 8. an older build wrote a tank over the book (a rollback's swap): the first page kept, the pages after it gone, the
       collection goes on (a new book) */
    int slot = -1; storage_books(&slot);
    shelf_port_erase((uint32_t)slot * SHELF_SLOT_SIZE, SHELF_SLOT_SIZE);
    board_boot();
    if (collection_count() != COLL_PAGE || collection_full() || collection_pages() != 2) CFAIL("the book overwritten: %d collected, %d pages", collection_count(), collection_pages());
    if (!collect(COLL_MAX + 1) || shelf_book_count() != 1 || collection_count() != COLL_PAGE + 1) CFAIL("a new book after the old was lost");
    /* 9. a reset under an older build (the first page another, the book this tank's): never read as its own; the next
       COLLECT writes over it */
    persist_port_blob_forget(COLL_NVS_KEY);         /* (the older build's reset: the namespace erased, the book left) */
    board_boot();
    if (collection_count() != 0) CFAIL("no first page: %d collected", collection_count());
    for (int j = 0; j < COLL_PAGE; j++) collect(3000 + j);
    if (collection_count() != COLL_PAGE) CFAIL("a new first page: %d", collection_count());
    board_boot();
    if (collection_count() != COLL_PAGE || shelf_book_count() != 1) CFAIL("a stale book was read as the new first page's (%d collected, %d books)", collection_count(), shelf_book_count());
    if (!collect(3100) || shelf_book_count() != 1 || storage_books(NULL) != 1) CFAIL("the stale book was not written over (%d books)", shelf_book_count());
    board_boot();
    if (collection_count() != COLL_PAGE + 1) CFAIL("the book written over: %d collected", collection_count());
    /* 10. the director's park: the staged tank never sees nor erases it; back, it reads again */
    sum = coll_sum();
    if (shelf_book_rekey(A, A | SHELF_BOOK_PARKED) != 1) CFAIL("park");
    persist_port_blob_forget(COLL_NVS_KEY); board_boot();
    if (collection_count() || shelf_book_count() != 1) CFAIL("parked: the staged tank sees %d, %d books", collection_count(), shelf_book_count());
    collection_stage(0, 1);                          /* (the director's `coll fill` over the staged tank: a reset of its own, the parked book kept) */
    if (shelf_book_count() != 1) CFAIL("a staged fill erased the parked book");
    for (int j = 0; j < COLL_PAGE + 1; j++) collect(3000 + j);   /* the staged tank: the same first page as the parked book's, and a 13th */
    if (shelf_book_count() != 2) CFAIL("the staged tank's own book: %d books", shelf_book_count());
    shelf_book_forget(A);
    if (shelf_book_rekey(A | SHELF_BOOK_PARKED, A) != 1 || shelf_book_count() != 1) CFAIL("back: %d books", shelf_book_count());
    board_boot();
    if (collection_count() != COLL_PAGE + 1 || coll_sum() != sum) CFAIL("back from the park: %d collected (or changed)", collection_count());
    /* 11. a book whose tank is gone (an older build removed it): the boot erases it */
    uint8_t *bb = shelf_book_buf(); memset(bb, 0, 76); bb[0] = 1;
    if (!shelf_book_write(777, 12345, 0, 1, 76) || shelf_book_count() != 2) CFAIL("an orphan book");
    board_boot();
    if (shelf_book_count() != 1 || storage_books(NULL) != 1) CFAIL("the orphan book was not erased (%d)", shelf_book_count());
    /* 12. no room on the board for a book (one slot stays free for swaps): FULL at the first page */
    collection_reset(); for (int j = 0; j < COLL_PAGE; j++) collect(4000 + j);
    while (shelf_free_places() > 0) if (shelf_new(0, clock_port_now_unix()) < 0) CFAIL("filling the shelf");
    if (!collection_full() || collection_pages() != 1 || collect(4100)) CFAIL("no room for a book: full %d, %d pages", collection_full(), collection_pages());
    /* 13. the keeper's reset: the book with it - and one the director parked (0.4 rc: the reset erases the parked save, and
       the parked book outlived it, its slot never free again) */
    collection_reset(); while (shelf_count()) shelf_remove(0);
    for (int j = 0; j < COLL_PAGE + 5; j++) collect(5000 + j);
    if (shelf_book_count() != 1) CFAIL("before the reset: %d books", shelf_book_count());
    if (shelf_book_rekey(shelf_now()->id, shelf_now()->id | SHELF_BOOK_PARKED) != 1) CFAIL("parking the book before the reset");
    if (!collect(5100) || shelf_book_count() != 2) CFAIL("the tank's own book beside the parked one: %d books", shelf_book_count());
    collection_reset();
    if (shelf_book_count() || storage_books(NULL)) CFAIL("the reset left a book (%d, %d on the flash: the parked one?)", shelf_book_count(), storage_books(NULL));
    /* 13b. a refused first page (0.4 rc: it still said COLLECTED, and the visitor was gone at the next boot): not collected,
       and a change the flash refused waits for the next save */
    for (int j = 0; j < 3; j++) collect(7000 + j);
    uint32_t s3 = coll_sum();
    persist_port_sim_block(true); bool took = collect(7100); persist_port_sim_block(false);
    if (took || collection_count() != 3 || coll_sum() != s3) CFAIL("a refused first page: COLLECT said %d, %d collected", took, collection_count());
    board_boot();
    if (collection_count() != 3 || coll_sum() != s3) CFAIL("after the refused COLLECT and a reboot: %d collected (or changed)", collection_count());
    const int v7 = collection_get(collection_find(0x77000000u + 7000))->visits;
    collection_visit(0x77000000u + 7000);
    persist_port_sim_block(true); collection_flush(); persist_port_sim_block(false);
    collection_flush(); board_boot();
    if (collection_get(collection_find(0x77000000u + 7000))->visits != v7 + 1) CFAIL("a visit saved while the flash refused was dropped, not kept for the next save (%d visits, want %d)",
                                                                                     collection_get(collection_find(0x77000000u + 7000))->visits, v7 + 1);
    collection_reset();
    /* 14. the page: the arrows inside the glass and wrapping, a tap on page 2's grid is page 2's fish, a page draw's time */
    collection_stage(115, 3);
    if (collection_count() != 115 || collection_pages() != 10) CFAIL("staged 115: %d, %d pages", collection_count(), collection_pages());
    static uint16_t fb[TANK_W * TANK_H];
    render_collection_leave();
    int ax, ay, bx2, by2;
    if (!render_collection_arrow(false, &ax, &ay) || !render_collection_arrow(true, &bx2, &by2)) CFAIL("no arrows on 10 pages");
    const int h = RENDER_CRIT_NAV_W / 2;
    int cs[4][2] = { { ax - h, ay - h }, { ax + h, ay - h }, { bx2 - h, by2 - h }, { bx2 + h, by2 - h } };
    for (int k = 0; k < 4; k++) if (!in_glass(cs[k][0], cs[k][1], 6)) CFAIL("an arrow's corner (%d,%d) is not 6 px inside the glass", cs[k][0], cs[k][1]);
    render_collection_tap(&s_tk, (float)ax, (float)ay);
    if (render_collection_page() != 9) CFAIL("the left arrow from page 1 went to page %d (want 10: it wraps)", render_collection_page() + 1);
    render_collection_tap(&s_tk, (float)bx2, (float)by2); render_collection_tap(&s_tk, (float)bx2, (float)by2);
    if (render_collection_page() != 1) CFAIL("right twice from page 10: page %d (want 2)", render_collection_page() + 1);
    render_collection(&s_tk, fb, TANK_W);
    t0 = clock(); for (int k = 0; k < 50; k++) render_collection(&s_tk, fb, TANK_W);
    double draw_ms = ms_since(t0) / 50;
    if (render_collection_tap(&s_tk, PAGE_X + 40 + 46, PAGE_Y + 60 + 40) != COLL_TAP_KEPT) CFAIL("a tap on page 2's first place");
    render_collection_tap(&s_tk, 0, 0);
    render_collection_leave();
    if (render_collection_page() != 0) CFAIL("the page did not open on page 1 again");
    collection_reset();
    printf("selftest-coll-pages: the first page the 918-B blob it was, pages 2..10 one book slot (120, the 121st refused); a "
           "reboot, a cut mid-write, a swap out and back, a tank removed, a book written over, a stale and a parked book, an orphan, "
           "no room (FULL at 12), the reset (a parked book with it), a refused first page; arrows inside the glass and wrapping | sim: a COLLECT into the book %.2f ms, a fry's "
           "group write %.2f ms, a page draw %.2f ms\n", add_ms, fry_ms, draw_ms);
    return 0;
}

/* ---- --coll-shot <prefix> ---- */
static void write_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t p = FB_SWAP16(fb[i]);                 /* (as main.c's: the swap build's frame is in the panel's order) */
        uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
int coll_shot(const char *prefix) {
    static uint16_t fb[TANK_W * TANK_H], scene[TANK_W * TANK_H], card[RENDER_CARD_W * RENDER_CARD_H], give[GIFT_CARD_W * GIFT_CARD_H], gift[GIFT_CARD_W * GIFT_CARD_H];
    static uint8_t vig[TANK_W * TANK_H]; static uint32_t dirty[RENDER_DIRTY_WORDS];
    char path[600];
    board_wipe(); board_boot();
    tank_init(&s_tk, 2026); tank_new_population(&s_tk); tank_ensure_ids(&s_tk);
    for (int i = 0; i < 2 * 60; i++) tank_tick(&s_tk, 1.0f / 60.0f, advisor_rules);
    render_set_scene_cache(scene); render_set_vignette_cache(vig); render_set_card_cache(card); render_set_modal_cache(give, gift); render_set_dirty_mask(dirty);
    collection_stage(COLL_MAX, 11);
    render_collection_leave(); render_collection(&s_tk, fb, TANK_W);
    snprintf(path, sizeof path, "%s_page1_of10_full.ppm", prefix); write_ppm(path, fb);
    render_collection_set_page(1); render_collection(&s_tk, fb, TANK_W);
    snprintf(path, sizeof path, "%s_page2_of10.ppm", prefix); write_ppm(path, fb);
    collection_stage(115, 12);
    render_collection_leave(); render_collection_set_page(9); render_collection(&s_tk, fb, TANK_W);
    snprintf(path, sizeof path, "%s_page10_of10_115.ppm", prefix); write_ppm(path, fb);
    collection_stage(COLL_MAX, 11);                 /* a visitor's passport with every page full: FULL */
    fish_t *v = &s_tk.fish[N_FISH_MAX]; *v = s_tk.fish[0];
    v->x = TANK_W * 0.62f; v->y = TANK_BOT * 0.42f; v->color = 0x9b72ff; v->accent = 0xffd166; v->stage = STAGE_ADULT;
    v->shape = SHAPE_SLENDER; v->fins = FINS_VEIL; snprintf(v->name, sizeof v->name, "rio"); v->uid = 0x383bbe99;
    s_tk.vis_used[0] = true; s_tk.vis_badge[0] = 1; s_tk.vis_home[0] = 0x113afc56; s_tk.vis_since[0] = s_tk.clock - 245; s_tk.link_badge = 1;
    render_tank(&s_tk, fb, TANK_W); render_stats_card(&s_tk, N_FISH_MAX, fb, TANK_W);
    snprintf(path, sizeof path, "%s_passport_full.ppm", prefix); write_ppm(path, fb);
    s_tk.vis_used[0] = false; render_collection_leave(); collection_reset();
    printf("coll-shot: %s_{page1_of10_full,page2_of10,page10_of10_115,passport_full}.ppm\n", prefix);
    return 0;
}
