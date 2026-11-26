/* linkfix_test.inc - --selftest-linkfix: the 0.4.3 audit round 1 fixes for paragons and lab fish over the tank link
 * (docs/audit-0.4.3-r1.md; the probes they grew from are v6_test.inc's, the audit's verify/V6). Each part fails on b121cf6d.
 *   SLB-2  a lab fish given (its passport through the air) still says THE LAB; a shop fry still says THE SHOP; a visitor's copy
 *          passes the mark on (GIVE where it swims sends the host's copy)
 *   SLB-3  which fish only a 0.4.3 tank can take whole (progression_fish_needs_new) - the link side is --selftest-doors 7c2
 *   SLB-4  a fry from next door wears no part and is never READY; an honest fry's family places come through
 *   SV-5   the save: below the paragon stage no part and never READY, family places kept, an elder's unknown part kept
 *   SLB-5  a body from the air is held to the creator's area; one the creator made comes through to the byte
 *   SLP-3  a stage reached as a visitor, then kept where it swims, is paid here and gets its card; one paid before is not
 *   TP-7   KEEP is refused for a fish next door; at home it keeps
 *   (audit r2) R2-L1 a passport's stage with no reached marks mints nothing; R2-L2 KEEP refused in the doorway; R2-L3 a family
 *   place needs a 0.4.3 tank. R2-3 and R2-L4 are --selftest-doors 7c2 / 7c3 */
#define LF_FAIL(...) do { printf("FAIL: selftest-linkfix: " __VA_ARGS__); printf("\n"); return 1; } while (0)
static int lf_home(uint32_t uid) { for (int i = 0; i < tank.n_fish; i++) if (tank.fish[i].uid == uid) return i; return -1; }
static bool lf_air(const passport_t *in, passport_t *out) {       /* through the air: written by this build, read back */
    uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(in, buf, sizeof buf);
    return n == PASSPORT_V5_BYTES && passport_read(out, buf, n);
}
#include "swap.h"
static int selftest_linkfix(void) {
    char l1[64]; const char *l2;
    /* ---- SLB-2: THE LAB travels ---- */
    v6_setup();
    progression_fry_open(&tank);
    tank.sd_balance = 1500 + 250;
    if (!progression_lab_buy(&tank)) LF_FAIL("SLB-2: the lab would not open");
    {   fish_t r = tank.fish[0]; snprintf(r.name, FISH_NAME_MAX + 1, "lab"); tank_set_body(&r, 19, 10);
        int s = progression_lab_pour(&tank, &r);
        if (s < 0 || !progression_from_lab(&tank, s) || !tank.fish[s].lab) LF_FAIL("SLB-2: the poured fish is not the lab's (slot %d)", s);
        passport_t pp, rd; passport_from_fish(&pp, &tank.fish[s], tank.uid, 3600);
        if (!lf_air(&pp, &rd) || !rd.lab) LF_FAIL("SLB-2: the lab's mark did not travel (lab %d)", rd.lab);
        fish_t host; memset(&host, 0, sizeof host); passport_to_fish(&rd, &host);   /* a visit: the host's copy, then GIVE where it swims */
        passport_t hp, hr; passport_from_fish(&hp, &host, 0x5eed1234u, 3600);
        if (!host.lab || !lf_air(&hp, &hr) || !hr.lab) LF_FAIL("SLB-2: a visitor's copy dropped the lab's mark");
        const uint32_t uid = tank.fish[s].uid;
        if (!progression_give_out(&tank, s)) LF_FAIL("SLB-2: give_out refused");
        hr.home_tank = 0x5eed1234u;
        int g = progression_gift_adopt(&tank, &hr);
        if (g < 0 || tank.fish[g].uid != uid) LF_FAIL("SLB-2: the gift was not taken in");
        setup_born_lines(&tank, g, l1, sizeof l1, &l2);
        if (!progression_from_lab(&tank, g) || !strstr(l1, "THE LAB") || strcmp(setup_from_name(&tank, g, 0), "THE LAB"))
            LF_FAIL("SLB-2: given, it reads '%s' / family '%s' (want THE LAB)", l1, setup_from_name(&tank, g, 0));
        /* (sibling) a shop fry given still says THE SHOP */
        int b = tank.n_fish; tank_make_fish(&tank, b, 0, 0.5f, 0.5f, STAGE_FRY); tank.n_fish++;
        tank.fish[b].uid = 0x51a90001u; tank.fish[b].ms_bits = MS_ARRIVED | MS_BOUGHT; tank.fish[b].parent_uid[0] = tank.fish[b].parent_uid[1] = 0;
        tank.fish[b].parent_a = tank.fish[b].parent_b = -1;
        passport_t sp, sr; passport_from_fish(&sp, &tank.fish[b], tank.uid, 60);
        if (!lf_air(&sp, &sr) || sr.lab) LF_FAIL("SLB-2: a shop fry's passport says the lab");
        progression_give_out(&tank, b); sr.home_tank = 0x5eed1234u;
        int g2 = progression_gift_adopt(&tank, &sr);
        setup_born_lines(&tank, g2, l1, sizeof l1, &l2);
        if (g2 < 0 || progression_from_lab(&tank, g2) || !strstr(l1, "THE SHOP")) LF_FAIL("SLB-2: a shop fry given reads '%s' (want THE SHOP)", l1);
        printf("selftest-linkfix: SLB-2 a lab fish given (and given on from a visit) says THE LAB; a shop fry says THE SHOP\n");
    }
    /* ---- SLB-3: who needs a 0.4.3 tank ---- */
    v6_setup();
    {   fish_t *f = &tank.fish[1];
        if (progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a plain fish needs a new tank");
        f->evo = EVO_HORN; f->stage = STAGE_ELDER;
        if (!progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a paragon does not");
        f->evo = 0; f->evo_offer = (uint16_t)(EVO_LURE | EVO_HUMP << 5 | EVO_HORN << 10);
        if (!tank_evo_ready(f) || !progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a READY fish does not");
        f->evo_offer = 0; tank_set_body(f, 19, 10);
        if (!progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a stretched body does not");
        tank_clear_body(f); f->lab = 1;
        if (!progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a lab fish does not");
        f->lab = 0;
        f->body_c[0] = (uint8_t)(19 * BODY_Q); f->body_c[1] = (uint8_t)(10 * BODY_Q);   /* (the review of 11-22) carried only: its young would lose it */
        if (!progression_fish_needs_new(&tank, 1)) LF_FAIL("SLB-3: a carried stretched body does not");
        f->body_c[0] = f->body_c[1] = 0;
        printf("selftest-linkfix: SLB-3 a paragon, a READY fish, a lab fish and a stretched body need a 0.4.3 tank; a plain fish does not\n");
    }
    /* ---- SLB-4: a fry from next door ---- */
    v6_setup();
    {   static const struct { uint16_t w; uint16_t want; bool ready; const char *what; } C[4] = {
            { (uint16_t)(7 | 31u << 5 | 31u << 10), 0, false, "a part worn" },
            { (uint16_t)(2 | 4 << 5 | 6 << 10), (uint16_t)(2 | 4 << 5), false, "three offered" },
            { (uint16_t)(2 | 4 << 5 | 6 << 10 | EVO_SHOWN), (uint16_t)(2 | 4 << 5), false, "three offered, shown" },
            { (uint16_t)(3 | 5 << 5), (uint16_t)(3 | 5 << 5), false, "two family places (honest)" } };
        for (int c = 0; c < 4; c++) {
            passport_t pp, rd; passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, 0);
            pp.uid = 0x77700000u + c; pp.stage = STAGE_ELDER; pp.parent_uid[0] = tank.fish[0].uid; pp.parent_uid[1] = 0x5eed9999u;
            pp.evo = C[c].w;
            if (!lf_air(&pp, &rd)) LF_FAIL("SLB-4: read failed");
            int s = progression_fry_adopt(&tank, &rd, 0);
            if (s < 0) LF_FAIL("SLB-4: fry_adopt refused");
            fish_t *f = &tank.fish[s];
            if (f->evo || tank_evo_ready(f) || f->evo_offer != C[c].want || progression_fish_value(&tank, s) != SD_FISH_FRY * 2)
                LF_FAIL("SLB-4 %s: fry evo %d ready %d offer %04x (want %04x) worth %d", C[c].what, f->evo, tank_evo_ready(f), f->evo_offer, C[c].want, progression_fish_value(&tank, s));
            progression_give_out(&tank, s);
        }
        printf("selftest-linkfix: SLB-4 a fry from next door wears no part and is never READY; two family places come through\n");
    }
    /* ---- SV-5: the save ---- */
    v6_setup();
    {   const uint32_t ua = tank.fish[1].uid, ub = tank.fish[2].uid, ue = tank.fish[0].uid;
        if (tank.fish[1].stage >= STAGE_ELDER || tank.fish[2].stage >= STAGE_ELDER || tank.fish[0].stage != STAGE_ELDER) LF_FAIL("SV-5: the cast moved");
        tank.fish[1].evo = EVO_HORN; tank.fish[1].evo_offer = 0;                                /* an adult wearing a part */
        tank.fish[2].evo = 0; tank.fish[2].evo_offer = (uint16_t)(EVO_LURE | EVO_HUMP << 5 | EVO_SWORD << 10 | EVO_SHOWN);   /* an adult READY */
        tank.fish[0].evo_offer = 0; tank.fish[0].evo = 25;                                                                 /* an elder in a part a later release added */
        if (!progression_save(&tank)) LF_FAIL("SV-5: the save refused");
        shelf_reboot();
        int a = lf_home(ua), b = lf_home(ub), e = lf_home(ue);
        if (a < 0 || b < 0 || e < 0) LF_FAIL("SV-5: a fish did not come back");
        if (tank.fish[a].evo || progression_fish_value(&tank, a) != SD_FISH_ADULT) LF_FAIL("SV-5: an adult loaded in a part (%d), worth %d", tank.fish[a].evo, progression_fish_value(&tank, a));
        if (tank_evo_ready(&tank.fish[b]) || tank.fish[b].evo_offer != (uint16_t)(EVO_LURE | EVO_HUMP << 5)) LF_FAIL("SV-5: an adult loaded READY (offer %04x)", tank.fish[b].evo_offer);
        if (tank.fish[e].evo != 25) LF_FAIL("SV-5: an elder's unknown part was not kept (%d)", tank.fish[e].evo);
        printf("selftest-linkfix: SV-5 loaded, an adult wears no part and is not READY (its two family places kept); an elder's unknown part is kept\n");
    }
    /* ---- SLB-5: the body's area ---- */
    v6_setup();
    {   static const float BL[4][2] = { { 21, 15 }, { 13, 6.5f }, { 21, 11.7f }, { 13, 15 } };
        for (int c = 0; c < 4; c++) {
            passport_t pp, rd; passport_from_fish(&pp, &tank.fish[0], 0x5eed1234u, 3600);
            pp.stretch[0] = (uint8_t)(BL[c][0] * BODY_Q + 0.5f); pp.stretch[1] = (uint8_t)(BL[c][1] * BODY_Q + 0.5f);
            pp.stretch[2] = pp.stretch[3] = 0;
            if (!lf_air(&pp, &rd)) LF_FAIL("SLB-5: read failed");
            fish_t f = tank.fish[0]; passport_to_fish(&rd, &f);
            float l, d; tank_body(&f, &l, &d);
            fish_t m = tank.fish[0]; tank_set_body(&m, BL[c][0], BL[c][1]);      /* what the creator makes of the same two numbers */
            if (f.body[0] != m.body[0] || f.body[1] != m.body[1]) LF_FAIL("SLB-5: %.1f x %.1f from the air reads %.1f x %.1f (the creator: %.1f x %.1f)",
                                                                           BL[c][0], BL[c][1], l, d, m.body[0] / BODY_Q, m.body[1] / BODY_Q);
        }
        /* (the review of 11-22) bodies the creator stores off the tenth grid at its trimmed corners come through to the byte */
        static const float RB[6][2] = { { 13.54f, 7.534f }, { 19.46f, 12.67f }, { 13.04f, 7.83f }, { 20.96f, 11.77f }, { 16.95f, 6.02f }, { 14.05f, 17.5f } };
        for (int c = 0; c < 6; c++) {
            fish_t m = tank.fish[0]; tank_set_body(&m, RB[c][0], RB[c][1]);
            passport_t pp, rd; passport_from_fish(&pp, &m, 0x5eed1234u, 3600);
            if (!lf_air(&pp, &rd)) LF_FAIL("SLB-5: read failed");
            fish_t f = tank.fish[0]; passport_to_fish(&rd, &f);
            if (f.body[0] != m.body[0] || f.body[1] != m.body[1]) LF_FAIL("SLB-5: the creator's %.2f x %.2f (%d, %d) came through as (%d, %d)",
                                                                           RB[c][0], RB[c][1], m.body[0], m.body[1], f.body[0], f.body[1]);
        }
        printf("selftest-linkfix: SLB-5 a body from the air is the creator's for the same numbers (21 x 15 -> 21 x 11.7, 13 x 6.5 -> 13 x 7.8; its own unchanged)\n");
    }
    /* ---- SLP-3: a stage reached as a visitor, kept where it swims ---- */
    for (int c = 0; c < 2; c++) {
        v6_setup();
        notice_sync(&tank);
        passport_t pp; passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, (float)(STAGE_ADULT_AGE + 30));
        pp.uid = 0x66600000u + c; pp.stage = c ? STAGE_ADULT : STAGE_JUV;
        pp.ms_bits = c ? (MS_REACHED_JUV | MS_REACHED_ADULT) : MS_REACHED_JUV;       /* c 0: it grew up here as a visitor; c 1: at home, paid there */
        int g = progression_gift_adopt(&tank, &pp);
        if (g < 0) LF_FAIL("SLP-3: adopt refused");
        const bool paid0 = (tank.sd_paid_fish[g] & SD_PAID_ADULT) != 0;   /* (the step is paid by sd_tick, which sets the bit as it pays) */
        bool card = false;
        for (int k = 0; k < 60 * 12; k++) {
            progression_tick(&tank, 1.0f / 60.0f); notice_tick(&tank, 1.0f / 60.0f, false);
            const notice_t *n = notice_current();
            if (n) { if (n->kind == NOTICE_STAGE && n->fish == g) card = true; notice_dismiss(); }
        }
        const bool paid1 = (tank.sd_paid_fish[g] & SD_PAID_ADULT) != 0;
        if (tank.fish[g].stage != STAGE_ADULT) LF_FAIL("SLP-3: stage %d (want adult)", tank.fish[g].stage);
        if (!c && (paid0 || !paid1 || !card)) LF_FAIL("SLP-3: grown up as a visitor and kept: owed at adopt %d, paid by the tick %d, card %d", !paid0, paid1, card);
        if (c && (!paid0 || card)) LF_FAIL("SLP-3: grown up at home and paid there: owed here again %d, card %d", !paid0, card);
    }
    {   /* (the review of 11-22) a modified sender's FRY, 4e9 s old and no stage marks: every step its age gives is counted paid */
        v6_setup();
        passport_t pp; passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, 4.0e9f);
        pp.uid = 0x66610000u; pp.stage = STAGE_FRY; pp.ms_bits = 0;
        int g = progression_gift_adopt(&tank, &pp);
        if (g < 0) LF_FAIL("SLP-3: adopt refused");
        const uint32_t all = SD_PAID_JUV | SD_PAID_ADULT | SD_PAID_ELDER;
        if ((tank.sd_paid_fish[g] & all) != all) LF_FAIL("SLP-3: an old FRY's steps owed at adopt (paid %x) - a gift would mint them", (unsigned)tank.sd_paid_fish[g]);
        /* ... and one an hour past its step plus a minute is old news too */
        passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, (float)(STAGE_ADULT_AGE + 3700)); pp.uid = 0x66610001u; pp.stage = STAGE_JUV; pp.ms_bits = MS_REACHED_JUV;
        g = progression_gift_adopt(&tank, &pp);
        if (g < 0 || !(tank.sd_paid_fish[g] & SD_PAID_ADULT)) LF_FAIL("SLP-3: a step crossed long before was owed");
    }
    /* (0.4.3 audit r2, R2-L1) a crafted passport with a stage and NO reached marks: every step at or below its stage is paid
       at the adopt (never on the next tick, with no card); the honest elder (every mark) is the control */
    for (int c = 0; c < 4; c++) {
        v6_setup(); notice_sync(&tank);
        static const double AGE[4] = { STAGE_ELDER_AGE + 50000.0, STAGE_ELDER_AGE + 30.0, STAGE_ADULT_AGE + 50000.0, STAGE_ELDER_AGE + 50000.0 };
        passport_t pp; passport_from_fish(&pp, &tank.fish[1], 0x5eed1234u, (float)AGE[c]);
        pp.uid = 0x66620000u + c; pp.stage = c != 2 ? STAGE_ELDER : STAGE_ADULT;
        pp.ms_bits = c == 3 ? (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER) : 0;
        int g = progression_gift_adopt(&tank, &pp);
        if (g < 0) LF_FAIL("R2-L1: adopt refused");
        const uint32_t want = c != 2 ? (SD_PAID_JUV | SD_PAID_ADULT | SD_PAID_ELDER) : (SD_PAID_JUV | SD_PAID_ADULT);
        const uint32_t paid0 = tank.sd_paid_fish[g];
        int cards = 0;
        for (int k = 0; k < 60 * 12; k++) {
            progression_tick(&tank, 1.0f / 60.0f); notice_tick(&tank, 1.0f / 60.0f, false);
            const notice_t *n = notice_current();
            if (n) { if (n->kind == NOTICE_STAGE && n->fish == g) cards++; notice_dismiss(); }
        }
        if ((paid0 & want) != want) LF_FAIL("R2-L1 case %d: a step at or below the passport's stage was owed at adopt (paid %x, want %x) - the next tick mints it", c, (unsigned)paid0, (unsigned)want);
        if (cards) LF_FAIL("R2-L1 case %d: a stage card for a stage it came with", c);
    }
    printf("selftest-linkfix: SLP-3 a stage reached as a visitor, then kept, is paid here (+%d) with its card; one its home paid is not paid again\n", SD_STAGE_ADULT);
    printf("selftest-linkfix: R2-L1 a passport's stage with no reached marks pays nothing below it (elder, young elder, adult; an honest elder the same)\n");
    /* ---- TP-7: KEEP only at home ---- */
    v6_setup();
    {   fish_t *f = &tank.fish[0];
        progression_set_age(&tank, 0, EVO_AGE + 5); f->evo = 0; f->evo_offer = 0; tank_evo_roll(&tank, f);
        f->away = true;
        if (progression_evo_keep(&tank, 0, 1) || f->evo || !tank_evo_ready(f)) LF_FAIL("TP-7: a fish next door kept a part (evo %d)", f->evo);
        f->away = false;
        if (!progression_evo_keep(&tank, 0, 1) || !f->evo) LF_FAIL("TP-7: at home KEEP was refused");
        printf("selftest-linkfix: TP-7 KEEP is refused for a fish next door (it stays READY with its three); at home it keeps\n");
        /* (0.4.3 audit r2, R2-L2) the doorway is next door already: its offer is out with the passport as it was */
        f->evo = 0; f->evo_offer = 0; tank_evo_roll(&tank, f);
        f->door = DOOR_ASKED; f->door_sent = true;
        if (progression_evo_keep(&tank, 0, 1) || f->evo || !tank_evo_ready(f)) LF_FAIL("R2-L2: KEEP in the doorway (asked) kept a part (evo %d)", f->evo);
        f->door = DOOR_PASSING;
        if (progression_evo_keep(&tank, 0, 1) || f->evo) LF_FAIL("R2-L2: KEEP in the doorway (passing) kept a part (evo %d)", f->evo);
        f->door = DOOR_NONE; f->door_sent = true;               /* (a sibling: turned back at home, door_sent not yet cleared - it keeps) */
        if (!progression_evo_keep(&tank, 0, 1) || !f->evo) LF_FAIL("R2-L2: KEEP at home with a door_sent left over was refused");
        f->evo = 0; f->evo_offer = 0; tank_evo_roll(&tank, f);
        f->door = DOOR_HEADING; f->door_sent = false;           /* (the sibling: on its way to a door, nothing sent - it keeps) */
        if (!progression_evo_keep(&tank, 0, 1) || !f->evo) LF_FAIL("R2-L2: KEEP on the way to a door (nothing sent) was refused");
        f->door = DOOR_NONE;
        printf("selftest-linkfix: R2-L2 KEEP is refused in the doorway (asked, passing); on the way to a door it keeps\n");
    }
    /* ---- R2-L3: a juvenile with a paragon parent's part in a family place needs a 0.4.3 tank; one without does not ---- */
    v6_setup();
    {   fish_t *f = &tank.fish[1];
        f->stage = STAGE_JUV; f->evo = 0; f->evo_offer = (uint16_t)EVO_HORN; tank_evo_fit(f);
        if (!evo_offer_at(f->evo_offer, 0)) LF_FAIL("R2-L3: the family place did not stay (offer %04x)", f->evo_offer);
        if (!progression_fish_needs_new(&tank, 1)) LF_FAIL("R2-L3: a juvenile with a family place does not need a 0.4.3 tank");
        f->evo_offer = 0;
        if (progression_fish_needs_new(&tank, 1)) LF_FAIL("R2-L3: a plain juvenile needs a 0.4.3 tank");
        f->evo_offer = EVO_SHOWN;                               /* (a SHOWN bit alone is no part) */
        if (progression_fish_needs_new(&tank, 1)) LF_FAIL("R2-L3: a SHOWN mark alone needs a 0.4.3 tank");
        f->evo_offer = 0;
        printf("selftest-linkfix: R2-L3 a juvenile with a paragon parent's part in a family place needs a 0.4.3 tank; a plain one does not\n");
    }
    /* ---- R2-L9 (Strato 11-23): one of ours back by a swap's ABORT or a gift's restore from bubbles keeps ITS STORY - its FROM
       (a fish that first came from tank X still came from X) and its lived time, exactly as when it left; through a reboot too ---- */
    for (int c = getenv("LF_C0") ? atoi(getenv("LF_C0")) : 0; c < 4; c++) {
        const bool swap = c >= 2, boot = c & 1;
        const uint32_t X = 0x5eed1234u, PEER = 0x5eed9999u, SID = 0x51u + (uint32_t)c, NONCE = 0x77u + (uint32_t)c;
        v6_setup();
        passport_t gp; passport_from_fish(&gp, &tank.fish[1], X, (float)(STAGE_ADULT_AGE + 60));
        gp.uid = 0x99900000u + (uint32_t)c; gp.stage = STAGE_ADULT; gp.ms_bits = MS_REACHED_JUV | MS_REACHED_ADULT;
        int g = progression_gift_adopt(&tank, &gp);
        if (g < 0) LF_FAIL("R2-L9: staging the gift from X");
        progression_tick(&tank, 1.0f / 60.0f);
        progression_slept(&tank, 200000.0f);                       /* (lived runs ahead of its grown age asleep) */
        const uint32_t from0 = progression_fish_from(&tank, &tank.fish[g]);
        const double lived0 = progression_lived_s(&tank, g);
        if (from0 != X || lived0 < progression_age_s(&tank, g) + 100000.0) LF_FAIL("R2-L9: staging (from %08x, lived %.0f age %.0f)", from0, lived0, progression_age_s(&tank, g));
        passport_t pp; passport_from_fish(&pp, &tank.fish[g], tank.uid, progression_age_s(&tank, g));   /* (ours as it leaves: pp_of's home) */
        const uint32_t uid = pp.uid;
        static gift_book_t GB; static swap_book_t SB;
        memset(&GB, 0, sizeof GB); memset(&SB, 0, sizeof SB); SB.peer_edge = -1;
        if (!swap) {
            gift_attach(&tank, &GB);
            if (!gift_given_add(&GB, uid, NONCE, PEER, &pp)) LF_FAIL("R2-L9: staging the GIVEN note");
            if (!progression_give_out(&tank, g)) LF_FAIL("R2-L9: give_out");
        } else {
            swap_attach(&tank, &SB);
            swap_note_t *n = &SB.notes[0];
            n->st = SWN_COMMITTED; n->role = SWAP_ROLE_I; n->sid = SID; n->peer = PEER; n->tank = tank.uid; n->out = pp; n->in = gp; n->in.uid = 0x99910000u;
            if (!swap_save(&SB)) LF_FAIL("R2-L9: the note on the flash");   /* (as i_commit: the note first) */
            if (!progression_swap_out(&tank, g)) LF_FAIL("R2-L9: swap_out");
        }
        if (lf_home(uid) >= 0) LF_FAIL("R2-L9: the fish did not leave");
        if (boot) {                                                 /* (the board restarts before it comes back) */
            const uint32_t me = tank.uid;
            memset(&tank, 0, sizeof tank); tank_init(&tank, 4343); shelf_boot(clock_port_now_unix()); progression_boot(&tank);
            if (tank.uid != me) LF_FAIL("R2-L9: the reboot is another tank");
            if (!swap) gift_attach(&tank, &GB); else swap_attach(&tank, &SB);
        }
        link_ev_t ev; memset(&ev, 0, sizeof ev);
        if (!swap) { ev.type = LEV_GIVE_RESTORE; ev.uid = uid; ev.peer = PEER; ev.nonce = NONCE; ev.pp = pp; }
        else { ev.type = LEV_SWAP; ev.peer = PEER; ev.reason = SWK_ABORT; ev.nonce = SID; ev.uid = uid; }
        tank_link_event(&tank, NULL, &ev);
        const int k = lf_home(uid);
        const uint32_t from1 = k >= 0 ? progression_fish_from(&tank, &tank.fish[k]) : 0;
        const double lived1 = k >= 0 ? progression_lived_s(&tank, k) : 0;
        gift_attach(&tank, NULL); swap_attach(&tank, NULL);
        if (k < 0) LF_FAIL("R2-L9 %s%s: it did not come back", swap ? "swap ABORT" : "gift restore", boot ? " after a reboot" : "");
        if (from1 != X || fabs(lived1 - lived0) > 1.0)
            LF_FAIL("R2-L9 %s%s: its story changed: FROM %08x (want %08x), lived %.0f s (want %.0f)", swap ? "swap ABORT" : "gift restore", boot ? " after a reboot" : "",
                    from1, X, lived1, lived0);
    }
    {   /* (a sibling) a fish of ours GIVEN for good, then given back by the other tank (its passport's home is theirs): from THEM, as ever */
        v6_setup();
        const uint32_t PEER = 0x5eed9999u;
        passport_t pp; passport_from_fish(&pp, &tank.fish[1], tank.uid, progression_age_s(&tank, 1));
        const uint32_t uid = pp.uid;
        if (!progression_give_out(&tank, 1)) LF_FAIL("R2-L9 sibling: give_out");
        pp.home_tank = PEER;
        int k = progression_gift_adopt(&tank, &pp);
        if (k < 0 || progression_fish_from(&tank, &tank.fish[k]) != PEER) LF_FAIL("R2-L9 sibling: given back by the other tank reads FROM %08x (want %08x)",
                                                                          k >= 0 ? progression_fish_from(&tank, &tank.fish[k]) : 0, PEER);
        (void)uid;
    }
    printf("selftest-linkfix: R2-L9 a fish back by a swap's ABORT or from bubbles keeps its FROM and lived time (and through a reboot); given back by the other tank it is theirs\n");
    /* ---- R3-L13: a note for a fish that can still come back (a swap's I note, a GIVEN note) outlives the notes of fish gone for
       good. Five fish, a leaves (pending), two others are GIVEN for good, two arrive: the save has spare rows for a's note only if the
       pending note is written first. c 0/1: swap ABORT, gift restore, each after a reboot; c 2: six departures for good with no
       reboot (RAM keeps six notes). Siblings: a note for a fish gone for good still takes a row that is free ---- */
    for (int c = getenv("LF_C1") ? atoi(getenv("LF_C1")) : 0; c < 3; c++) {
        const bool swap = c != 1;
        const uint32_t X = 0x5eed1234u, PEER = 0x5eed9999u, S = 0x5eed7777u, SID = 0x61u + (uint32_t)c, NONCE = 0x87u + (uint32_t)c;
        static gift_book_t GB; static swap_book_t SB;
        v6_setup();
        memset(&GB, 0, sizeof GB); memset(&SB, 0, sizeof SB); SB.peer_edge = -1;
        gift_attach(&tank, &GB); swap_attach(&tank, &SB);
        uint32_t su = 0xC1300000u + (uint32_t)c * 0x100u;
#define LF_STRANGER() do { passport_t sp_; passport_from_fish(&sp_, &tank.fish[0], S, 600.0f); sp_.uid = su++; sp_.stage = STAGE_FRY; sp_.ms_bits = 0; \
                           if (progression_gift_adopt(&tank, &sp_) < 0) LF_FAIL("R3-L13 %d: staging: no room for an arrival (%d fish)", c, tank.n_fish); } while (0)
        while (tank.n_fish > 4) progression_sell_fish(&tank, tank.n_fish - 1);
        while (tank.n_fish < 4) LF_STRANGER();
        passport_t gp; passport_from_fish(&gp, &tank.fish[1], X, (float)(STAGE_ADULT_AGE + 60));
        gp.uid = 0x99a00000u + (uint32_t)c; gp.stage = STAGE_ADULT; gp.ms_bits = MS_REACHED_JUV | MS_REACHED_ADULT;
        int g = progression_gift_adopt(&tank, &gp);
        if (g < 0 || tank.n_fish != 5) LF_FAIL("R3-L13 %d: staging the fish from X (%d fish)", c, tank.n_fish);
        progression_tick(&tank, 1.0f / 60.0f);
        progression_slept(&tank, 200000.0f);
        const double lived0 = progression_lived_s(&tank, g);
        passport_t pp; passport_from_fish(&pp, &tank.fish[g], tank.uid, progression_age_s(&tank, g));
        const uint32_t uid = pp.uid;
        if (swap) {
            swap_note_t *n = &SB.notes[0];
            n->st = SWN_COMMITTED; n->role = SWAP_ROLE_I; n->sid = SID; n->peer = PEER; n->tank = tank.uid; n->out = pp; n->in = gp; n->in.uid = 0x99a10000u + (uint32_t)c;
            if (!swap_save(&SB)) LF_FAIL("R3-L13 %d: the note on the flash", c);
            if (!progression_swap_out(&tank, g)) LF_FAIL("R3-L13 %d: swap_out", c);
        } else {
            if (!gift_given_add(&GB, uid, NONCE, PEER, &pp)) LF_FAIL("R3-L13 %d: staging the GIVEN note", c);
            if (!progression_give_out(&tank, g)) LF_FAIL("R3-L13 %d: give_out", c);
        }
        const int rounds = c == 2 ? 6 : 2;                          /* GIVEs for good (no GIVEN note left: the other tank has them) */
        for (int r = 0; r < rounds; r++) {
            int v = -1; for (int i = 0; i < tank.n_fish && v < 0; i++) if (tank.fish[i].uid != uid) v = i;
            if (v < 0 || !progression_give_out(&tank, v)) LF_FAIL("R3-L13 %d: a GIVE for good", c);
            if (c == 2) LF_STRANGER();
        }
        if (c != 2) { LF_STRANGER(); LF_STRANGER(); }
        if (tank.n_fish != 4 || progression_fish_held(&tank) != 5) LF_FAIL("R3-L13 %d: staging: %d fish, held %d (want 4, 5)", c, tank.n_fish, progression_fish_held(&tank));
        progression_save(&tank);
        if (c != 2) {                                               /* (the board restarts before a comes back) */
            const uint32_t me = tank.uid;
            gift_attach(&tank, NULL); swap_attach(&tank, NULL);
            memset(&tank, 0, sizeof tank); tank_init(&tank, 4343); shelf_boot(clock_port_now_unix());
            gift_attach(&tank, &GB); swap_attach(&tank, &SB);
            progression_boot(&tank);
            if (tank.uid != me) LF_FAIL("R3-L13 %d: the reboot is another tank", c);
        }
        link_ev_t ev; memset(&ev, 0, sizeof ev);
        if (swap) { ev.type = LEV_SWAP; ev.peer = PEER; ev.reason = SWK_ABORT; ev.nonce = SID; ev.uid = uid; }
        else { ev.type = LEV_GIVE_RESTORE; ev.uid = uid; ev.peer = PEER; ev.nonce = NONCE; ev.pp = pp; }
        tank_link_event(&tank, NULL, &ev);
        const int k = lf_home(uid);
        const uint32_t from1 = k >= 0 ? progression_fish_from(&tank, &tank.fish[k]) : 0;
        const double lived1 = k >= 0 ? progression_lived_s(&tank, k) : 0;
        if (k < 0) LF_FAIL("R3-L13 %d: a did not come back", c);
        if (from1 != X || fabs(lived1 - lived0) > 1.0)
            LF_FAIL("R3-L13 %s: a lost its story behind the notes of fish gone for good: FROM %08x (want %08x), lived %.0f s (want %.0f)",
                    c == 0 ? "swap ABORT after a reboot" : c == 1 ? "gift restore after a reboot" : "six GIVEs for good, no reboot", from1, X, lived1, lived0);
        gift_attach(&tank, NULL); swap_attach(&tank, NULL);
#undef LF_STRANGER
    }
    {   /* (sibling) with rows to spare, a note for a fish GIVEN for good still lands on the flash and still reads after a reboot: given
           back with our own passport (the other tank never took it in), it keeps its story as before */
        v6_setup();
        while (tank.n_fish > 3) progression_sell_fish(&tank, tank.n_fish - 1);
        const uint32_t X = 0x5eed1234u;
        passport_t gp; passport_from_fish(&gp, &tank.fish[1], X, (float)(STAGE_ADULT_AGE + 60));
        gp.uid = 0x99b00000u; gp.stage = STAGE_ADULT; gp.ms_bits = MS_REACHED_JUV | MS_REACHED_ADULT;
        int g = progression_gift_adopt(&tank, &gp);
        if (g < 0) LF_FAIL("R3-L13 sibling: staging");
        progression_tick(&tank, 1.0f / 60.0f); progression_slept(&tank, 200000.0f);
        const double lived0 = progression_lived_s(&tank, g);
        passport_t pp; passport_from_fish(&pp, &tank.fish[g], tank.uid, progression_age_s(&tank, g));
        if (!progression_give_out(&tank, g)) LF_FAIL("R3-L13 sibling: give_out");
        const uint32_t me = tank.uid;
        memset(&tank, 0, sizeof tank); tank_init(&tank, 4343); shelf_boot(clock_port_now_unix()); progression_boot(&tank);
        if (tank.uid != me) LF_FAIL("R3-L13 sibling: the reboot is another tank");
        progression_gift_adopt(&tank, &pp); const int k = lf_home(pp.uid);
        if (k < 0 || progression_fish_from(&tank, &tank.fish[k]) != X || fabs(progression_lived_s(&tank, k) - lived0) > 1.0)
            LF_FAIL("R3-L13 sibling: a note with a free row was not kept (FROM %08x)", k >= 0 ? progression_fish_from(&tank, &tank.fish[k]) : 0);
    }
    printf("selftest-linkfix: R3-L13 a note for a fish that can still come back outlives the notes of fish gone for good (swap, gift, reboot, six departures); a free row still keeps the rest\n");
    printf("selftest-linkfix: PASS\n");
    return 0;
}
/* --linkfix-shot <prefix>: the GIVE card for a paragon when the only tank next door is older (SLB-3's words), and the same
   card for a plain fish (CONFIRM, as ever) - <prefix>_give_old.ppm, <prefix>_give_plain.ppm */
static int linkfix_shot(const char *prefix) {
    static uint16_t fb[TANK_W * TANK_H]; char path[600];
    v6_setup();
    for (int i = 0; i < 4 * 60; i++) tank_tick(&tank, 1.0f / 60.0f, advisor_rules);
    static gift_book_t G; memset(&G, 0, sizeof G); gift_attach(&tank, &G);
    tank.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;   /* (no swap book: an older tank) */
    for (int c = 0; c < 2; c++) {
        int f0 = -1; for (int i = 0; i < tank.n_fish && f0 < 0; i++) if (progression_fish_givable(&tank, i) && tank.fish[i].stage == STAGE_ELDER) f0 = i;
        if (f0 < 0) { printf("linkfix-shot: no elder to give\n"); return 1; }
        tank.fish[f0].evo = c ? 0 : EVO_BANNER;
        render_give_modal_close(); render_give_tap(&tank, f0);
        for (int n = 0; n < 40; n++) { tank_tick(&tank, 0.04f, advisor_rules); render_frame_begin(); render_tank(&tank, fb, TANK_W); render_give_modal(&tank, fb, TANK_W); }
        printf("linkfix-shot: %s: step %d, \"%s\"\n", c ? "plain" : "paragon", render_give_modal_step(&tank), render_give_modal_line(&tank));
        snprintf(path, sizeof path, "%s_give_%s.ppm", prefix, c ? "plain" : "old"); write_ppm(path, fb);
    }
    render_give_modal_close();
    return 0;
}
