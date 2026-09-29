/* OS setup byte numbers found with the SETUP-PROBE add-in (experiment E1, docs/RUNMAT_SPEC.md).
 * GetSetupSetting(n) / SetSetupSetting(n, v). Values were measured on the fx-CG Manager PLUS
 * emulator (2026-09-24); re-check on a real device (step 11 of CLAUDE.md). Do not add a byte
 * here without a new E1 run. */
#ifndef ZSKIN_OS_H
#define ZSKIN_OS_H

#define ZOS_BYTE_ANGLE 0x13
#define ZOS_ANGLE_DEG  0x00
#define ZOS_ANGLE_RAD  0x01 /* Gra (grad) not measured; the sync ignores any other value */

#endif
