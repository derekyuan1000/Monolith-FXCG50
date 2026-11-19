#include <fxcg/display.h>
#include <fxcg/rtc.h>
#include "zflip.h"
#include "zskin_status.h"

static unsigned flips, last_flips;
static int t0, last_ticks;

void zflip(void) { ++flips; Bdisp_PutDisp_DD(); }
void zflip_key_begin(void) { flips = 0; t0 = RTC_GetTicks(); zskin_status_msg(0); }
void zflip_key_end(void) { last_flips = flips; last_ticks = RTC_GetTicks() - t0; }
unsigned zflip_last_count(void) { return last_flips; }
int zflip_last_ticks(void) { return last_ticks; }
