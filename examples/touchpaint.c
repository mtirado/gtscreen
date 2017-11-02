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

#include "../spr16.h"
#include "util.h"
#define STRERR strerror(errno)

struct spr16 *g_screen;
struct contact {
	int id;
	struct vec2 pos;
	float magnitude;
	uint32_t argb;
} g_contacts[SPR16_SURFACE_MAX_CONTACTS];
int g_active_slot;
int g_shifted;
/* ARGB */
uint32_t g_ticks;
int g_lock_colors;
uint32_t g_palette[] = {
	0xffaa0000,
	0xff00aa00,
	0xff0000aa,
	0xffaa00aa,
	0xff00aaaa,
};

int update_contact(int id, struct vec2 pos, int magnitude, int magnitude_max)
{
	int i;
	const float threshold = 0.5;
	if (id < 0) {
		printf("invalid id in upate_contact\n");
		return -1;
	}
	for (i = 0; i < SPR16_SURFACE_MAX_CONTACTS; ++i)
	{
		if (g_contacts[i].id == id) {
			g_contacts[i].pos = pos;
			g_contacts[i].magnitude = (float)magnitude/magnitude_max;
			if (g_contacts[i].magnitude < threshold)
				g_contacts[i].magnitude = threshold;
			if (magnitude == 0) {
				g_contacts[i].id = -1;
			}
			else {
				g_contacts[i].id = id;
			}
			return 0;
		}
	}
	if (magnitude == 0)
	{
		/* FIXME yep this happens, track up/down in platform driver? */
		/*printf("untracked touch up, id=%d\n", id);*/
		return 0;
	}
	/* new contact? */
	for (i = 0; i < SPR16_SURFACE_MAX_CONTACTS; ++i)
	{
		if (g_contacts[i].id == -1) {
			g_contacts[i].pos = pos;
			g_contacts[i].magnitude = (float)magnitude/magnitude_max;
			if (g_contacts[i].magnitude < threshold)
				g_contacts[i].magnitude = threshold;
			g_contacts[i].id = id;
			g_active_slot = i;
			return 0;
		}
	}
	printf("error, contact list is full\n");
	return -1;
}

int touch_init(struct spr16 *screen)
{
	int i;
	g_screen = screen;
	g_active_slot = 0;
	g_shifted = 0;
	memset(&g_contacts, 0, sizeof(g_contacts));
	for (i = 0; i < SPR16_SURFACE_MAX_CONTACTS; ++i)
	{
		g_contacts[i].id = -1;
	}
	if (spr16_client_sync(0, 0, g_screen->width,
				g_screen->height, SPRITESYNC_FLAG_ASYNC)) {
		if (errno != EAGAIN) {
			return -1;
		}
	}
	return 0;
}

static void morph_color(int pindex, int rd, int gd, int bd)
{
	uint8_t r = (g_palette[pindex] & 0x00ff0000) >> 16;
	uint8_t g = (g_palette[pindex] & 0x0000ff00) >> 8;
	uint8_t b = (g_palette[pindex] & 0x000000ff);
	r += rd;
	g += gd;
	b += bd;
	g_palette[pindex] = 0xff000000
		| ((uint32_t)r << 16)
		| ((uint32_t)g << 8)
		| ((uint32_t)b);
}

int regular_input(struct spr16_msgdata_input *input)
{
	int r = 0;
	int g = 0;
	int b = 0;
	switch (input->code)
	{
		case SPR16_KEYCODE_LSHIFT:
		case SPR16_KEYCODE_RSHIFT:
			g_shifted = input->val;
			break;
		case 'r':
			r = (g_shifted ? -8 : 8);
			break;
		case 'g':
			g = (g_shifted ? -8 : 8);
			break;
		case 'b':
			b = (g_shifted ? -8 : 8);
			break;
		case 'q':
			_exit(0);
		default:
			return 0;
	}
	if (r || g || b) {
		unsigned int colors = sizeof(g_palette) / sizeof(g_palette[0]);
		unsigned int pindex = g_active_slot % colors;
		morph_color(pindex, r, g, b);
	}
	return 0;

}

int surface_input(struct spr16_msgdata_input_surface *surface)
{
	struct vec2 v;
	/* convert touch coords to screen space */
	v.x = (float)surface->xpos / (float)surface->xmax;
	v.y = (float)surface->ypos / (float)surface->ymax;
	v.x = v.x*g_screen->width;
	v.y = v.y*g_screen->height;
	if (update_contact(surface->input.code,
				v,
				surface->input.val,
				surface->input.ext)) {
		printf("update_contact fail, code= %d\n", surface->input.code);
		/* XXX return -1? */
		return 0;
	}
	return 0;
}

int touch_update()
{
	return 0;
}

int touch_draw()
{
	unsigned int i;
	const unsigned int colors = sizeof(g_palette)/sizeof(g_palette[0]);
	for (i = 0; i < colors; ++i)
	{
		morph_color(i, 1,1,1);
	}
	for (i = 0; i < SPR16_SURFACE_MAX_CONTACTS; ++i) {
		if (g_contacts[i].id >= 0) {
			unsigned int pindex = i % colors;
			uint16_t x, y, w, h;
			x = g_contacts[i].pos.x;
			y = g_contacts[i].pos.y;
			w = (50*g_contacts[i].magnitude);
			h = (50*g_contacts[i].magnitude);
			if (x+w >= g_screen->width)
				x = g_screen->width - w;
			if (y+h >= g_screen->height)
				y = g_screen->height - h;
			draw_fillrect(g_screen, x, y, w, h, g_palette[pindex]);
			if (spr16_client_sync(x, y, x+w, x+h, SPRITESYNC_FLAG_ASYNC)) {
				if (errno != EAGAIN) {
					return -1;
				}
			}
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
	if (spr16_client_handshake_start("touchpaint", sinfo->width,
				sinfo->height, 0))
		return -1;
	return 0;
}

int main(int argc, char *argv[])
{
	struct spr16 *screen;
	struct timespec tcur, tlast;
	int r;

	g_lock_colors = 0;
	g_ticks = 0;
	if (argc < 2) {
		printf("\n");
		printf("-----------------------------\n");
		printf("usage:\n");
		printf("-----------------------------\n");
		printf("touchpaint <server-name>\n");
		printf("e.g: touchpaint tty1\n\n");
		printf("\n");
		printf("-----------------------------\n");
		printf("keys:\n");
		printf("-----------------------------\n");
		printf("q: quit\n");
		printf("r: morph red channel\n");
		printf("g: morph green channel\n");
		printf("b: morph blue channel\n");
		printf("shift: reverse morph\n");
		printf("\n");
		return -1;
	}

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tlast))
		return -1;
	if (spr16_client_init())
		return -1;
	spr16_client_set_servinfo_handler(handle_servinfo);
	spr16_client_set_input_handler(regular_input);
	spr16_client_set_input_surface_handler(surface_input);

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
	if (touch_init(screen))
		return -1;
	while (1)
	{
		unsigned int usec;
		struct timespec elapsed;
		r = spr16_client_update(0);
		if (r == -1) {
			printf("spr16 update error\n");
			return -1;
		}
		if (touch_update())
			return -1;

		/* rate limit syncs, don't nuke the server */
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &tcur))
			return -1;
		elapsed.tv_sec  = tcur.tv_sec - tlast.tv_sec;
		if (!elapsed.tv_sec) {
			elapsed.tv_nsec = tcur.tv_nsec - tlast.tv_nsec;
			usec = elapsed.tv_nsec / 1000;
		}
		else {
			usec = ((1000000000 - tlast.tv_nsec) + tcur.tv_nsec) / 1000;
			usec += (elapsed.tv_sec-1) * 1000000;
		}

		if (usec >= 32000) { /* roughly 30fps */
			tlast = tcur;
			if (touch_draw())
				return -1;
		}
		/* spare client cpu usage */
		usleep(1000);
		++g_ticks;
	}
	spr16_client_shutdown();

	return 0;
}
