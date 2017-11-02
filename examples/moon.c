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

struct moon *moon_create(float mass, uint16_t fuel, uint32_t color)
{
	struct vec2 pos = { 200.0f, 200.0f };
	struct moon *newmoon = malloc(sizeof(struct moon));
	if (!newmoon)
		return NULL;
	memset(newmoon, 0, sizeof(struct moon));
	newmoon->mass = mass;
	/*int (*update)(void *self);*/
	newmoon->o.free   = (int (*)(void *))moon_free;
	newmoon->o.draw   = (int (*)(void *))moon_draw;
	newmoon->o.update = (int (*)(void *))moon_update;
	newmoon->argb     = color;
	newmoon->player = craft_create(pos, fuel);
	if (!newmoon->player)
		goto err;
	moon_add_obj(newmoon, &newmoon->player->o);

	if (dynamics_init(&newmoon->dyn_objs))
		goto err;
	if (dynamics_add_obj(&newmoon->dyn_objs, &newmoon->player->o))
		goto err;

	/* yeah that's ugly, macros could help */
	newmoon->player->o.dyn->center.x = 200.0f;
	newmoon->player->o.dyn->center.y = 200.0f;
	newmoon->player->o.dyn->mass = 300;
	newmoon->player->o.dyn->radius = 5.0f;
	newmoon->dyn_objs.newtons.x = 0;
	/*
	 * earth	9.8
	 * moon		1.625
	 * mars 	3.728
	 * pluto	0.610
	 */
	newmoon->dyn_objs.newtons.y = -1.625;

	return newmoon;

err:
	moon_free(newmoon);
	return NULL;
}

int moon_free(struct moon *self)
{
	/* TODO destroy all objects */
	free(self);
	return 0;
}

int moon_update(struct moon *self)
{
	struct object *obj = self->objects;
	while (obj)
	{
		obj->update(obj);
		obj = obj->next;
	}

	return 0;
}

int moon_draw(struct moon *self)
{
	/*const int horizon = 320;*/
	struct object *obj;

	draw_fillrect(g_screen, 20, 20, 50, 50, self->argb);

	obj = self->objects;
	while (obj)
	{
		obj->draw(obj);
		obj = obj->next;
	}

	return 0;
}

/* it's just a dumb ole linked list that doesn't even check for duplicates. */
int moon_add_obj(struct moon *self, struct object *o)
{
	o->next = self->objects;
	self->objects = o;
	return 0;
}

int moon_remove_obj(struct moon *self, struct object *o)
{
	struct object *i, *prev;
	prev = NULL;
	i = self->objects;
	while (i)
	{
		if (i == o) {
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
