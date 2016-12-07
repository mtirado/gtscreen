/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include "game.h"

struct spr16 *g_screen;
struct moon *g_moon;
unsigned int g_frame;
unsigned int g_level;

/* note: y component is inverted because DRM origin is top left,
 * and i can't stand for using top left origin, yuck */
/* axis aligned rectangle */
int draw_fillrect(struct spr16 *screen, uint16_t x, uint16_t y,
		  uint16_t w, uint16_t h, uint32_t argb)
{
	uint32_t *fbmem = (uint32_t *)screen->shmem.addr;
	int fy,fx,fh;
	fy = screen->height - 1 - y;
	fh = fy - h;
	for (; fy > fh; --fy)
	{
		uint32_t *row = fbmem + (fy * screen->width);
		for (fx = 0; fx < w; ++fx)
		{
			*(row+fx+x) = argb;
		}
	}
	return 0;
}
#ifndef M_PI
	#define M_PI 3.141592f
#endif
#define VEC2_MINMAG 0.000001f
void vec2_normalize(struct vec2 *out)
{
	float mag = sqrtf((out->x * out->x) + (out->y * out->y));
	if (mag < VEC2_MINMAG)
		mag = VEC2_MINMAG;
	out->x /= mag;
	out->y /= mag;
}

/* input vectors should be normalized */
float vec2_norml_angle(struct vec2 a, struct vec2 b)
{
	float d = (a.x * b.x) + (a.y * b.y);
	if (d >= 1.0f)
		return 0.0f;
	else if (d <= -1.0f)
		return M_PI;
	else
		return acos(d);
}

void vec2_transform(struct vec2 *out, struct vec2 v, matrix3 t)
{
	/* vector * matrix(rotate+translate) */
	out->x = t[0] * v.x + t[3] * v.y + t[6];
	out->y = t[1] * v.x + t[4] * v.y + t[7];
}

float vec2_cross(struct vec2 a, struct vec2 b)
{
	return (a.x * b.y) - (a.y * b.x);
}

void vec2_rotate(struct vec2 *out, struct vec2 v, float radians)
{
	float cosr = cosf(radians);
	float sinr = sinf(radians);

	out->x = v.x *  cosr + v.y * sinr;
	out->y = v.x * -sinr + v.y * cosr;
}

void clear_transform(matrix3 out)
{
	out[0] = 1; out[1] = 0; out[2] = 0;
	out[3] = 0; out[4] = 1; out[5] = 0;
	out[6] = 0; out[7] = 0; out[8] = 1;
}

void set_transform(matrix3 out, float x, float y, float radians)
{
	float cosr = cosf(radians);
	float sinr = sinf(radians);
	out[0] =  cosr; out[1] = sinr; out[2] = 0;
	out[3] = -sinr; out[4] = cosr; out[5] = 0;
	out[6] =  x;    out[7] =  y;   out[8] = 1;
}

/* TODO optimized path for 0 transform */
int draw_bitmap(struct spr16 *screen, char *bmp,
		uint16_t w, uint16_t h, uint32_t argb, matrix3 transform)
{
	uint32_t *fbmem = (uint32_t *)screen->shmem.addr;
	struct vec2 src;
	struct vec2 t;
	uint32_t i = 0;
	uint16_t fy, fx;
	uint16_t hh, hw;
	const float osmp = 0.25f;
	hw = w / 2;
	hh = h / 2;
	for (fy = 0; fy < h; ++fy)
	{
		/*uint32_t *row = fbmem + ((fy+y) * screen->width);*/
		src.y = (h-fy-hh);
		for (fx = 0; fx < w; ++fx)
		{
			src.x = (fx-hw);
			if (bmp[i]) {
				vec2_transform(&t, src, transform);
				*(fbmem
				 +((screen->height - (uint16_t)(t.y-osmp))*screen->width)
				 +(uint16_t)(t.x-osmp)) = argb;
				*(fbmem
				 +((screen->height - (uint16_t)(t.y+osmp))*screen->width)
				 +(uint16_t)(t.x-osmp)) = argb;
				*(fbmem
				 +((screen->height - (uint16_t)(t.y-osmp))*screen->width)
				 +(uint16_t)(t.x+osmp)) = argb;
				*(fbmem
				 +((screen->height - (uint16_t)(t.y+osmp))*screen->width)
				 +(uint16_t)(t.x+osmp)) = argb;
			}
			++i;
		}
	}
	return 0;
}

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
