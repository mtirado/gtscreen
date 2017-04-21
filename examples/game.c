/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
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

int game_init(struct spr16 *screen)
{
	g_frame = 0;
	g_level = 1;
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
	case 'h':
		craft_roll(g_moon->player, -10.5f);
		break;
	case 'l':
		craft_roll(g_moon->player, 10.5f);
		break;
	case ' ':
	case 'k':
		craft_thrust(g_moon->player, 180.0f);
		break;
	/*case 'j':
		craft_thrust(g_moon->player, -180.0f);
		break;*/
	default:
		break;
	}
	return 0;
}

int game_update()
{
	if (dynamics_step(&g_moon->dyn_objs, 1000))
		return -1;
	g_moon->o.update(g_moon);
	return 0;
}

int game_draw()
{
	/* clear full screen */
	draw_fillrect(g_screen, 0, 0, g_screen->width, g_screen->height, 0xff000000);
	if (moon_draw(g_moon)) {
		return -1;
	}
	if (spr16_client_sync(0, 0, g_screen->width, g_screen->height )) {
		if (errno != EAGAIN) {
			return -1;
		}
	}
	return 0;
}
