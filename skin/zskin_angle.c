#include "zskin_os.h"
#include "zskin_angle.h"

int zskin_angle_step(zskin_angle_state *st, int os_byte, int giac_is_radian)
{
  int os_known = (os_byte == ZOS_ANGLE_RAD || os_byte == ZOS_ANGLE_DEG);
  int giac = giac_is_radian ? 1 : 0;

  if (os_known && os_byte != st->last_os) {
    /* the OS changed (or first call): it wins */
    int os_rad = (os_byte == ZOS_ANGLE_RAD);
    st->last_os = os_byte;
    st->last_giac = os_rad;
    if (giac != os_rad)
      return os_rad ? ZSKIN_ANGLE_SET_GIAC_RAD : ZSKIN_ANGLE_SET_GIAC_DEG;
    return ZSKIN_ANGLE_NONE;
  }
  if (st->last_giac != -1 && giac != st->last_giac) {
    /* Giac changed, the OS did not: write it to the OS */
    st->last_os = giac ? ZOS_ANGLE_RAD : ZOS_ANGLE_DEG;
    st->last_giac = giac;
    return giac ? ZSKIN_ANGLE_WRITE_OS_RAD : ZSKIN_ANGLE_WRITE_OS_DEG;
  }
  st->last_giac = giac;
  if (os_known)
    st->last_os = os_byte;
  return ZSKIN_ANGLE_NONE;
}
