/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#ifndef _GAME_H__
#define _GAME_H__

#define _GNU_SOURCE
#include <time.h>

#include "../protocol/spr16.h"
#include "util.h"

extern struct spr16 *g_screen;

enum {
	OBJ_RAPTOR=0,
	OBJ_PAD,
	OBJ_CRAFT
};

/*
 * hacky C objects, give everything an object struct as first member
 */
struct object
{
	struct object *next;
	struct dynamic *dyn;
	uint16_t type;
	uint16_t flags;
	int (*update)(void *self);
	int (*draw)(void *self);
	int (*free)(void *self);
};

struct raptor
{
	struct object o;
	uint32_t argb;
};

struct pad
{
	struct object o;
};

struct craft
{
	struct object o;
	uint16_t fuel;
	uint32_t argb;
};

struct moon
{
	struct object o;
	struct dynamics dyn_objs;
	struct object *objects;
	struct craft *player;
	uint32_t argb;
	float mass;
};



int game_init();
int game_input(struct spr16_msgdata_input *input);
int game_update();
int game_draw();

struct moon *moon_create(float mass, uint16_t fuel, uint32_t color);
int moon_update(struct moon *self);
int moon_draw(struct moon *self);
int moon_add_obj(struct moon *self, struct object *o);
int moon_remove_obj(struct moon *self, struct object *o);
int moon_free(struct moon *self);

struct craft *craft_create(uint16_t fuel);
int craft_draw(struct craft *self);
int craft_free(struct craft *self);
int craft_update(struct craft *self);
void craft_thrust(struct craft *self, float force);
void craft_roll(struct craft *self, float torque);

int draw_fillrect(struct spr16 *screen, uint16_t x, uint16_t y,
		  uint16_t w, uint16_t h, uint32_t argb);
int draw_bitmap(struct spr16 *screen, char *bmp,
		uint16_t w, uint16_t h, uint32_t argb, matrix3 transform);

void vec2_transform(struct vec2 *out, struct vec2 v, matrix3 t);
void clear_transform(matrix3 out);
void set_transform(matrix3 out, float x, float y, float radians);
void vec2_normalize(struct vec2 *out);
float vec2_norml_angle(struct vec2 a, struct vec2 b);
void vec2_rotate(struct vec2 *out, struct vec2 v, float radians);
float vec2_cross(struct vec2 a, struct vec2 b);

#endif
