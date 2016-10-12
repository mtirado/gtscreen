/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#ifndef _GAME_H__
#define _GAME_H__

#define _GNU_SOURCE
#include <sys/time.h>
#include "../protocol/spr16.h"

extern struct spr16 *g_screen;

#define ARGB_SETA(var, val)  (var = (var & 0x00ffffff) | ((uint32_t)val<<24))
#define ARGB_SETR(var, val)  (var = (var & 0xff00ffff) | ((uint32_t)val<<16))
#define ARGB_SETG(var, val)  (var = (var & 0xffff00ff) | ((uint32_t)val<<8) )
#define ARGB_SETB(var, val)  (var = (var & 0xffffff00) |  (uint32_t)val     )
#define ARGB_GETA(var) ((var & 0xff000000) >>24)
#define ARGB_GETR(var) ((var & 0x00ff0000) >>16)
#define ARGB_GETG(var) ((var & 0x0000ff00) >>8 )
#define ARGB_GETB(var) ((var & 0x000000ff)     )

struct vec2
{
	float x;
	float y;
};

/* column major 3x3 matrix
 * 0 3 6
 * 1 4 7
 * 2 5 8
 * */
typedef float matrix3[9];

/* XXX velocities should be considered rdonly internal variables,
 * they are manipulated in the step function */
struct dynamic
{
	struct vec2 center;
	struct vec2 up;
	struct vec2 velocity;
	struct vec2 translate_force;
	float angular_velocity;
	float torque_force;
	float radius;
	float mass;
};

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

struct dynamics
{
	struct timespec tlast;
	struct object *objects;
	struct vec2 newtons; /* would be interesting to move
				this to struct dynamic instead of single g */
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



int dynamics_init(struct dynamics *self);
int dynamics_add_obj(struct dynamics *self, struct object *o);
int dynamics_remove_obj(struct dynamics *self, struct object *o);
int dynamics_step(struct dynamics *self, unsigned int min_uslice);
void dynamic_apply_impulse(struct dynamic *self, struct vec2 force);
void dynamic_apply_torque(struct dynamic *self,
			  struct vec2 offset, struct vec2 force);

int game_init();
int game_input(uint16_t flags, uint16_t keycode);
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
