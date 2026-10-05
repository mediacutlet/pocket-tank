/* sd_backup.h - the 2.16's microSD slot keeps copies of the tank (2026-10-04).
 * The save lives in NVS; this copies that same blob to the card and, on a
 * board whose NVS holds no tank (a --full flash, an erased chip), brings the
 * newest copy back before the tank boots. The card is mounted only while a
 * copy is made (the internal heap is tight), and a missing card, or another
 * board, is simply nothing to do. */
#ifndef SD_BACKUP_H
#define SD_BACKUP_H
#include <stdbool.h>
#include <stdint.h>
bool sd_backup_restore_if_empty(void);   /* before progression_boot: true = a copy from the card is now the save */
bool sd_backup_now(const char *why);     /* the save as it is in NVS -> the card (latest + today's) */
void sd_backup_poll(int64_t now_us);     /* the tank task, every frame: a copy a minute after boot, then every 3 h */
void sd_backup_status(void);             /* director `sd`: the card, the copies on it */
bool sd_backup_restore(const char *name);/* director `sd restore [file]`: that copy (default the latest) -> the save; the caller restarts */
/* the SD BACKUPS page (2026-10-04), from the updates page: the copies on the
 * card, newest first - tap one, confirm, and the tank restarts into it (the
 * tank as it was is kept first, as BEFORE RESTORING). Drawn and hit-tested in
 * PAGE coordinates (render.h), touched with tank coordinates like the others. */
enum { SDP_NONE = 0, SDP_CLOSE = 1, SDP_RESTORE = 2 };
void sd_backup_page_open(void);                                /* lists the card (mounts it for a moment) */
void sd_backup_page_render(uint16_t *fb, int stride);
int  sd_backup_page_touch(float x, float y, bool down);         /* every frame while up: SDP_* on a release */
void sd_backup_page_restore(void);                             /* after SDP_RESTORE (the tank saved first): keep the tank as UNDO.BIN, the chosen copy -> NVS, restart */
#endif
