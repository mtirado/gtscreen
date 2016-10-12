/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * TODO optimize
 */
#define _GNU_SOURCE
#include <time.h>
#include <memory.h>
#include <malloc.h>
#include "game.h"

/* FIXME single gravity vector hack */
struct vec2 g_gravity;

int dynamics_init(struct dynamics *self)
{
	memset(self, 0, sizeof(struct dynamics));
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &self->tlast))
		return -1;
	return dynamics_step(self, 0);
}

/* ideally we want dynamic structs packed tightly in memory, when that matters
 * we will also need a quadtree for early collison rejection
 */
int dynamics_add_obj(struct dynamics *self, struct object *o)
{
	struct dynamic *dyn;

	if (o->dyn || o->next)
		return -1;

	dyn = malloc(sizeof(struct dynamic));
	if (!dyn)
		return -1;
	memset(dyn, 0, sizeof(struct dynamic));
	dyn->mass = 1.0f;
	dyn->radius = 1.0f;
	dyn->up.x = 0.0f;
	dyn->up.y = 1.0f;
	o->dyn = dyn;
	o->next = self->objects;
	self->objects = o;
	return 0;
}

int dynamics_remove_obj(struct dynamics *self, struct object *o)
{
	struct object *i, *prev;
	if (!o->dyn)
		return -1;
	prev = NULL;
	i = self->objects;
	while (i)
	{
		if (i == o) {
			free(i->dyn);
			i->dyn = NULL;
			/* TODO remove check by using dbl pointer, if/when
			 * adding many ballistics being added/removed */
			if (prev == NULL) {
				self->objects = NULL;
			}
			else {
				prev->next = i->next;
			}
			return 0;
		}
		prev = i;
		i = i->next;
	}
	return -1;
}

void dynamic_step(struct dynamic *self, float timeslice)
{
	/* TODO "stick" rotational inertia I=(1/12)*m*r^2 r==length m==mass 1/12==??*/
	struct vec2 newup;
	float rotation;

	/* rotate */
	rotation = self->torque_force / self->mass;
	self->angular_velocity -= rotation * timeslice;
	vec2_rotate(&newup, self->up, self->angular_velocity);
	/*vec2_normalize(&newup);*/
	self->up = newup;

	/* apply gravity */
	/*self->velocity.x += g_gravity.x * timeslice */
	self->velocity.y += g_gravity.y * timeslice;
	/* translate */
	self->velocity.x += (self->translate_force.x / self->mass);
	self->velocity.y += (self->translate_force.y / self->mass);
	self->center.x += self->velocity.x * timeslice;
	self->center.y += self->velocity.y * timeslice;

	self->translate_force.x = 0.0f;
	self->translate_force.y = 0.0f;
	self->torque_force = 0.0f;
}

int dynamics_step(struct dynamics *self, unsigned int min_uslice)
{
	struct timespec elapsed, tcur;
	struct object *obj;
	float timeslice;
	unsigned int usec;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tcur))
		return -1;

	/* figure out roughly how many microseconds have gone by */
	elapsed.tv_sec  = tcur.tv_sec - self->tlast.tv_sec;
	if (!elapsed.tv_sec) {
		elapsed.tv_nsec = tcur.tv_nsec - self->tlast.tv_nsec;
		usec = elapsed.tv_nsec / 1000;
	}
	else {
		usec = ((1000000000 - self->tlast.tv_nsec) + tcur.tv_nsec) / 1000;
		usec += (elapsed.tv_sec-1) * 1000000;
	}

	if (usec == 0 || usec < min_uslice)
		return 0;

	g_gravity = self->newtons;
	timeslice = (float)usec / 1000000.0f;
	obj = self->objects;
	while (obj)
	{
		dynamic_step(obj->dyn, timeslice);
		obj = obj->next;
	}
	self->tlast = tcur;

	return 0;
}

void dynamic_apply_torque(struct dynamic *self, struct vec2 offset, struct vec2 force)
{
	self->torque_force += vec2_cross(offset, force);
}

void dynamic_apply_impulse(struct dynamic *self, struct vec2 force)
{
	self->translate_force.x += force.x;
	self->translate_force.y += force.y;
}


