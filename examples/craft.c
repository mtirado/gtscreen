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
#include <malloc.h>
#include <memory.h>
#include "game.h"

extern struct spr16 *g_screen;
#define CRAFT_WIDTH  31
#define CRAFT_HEIGHT 23
char mesh[CRAFT_WIDTH * CRAFT_HEIGHT] =
{
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,
	0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
	0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,
	0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,
	0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
};

struct craft *craft_create(struct vec2 center, uint16_t fuel)
{
	struct vec2 tvec = { 1.0f, 0.0f };
	struct craft *newcraft = malloc(sizeof(struct craft));
	if (!newcraft)
		return NULL;
	memset(newcraft, 0, sizeof(struct craft));
	newcraft->o.free   = (int (*)(void *))craft_free;
	newcraft->o.draw   = (int (*)(void *))craft_draw;
	newcraft->o.update = (int (*)(void *))craft_update;

	newcraft->fuel = fuel;
	newcraft->argb = 0xffff0121;
	newcraft->main_thruster = emitter_create(center, tvec, EMIT_MAIN_ENGINE);
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


	self->main_thruster->center = self->o.dyn->center;
	self->main_thruster->root_velocity = self->o.dyn->velocity;
	self->main_thruster->color = self->argb;
	emitter_update_draw(self->main_thruster);


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
	if (self->rolling)
		return;
	point.x = 0.0f;
	point.y = self->o.dyn->radius;
	force.x = torque;
	force.y = 0.0f;
	dynamic_apply_torque(self->o.dyn, point, force);
	self->rolling = 1;
}

void craft_thrust(struct craft *self, float force)
{
	struct vec2 impulse = self->o.dyn->up;
	if (self->thrusting)
		return;
	impulse.x *= force;
	impulse.y *= force;
	dynamic_apply_impulse(self->o.dyn, impulse);
	emitter_emit(self->main_thruster, 80);
	self->thrusting = 1;
}
int craft_update(struct craft *self)
{
	uint32_t g = ARGB_GETG(self->argb);
	ARGB_SETG(self->argb, ++g);
	self->main_thruster->direction.x = self->o.dyn->up.x * -1.0f;
	self->main_thruster->direction.y = self->o.dyn->up.y * -1.0f;
	vec2_normalize(&self->main_thruster->direction);
	/* debug center */
	draw_pixel(g_screen, self->o.dyn->center.x,
			self->o.dyn->center.y,
			0xffffffff);

	return 0;
}

int craft_free(struct craft *self)
{
	free(self);
	return 0;
}


