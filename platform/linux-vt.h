/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */

#ifndef LINUX_VT_H__
#define LINUX_VT_H__

#include <linux/kd.h>
#include <linux/vt.h>

/* some very coarse grained terminal mouse input can be received
 * using the right control codes, as \033[mbxy  (button, x, y)
 * keyboard mode can be K_UNICODE, (evdev, etc)K_OFF,
 * K_XLATE, K_MEDIUMRAW, or K_RAW
 * */
int vt_init(int tty_fd, unsigned int kbd_mode);
void vt_shutdown();

#endif
