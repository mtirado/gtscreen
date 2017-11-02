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
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include "util.h"
#include "game.h"

struct spr16 *g_screen;
struct moon *g_moon;
unsigned int g_frame;
unsigned int g_level;
unsigned int g_exit;

static int g_main_thrust;
static int g_pos_roll;
static int g_neg_roll;

int game_init(struct spr16 *screen)
{
	g_exit = 0;
	g_frame = 0;
	g_level = 1;
	g_main_thrust = 0;
	g_pos_roll = 0;
	g_neg_roll = 0;
	g_screen = screen;
	g_moon  = moon_create(1.0, 20000, 0xffdaccd1);
	if (!g_moon)
		return -1;
	return 0;
}

int game_input(struct spr16_msgdata_input *input)
{
	switch (input->code)
	{
	case SPR16_KEYCODE_LEFT:
	case ',':
	case 'h':
		g_neg_roll = input->val;
		break;
	case SPR16_KEYCODE_RIGHT:
	case '.':
	case 'l':
		g_pos_roll = input->val;
		break;
	case SPR16_KEYCODE_UP:
	case ' ':
	case 'k':
		g_main_thrust = input->val;
		break;
	case 'q':
		g_exit = 1;
		break;
	default:
		break;
	}
	return 0;
}

int game_update()
{
	int r;

	if (g_exit)
		return -1;

	if (g_main_thrust)
		craft_thrust(g_moon->player, 110.0f);
	if (g_pos_roll)
		craft_roll(g_moon->player, 5.0f);
	if (g_neg_roll)
		craft_roll(g_moon->player, -5.0f);

	/* minimum step of 10 miliseconds */
	r = dynamics_step(&g_moon->dyn_objs, 10000);
	if (r < 0) {
		return -1;
	}
	else if (r == 0) {
		/* craft only applies force once per step */
		g_moon->player->rolling = 0;
		g_moon->player->thrusting = 0;
	}
	g_moon->o.update(g_moon);
	return 0;
}

int game_draw()
{
	if (spr16_client_waiting_for_vsync()) {
		return 0;
	}
	/* clear full screen */
	draw_fillrect(g_screen, 0, 0, g_screen->width, g_screen->height, 0xff000000);
	if (moon_draw(g_moon)) {
		return -1;
	}
	if (spr16_client_sync(0, 0, g_screen->width,
				g_screen->height, SPRITESYNC_FLAG_VBLANK)) {
		if (errno != EAGAIN) {
			return -1;
		}
	}
	return 0;
}
