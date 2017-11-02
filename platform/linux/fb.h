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
 * TODO /dev/fb fallback support
 *
 */

#ifndef LINUX_FB_H__
#define LINUX_FB_H__

#include "../../spr16.h"
#include "platform.h"

/* aligns sync rectangle to grid
 * 16/32 pixel blocks are always nicely aligned for 8, 16, 32 bpp
 * 32 pixel grid @ 32bpp = 128byte == full xmm line,
 * 64 for ymm, 128 for zmm...
 * but is no good @24bpp, which is currently left unsupported
 *
 * large grid alignment does more harm than good on older cpu's
 * that do not support the newer instructions, repl movsb instruction
 * should probably be used for anything that supports it? i don't have
 * any such space-age hardware to test these theories.
 */
#define PIXL_ALIGN 32 /* right now this is kind of hacked up, 32 will use
			 xmm sse2 instructions, for generic memcpy set this to 16 */

int fb_drm_fd_callback(int fd, int event_flags, void *user_data);
int fb_sync_client(struct server_context *self, struct client *cl);

#endif
