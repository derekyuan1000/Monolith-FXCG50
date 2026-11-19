/* Monolith: every screen flip goes through zflip() so it can be counted (shown in MONO_PROF builds).
 * GPL-3.0-or-later */
#ifndef ZFLIP_H
#define ZFLIP_H
#ifdef __cplusplus
extern "C" {
#endif

void zflip(void);                /* Bdisp_PutDisp_DD + count; the only caller of Bdisp_PutDisp_DD */
void zflip_key_begin(void);      /* in_ckgetkey: a key was just read */
void zflip_key_end(void);        /* in_ckgetkey: about to wait for the next key */
unsigned zflip_last_count(void); /* flips between the last key and the following wait */
int zflip_last_ticks(void);      /* RTC ticks (1/128 s) for the same span */

#ifdef __cplusplus
}
#endif
#endif
