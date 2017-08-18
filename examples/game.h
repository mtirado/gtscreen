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
 * simple C objects, give everything an object struct as first member
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

/* TODO -- convert to structure of arrays for simd */
struct particle
{
	struct vec2 center;
	struct vec2 velocity;
	uint32_t color;
	int32_t usecs_left;
};

enum {
	EMIT_MAIN_ENGINE = 1,
	EMIT_ROTATE
};

struct emitter
{
	struct vec2 root_velocity;
	struct vec2 direction;
	struct vec2 center;
	float force;
	uint32_t radius;
	uint32_t max;
	uint32_t num_active;
	struct timespec tlast;
	uint32_t color;
	struct particle *particles;
	uint32_t  free_count;
	uint32_t *free_list; /* this is [max] sized, but it doesn't have to be */
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
	struct emitter *main_thruster;
	struct emitter *port_thruster;
	struct emitter *starboard_thruster;
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

struct craft *craft_create(struct vec2 center, uint16_t fuel);
int craft_draw(struct craft *self);
int craft_free(struct craft *self);
int craft_update(struct craft *self);
void craft_thrust(struct craft *self, float force);
void craft_roll(struct craft *self, float torque);

struct emitter *emitter_create(struct vec2 center, struct vec2 direction, int type);
int emitter_emit(struct emitter *self, uint32_t count);
int emitter_update(struct emitter *self);
int emitter_free(struct emitter *self);

#endif
