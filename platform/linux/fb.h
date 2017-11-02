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

int fb_drm_fd_callback(int fd, int event_flags, void *user_data);
int fb_sync_client(struct server_context *self, struct client *cl);

#endif
