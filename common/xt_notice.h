/* xt_notice.h - THE EXPERIMENTAL TANK'S THANK-YOU (0.4.3, docs/release-0.4.3.md item 9a).
 *
 * Strato 11-22: "the first time a keeper activates an experimental tank like the jellies, a notice should show in a
 * modal when they first boot the experimental tank that thanks them for their support and reminds to double tap BOOT
 * to get back to the MY TANKS screen, keep it brief please."
 *
 * Once per experimental tank (a jelly tank, PIRANHAS, MINNOWS - shelf.h SHELF_TYPE_EXPERIMENTAL), never on a fish tank.
 * The boot that puts one on the glass calls xt_notice_open with its shelf id; a tank not yet in THE SEEN LIST waits
 * XT_NOTICE_WAIT_S of frames with nothing over it (its room drawn, no page up), then comes up, and is written into the
 * list the moment it does - so a restart, a sleep, a swap away and back, a power cut while it is up, BOOT twice while it
 * is up: none shows it again. It stays until a tap (as AI MODEL DAMAGED: a kid who looked away still reads it), and
 * THE TOUCH RULE holds (touch_gate.h, G1): a press begun before it came up is spent, the notice stays. A page over it
 * (BOOT twice opens MY TANKS) closes it: it was seen.
 *
 * THE SEEN LIST: its own record in the shelf's frame store (shelf_port_frame_*: NVS `frame` on the boards, a file in
 * the sim), key "xtseen" - never a byte of the tank save. Shelf ids only grow (shelf.c s_next), except that ERASE
 * EVERYTHING starts them again at 2: shelf_erase_all drops "xtseen" with the shelf's other records. XT_SEEN_MAX ids,
 * the oldest let go when it is full (that tank would only say thank you once more). A board that opened its jelly
 * tank before 0.4.3 has no list: it says thank you once after the update.
 *
 * Cost: a few bytes of state; nothing per frame while it is not up (xt_notice_tick returns at once); the record is
 * read once at the boot (on the stack) and written once. */
#ifndef XT_NOTICE_H
#define XT_NOTICE_H
#include <stdbool.h>
#include <stdint.h>

#define XT_NOTICE_WAIT_S 1.5f            /* frames with nothing over the tank before it comes up */
#define XT_SEEN_MAX      32
#define XT_SEEN_KEY      "xtseen"
#define XT_SEEN_MAGIC    0x58545331u     /* 'XTS1' */

void  xt_notice_open(uint32_t shelf_id, bool experimental);   /* the boot: the tank on the glass (shelf_now()) */
/* (0.4.3) THE JELLY TANKS SAY THANK YOU ONCE IN ALL: a board keeps up to three (one a theme), and the thank-you is the
   keeper's, not a tank's - once one jelly tank has said it, another jelly tank (a second theme, or one installed again
   after a REMOVE) never says it again. Its mark in THE SEEN LIST is XT_SEEN_JELLY (no shelf id reaches the top bit); a
   jelly tank on the board already in the list counts too (a board that saw the notice before the mark). PIRANHAS and
   MINNOWS still say it once each. The boot calls this in place of xt_notice_open for a jelly tank on the glass */
#define XT_SEEN_JELLY    0xFFFFFF01u
void  xt_notice_open_jelly(uint32_t shelf_id);
void  xt_notice_tick(float dt, bool blocked);                 /* each frame; blocked = a page or MY TANKS is over the tank */
bool  xt_notice_up(void);
bool  xt_notice_tap(float held_s);       /* a tap lifted, its press held_s long: true = the notice took it (closed, or spent: still up) */
bool  xt_notice_seen(uint32_t shelf_id); /* in THE SEEN LIST (the selftest, the log) */

#endif
