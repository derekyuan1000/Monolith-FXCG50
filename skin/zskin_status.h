#ifndef ZSKIN_STATUS_H
#define ZSKIN_STATUS_H
#ifdef __cplusplus
extern "C" {
#endif

/* OS status area: native icons (shell) vs KhiCAS text messages (everything else). */
void zskin_status_icons(void);
void zskin_status_message(char *msg, short p2, char color, char p4);
/* RUN-MAT style: Math  Deg/Rad  Norm1     d/c  Real */
void zskin_status_runmat(int is_radian);
/* Show msg in the status area until the next key (it survives in_ckgetkey's pre-wait redraw). 0 clears. */
void zskin_status_msg(const char *msg);

#ifdef __cplusplus
}
#endif

/* Route every existing DefineStatusMessage call through the wrapper. Object-like on
 * purpose: fxcg_syscalls.h (no include guard, pulled in by later includes) declares
 * DefineStatusMessage with the same signature, and this way that declaration simply
 * becomes a compatible redeclaration of zskin_status_message, so the include order
 * in the calling files does not matter. */
#ifndef ZSKIN_STATUS_C
#define DefineStatusMessage zskin_status_message
#endif
#endif
