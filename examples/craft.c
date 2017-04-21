/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <memory.h>
#include "game.h"

extern struct spr16 *g_screen;

char mesh[31*23] =
{
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
	0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
	0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
	0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
	0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
};

struct craft *craft_create(uint16_t fuel)
{
	struct craft *newcraft = malloc(sizeof(struct craft));
	if (!newcraft)
		return NULL;
	memset(newcraft, 0, sizeof(struct craft));
	newcraft->fuel = fuel;
	newcraft->argb = 0xffff0121;
	newcraft->o.free   = (int (*)(void *))craft_free;
	newcraft->o.draw   = (int (*)(void *))craft_draw;
	newcraft->o.update = (int (*)(void *))craft_update;
	return newcraft;
}

int craft_draw(struct craft *self)
{
	matrix3 transform;
	struct vec2 up;
	float rotation;
	if (!self->o.dyn)
		return -1;
	up.x =  0.0f;
	up.y = 1.0f;
	rotation = vec2_norml_angle(self->o.dyn->up, up);
	if (self->o.dyn->up.x > 0.0f)
		rotation = -rotation;
	set_transform(transform,
		      self->o.dyn->center.x,
		      self->o.dyn->center.y,
		      rotation);
	draw_bitmap(g_screen,
		    mesh,
		    31,
		    23,
		    self->argb,
		    transform);
	return 0;
}

void craft_roll(struct craft *self, float torque)
{
	struct vec2 force;
	struct vec2 point;
	point.x = 0.0f;
	point.y = self->o.dyn->radius;
	force.x = torque;
	force.y = 0.0f;
	dynamic_apply_torque(self->o.dyn, point, force);
}

void craft_thrust(struct craft *self, float force)
{
	struct vec2 impulse = self->o.dyn->up;
	impulse.x *= force;
	impulse.y *= force;
	dynamic_apply_impulse(self->o.dyn, impulse);
}
int craft_update(struct craft *self)
{
	uint32_t g = ARGB_GETG(self->argb);
	ARGB_SETG(self->argb, ++g);
	return 0;
}

int craft_free(struct craft *self)
{
	free(self);
	return 0;
}


