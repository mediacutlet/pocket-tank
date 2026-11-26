/* linkver_test.inc - --selftest-linkver (0.4.3, Strato 11-23): the link beacon carries this tank's release (version.h, three
 * bytes after the edge peers). A tank reads it back as the number; a beacon without them - a 0.4.2 tank's, a 0.4.3 dev
 * build's, or one cut short - reads as "not known" (0), and every other field as before. The 0.4.2 side, run with 0.4.2's
 * own link.c (4dae3468), is in the report: it reads this longer beacon exactly as the short one. */
static uint8_t bv_b[LINK_FRAME_MAX]; static int bv_n;
static void bv_send(void *ctx, uint32_t to, const uint8_t *b, int n) { (void)ctx; (void)to; if (n > 3 && b[3] == 1 /* FR_BEACON */) { memcpy(bv_b, b, n); bv_n = n; } }
static int bv_room(void *ctx) { (void)ctx; return 2; }
static int bv_hex(const char *hx, uint8_t *b) { int n = 0; for (; n < LINK_FRAME_MAX && sscanf(hx + 2 * n, "%2hhx", &b[n]) == 1; n++) {} return n; }
int selftest_linkver(void) {
    link_t *A = calloc(1, (size_t)link_size()), *B = calloc(1, (size_t)link_size());
    if (!A || !B) { free(A); free(B); printf("FAIL: selftest-linkver: no memory\n"); return 1; }
    link_host_t h; memset(&h, 0, sizeof h); h.send = bv_send; h.room = bv_room;
    int fails = 0;
#define VCHECK(c, ...) do { if (!(c)) { printf("FAIL: selftest-linkver: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
    const uint32_t ME = 0x04300043u, WANT = (uint32_t)PT_RELEASE_NUM & ~0xffu;
    link_init(A, ME, &h, 7, 1000); link_mode(A, true); link_arm(A, LINK_LEFT); link_arm(A, LINK_RIGHT);
    bv_n = 0; for (uint32_t t = 1000; t < 6000 && !bv_n; t += 10) link_tick(A, t);
    VCHECK(bv_n == 12 + 13 + 3, "this tank's beacon is %d bytes (want 28: 0.4.2's 25 and the release)", bv_n);
    if (bv_n >= 28) VCHECK(bv_b[25] == PT_RELEASE_MAJOR && bv_b[26] == PT_RELEASE_MINOR && bv_b[27] == PT_RELEASE_PATCH,
                           "the beacon's release %d.%d.%d (want %s)", bv_b[25], bv_b[26], bv_b[27], PT_RELEASE);
    uint8_t full[LINK_FRAME_MAX]; const int nfull = bv_n; memcpy(full, bv_b, (size_t)nfull);
    /* read whole: the number */
    link_init(B, 0x0bbbbbbbu, &h, 9, 1000); link_rx(B, full, nfull, -40, 1100);
    VCHECK(link_peer_release(B, ME) == WANT, "read back %08x (want %08x)", (unsigned)link_peer_release(B, ME), (unsigned)WANT);
    VCHECK(link_peer_swap(B, ME), "the long beacon lost BF_SWAP");
    /* cut short (as a 0.4.3 dev build sends it, or one, two bytes of it): not known, the rest the same */
    for (int cut = 3; cut >= 1; cut--) {
        link_init(B, 0x0bbbbbbbu, &h, 9, 1000); link_rx(B, full, nfull - cut, -40, 1100);
        VCHECK(link_peer_release(B, ME) == 0, "a beacon %d bytes short read %08x (want 0: not known)", cut, (unsigned)link_peer_release(B, ME));
        VCHECK(link_peer_swap(B, ME), "a beacon %d bytes short lost its other fields (BF_SWAP)", cut);
    }
    /* a later beacon without it (a downgrade): not known again, never the old number */
    link_init(B, 0x0bbbbbbbu, &h, 9, 1000); link_rx(B, full, nfull, -40, 1100); link_rx(B, full, nfull - 3, -40, 1200);
    VCHECK(link_peer_release(B, ME) == 0, "a short beacon after a long one kept %08x", (unsigned)link_peer_release(B, ME));
    /* zeros where the release goes: not known */
    { uint8_t z[LINK_FRAME_MAX]; memcpy(z, full, (size_t)nfull); z[25] = z[26] = z[27] = 0;
      link_init(B, 0x0bbbbbbbu, &h, 9, 1000); link_rx(B, z, nfull, -40, 1100);
      VCHECK(link_peer_release(B, ME) == 0, "a 0.0.0 release read %08x", (unsigned)link_peer_release(B, ME)); }
    /* a real 0.4.2 tank's beacon (4dae3468's link.c: link mode, both doors armed - audit-0.4.3-evidence/linkver/b042 tx) */
    { uint8_t o[LINK_FRAME_MAX]; const int no = bv_hex("4c4b0101420020040000000000050302000000000000000000", o);
      link_init(B, 0x0bbbbbbbu, &h, 9, 1000); link_rx(B, o, no, -40, 1100);
      link_peer_info_t pi[LINK_PEERS_MAX]; int np = link_peers(B, pi, LINK_PEERS_MAX);
      VCHECK(no == 25 && np == 1 && pi[0].tank == 0x04200042u && pi[0].link_mode, "0.4.2 beacon read wrong (n %d, peers %d, tank %08x badge %d mode %d)", no, np, np ? (unsigned)pi[0].tank : 0, np ? pi[0].badge : -1, np ? pi[0].link_mode : -1);
      VCHECK(link_peer_release(B, 0x04200042u) == 0 && !link_peer_swap(B, 0x04200042u), "0.4.2's beacon read as release %08x / swaps %d",
             (unsigned)link_peer_release(B, 0x04200042u), link_peer_swap(B, 0x04200042u)); }
    /* a tank acting as 0.4.2 (the selftests' stand-in) sends 0.4.2's 25 bytes, nothing after */
    link_init(A, ME, &h, 7, 1000); link_test_as_042(A, true); link_mode(A, true); link_arm(A, LINK_LEFT); link_arm(A, LINK_RIGHT);
    bv_n = 0; for (uint32_t t = 1000; t < 6000 && !bv_n; t += 10) link_tick(A, t);
    VCHECK(bv_n == 25, "a tank acting as 0.4.2 sent %d bytes (want 25)", bv_n);
    if (getenv("LINKVER_HEX")) { for (int i = 0; i < nfull; i++) printf("%02x", full[i]); printf("\n"); }   /* (for 0.4.2's b042 rx) */
    free(A); free(B);
    if (fails) return 1;
    printf("selftest-linkver: the beacon carries %s (28 B, was 25); read back as %08x; cut short, zero, or 0.4.2's own beacon: not known, the rest as before\n", PT_RELEASE, (unsigned)WANT);
    return 0;
#undef VCHECK
}
