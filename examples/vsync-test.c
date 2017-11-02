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

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <memory.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

#include "../spr16.h"
#include "util.h"
#define STRERR strerror(errno)

struct spr16 *g_screen;
float g_offset;
int handle_input(struct spr16_msgdata_input *input)
{
	switch (input->code)
	{
		case 'q':
		case 'x':
			_exit(0);
		default:
			break;
	}
	return 0;
}

float change_speed()
{
	switch (rand()%7) {
		case 0:
			return 0.6f;
		case 1:
			return 0.7f;
		case 2:
			return 0.9f;
		case 3:
			return 1.4f;
		case 4:
			return 1.6f;
		case 5:
			return 1.8f;
		default:
			break;
	}
	return 1.0f;
}
int update()
{
	static int right = 1;
	static float speed = 3.0f;

	if (right) {
		g_offset += 0.732f * speed;
		if (g_offset > 400.0f) {
			g_offset = 400.0f;
			right = 0;
			speed = change_speed();
		}
	}
	else {
		g_offset -= 0.732f * speed;
		if (g_offset < 0.0f) {
			g_offset = 0.0f;
			right = 1;
			speed = change_speed();
		}
	}
	return 0;
}

int draw()
{
	const unsigned int bars = 9;
	unsigned int i;

	if (spr16_client_waiting_for_vsync()) {
		return 0;
	}
	/* black bars */
	draw_fillrect(g_screen, 0, 0, g_screen->width, g_screen->height, 0xff000000);

	/* white bars */
	for (i = 0; i <  bars-1; ++i)
	{
		unsigned int b_wid = g_screen->width / (bars-1);
		unsigned int b_off = (i * b_wid) + g_offset;
		if (i % 2 == 0) {
			if (b_off >= g_screen->width)
				continue;
			if (b_wid + b_off > g_screen->width)
				b_wid = b_wid - (b_wid + b_off - g_screen->width);
			draw_fillrect(g_screen, b_off, 0, b_wid,
					g_screen->height, 0xffffffff);
		}

	}
	if (spr16_client_sync(0, 0, g_screen->width,
				g_screen->height, SPRITESYNC_FLAG_VBLANK)) {
		if (errno != EAGAIN) {
			return -1;
		}
	}
	return 0;
}


int handle_servinfo(struct spr16_msgdata_servinfo *sinfo)
{
	printf("client: read msg\n");
	printf("max width: %d\n", sinfo->width);
	printf("max width: %d\n", sinfo->height);
	printf("max bpp: %d\n",   sinfo->bpp);
	/* XXX direct shm is implemented using prime mmap, BUT (oh joy!) it isn't
	 * actually faster than buffering through server (on my nouveau/ttm driver),
	 * and vblank through our server needs a bit of reworking, maybe page
	 * flipping makes more sense in this case?  so disabled until it works right...
	 */
	/*if (spr16_client_handshake_start("vsync_test", sinfo->width,
				sinfo->height, SPRITE_FLAG_DIRECT_SHM))*/
	if (spr16_client_handshake_start("vsync_test", sinfo->width, sinfo->height, 0))
		return -1;
	return 0;
}

int main(int argc, char *argv[])
{
	struct spr16 *screen;
	int r;

	srand(getpid());

	g_offset = 0.0f;
	if (argc < 2) {
		printf("\n");
		printf("-----------------------------\n");
		printf("usage:\n");
		printf("-----------------------------\n");
		printf("vsync-test <server-name>\n");
		printf("e.g: vsync-test tty1\n\n");
		printf("\n");
		printf("-----------------------------\n");
		printf("keys:\n");
		printf("-----------------------------\n");
		printf("q: quit\n");
		printf("\n");
		return -1;
	}

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (spr16_client_init())
		return -1;
	spr16_client_set_servinfo_handler(handle_servinfo);
	spr16_client_set_input_handler(handle_input);

	r = spr16_client_connect(argv[1]);
	if (r == -1) {
		printf("connect(%s) error:\n", argv[1]);
		return -1;
	}

	if (spr16_client_handshake_wait(10000)) {
		printf("handshake timed out\n");
		return -1;
	}
	screen = spr16_client_get_sprite();
	if (!screen)
		return -1;
	screen = spr16_client_get_sprite();
	g_screen = screen;

	while (1)
	{
		r = spr16_client_update(0);
		if (r == -1) {
			printf("spr16 update error\n");
			return -1;
		}
		if (update())
			return -1;

		draw();
		usleep(2000);
	}
	spr16_client_shutdown();

	return 0;
}
