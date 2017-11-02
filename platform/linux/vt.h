/* Copyright (C) 2017 Michael R. Tirado <mtirado418@gmail.com> -- GPLv3+
 *
 * This program is libre software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. You should have
 * received a copy of the GNU General Public License version 3
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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
