/* Decision logic for the two-way angle sync between the OS setup byte and Giac's angle setting.
 * Pure C, no OS or Giac includes, so it is unit-tested on the host (tests/harness/angle_test.c).
 * The caller (console.cc, zskin_sync_modes) reads both values, calls zskin_angle_step() and acts on
 * the answer.
 *
 * Rules: a change of the OS byte (an OS SETUP change, or the first call) wins and is applied to Giac;
 * a change on the Giac side with the OS byte unchanged (angle_radian(0) typed as a command, a script,
 * KhiCAS's own SETUP) is written to the OS byte. On the very first call with a known OS byte the OS
 * wins, so a saved session's angle is overridden at startup (intended). Any OS value other than
 * Rad/Deg (e.g. grad) is "unknown": never applied to Giac and never overwritten unless Giac changes. */
#ifndef ZSKIN_ANGLE_H
#define ZSKIN_ANGLE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int last_os; int last_giac; } zskin_angle_state; /* -1 = unknown */
#define ZSKIN_ANGLE_STATE_INIT { -1, -1 }

enum { ZSKIN_ANGLE_NONE = 0, ZSKIN_ANGLE_SET_GIAC_RAD, ZSKIN_ANGLE_SET_GIAC_DEG,
       ZSKIN_ANGLE_WRITE_OS_RAD, ZSKIN_ANGLE_WRITE_OS_DEG };

/* os_byte: raw OS angle byte (ZOS_ANGLE_DEG 0x00, ZOS_ANGLE_RAD 0x01, anything else = unknown);
 * giac_is_radian: 0/1. Returns what the caller must do and updates the state. */
int zskin_angle_step(zskin_angle_state *st, int os_byte, int giac_is_radian);

#ifdef __cplusplus
}
#endif

#endif
