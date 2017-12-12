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
 */

#ifndef LNX_PLATFORM_H__
#define LNX_PLATFORM_H__

#include "../../spr16.h"
#include "../fdpoll-handler.h"
#include "drm.h"

/* saves a client lookup, on systems without epoll make this a large static sized array
 * and use fd as 1:1 index lookup, otherwise you need to use a hashmap, or worse... */
struct cl_cb_data {
	struct cl_cb_data *next;
	struct server_context *self;
	struct client *cl;
};

struct server_context {
	struct screen *main_screen;
	struct cl_cb_data *cb_data;
	struct client *pending_clients;
	struct client *sync_clients[SPR16_MAXCLIENTS];
	struct spr16_framebuffer *fb;
	struct fdpoll_handler *fdpoll;
	struct input_device *input_devices;
	struct client *free_list[MAX_FDPOLL_HANDLER];
	unsigned int free_count;
	int listen_fd;

	/* TODO /dev/fb fallback */
	struct drm_kms *card0;
};

struct server_context *spr16_server_init(char *sockname,
					 struct fdpoll_handler *fdpoll,
					 struct spr16_framebuffer *fb);
int spr16_server_update(struct server_context *self);
int spr16_server_shutdown(struct server_context *self);
int spr16_server_reset_client(struct client *cl);
void server_sync_fullscreen(struct server_context *self);



int  input_flush_all_devices(struct input_device *list);
void load_linux_input_drivers(struct server_context *ctx,
			      int stdin_mode,
			      int evdev,
			      input_hotkey hk);

#endif
