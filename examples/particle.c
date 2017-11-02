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
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "util.h"
#include "game.h"

uint32_t g_noise;

struct emitter *emitter_create(struct vec2 center, struct vec2 direction, int type)
{
	struct emitter *self;
	self = calloc(1, sizeof(struct emitter));
	if (self == NULL)
		return NULL;

	switch (type)
	{
	case EMIT_MAIN_ENGINE:
		self->duration  = 4000000;
		self->max = 30000;
		self->color = 0xffaa071a;
		self->force = 35.0f;
		self->radius = 10;
		break;
	case EMIT_ROTATE:
		self->duration = 2000000;
		self->color = 0xff1aaa1a;
		self->max = 1000;
		self->force = 45.0f;
		self->radius = 4;
		break;
	default:
		goto free_fail;
	}
	if (self->max >= UINT32_MAX-1) {
		goto free_fail;
	}
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &self->tlast)) {
		goto free_fail;
	}
	self->particles = calloc(self->max, sizeof(struct particle));
	if (self->particles == NULL) {
		goto free_fail;
	}
	self->free_list = calloc(self->max, sizeof(uint32_t));
	if (self->free_list == NULL) {
		free(self->particles);
		goto free_fail;
	}
	self->direction = direction;
	self->center    = center;
	self->root_velocity.x = 0.0f;
	self->root_velocity.y = 0.0f;
	return self;

free_fail:
	free(self);
	return NULL;
}

int emitter_emit(struct emitter *self, uint32_t count)
{
	struct vec2 right;
	uint32_t z;

	vec2_rotate(&right, self->direction, 1.5708);
	if (count + self->num_active > self->max)
		count = self->max - self->num_active;

	for (z = 0; z < count; ++z)
	{
		uint32_t i = ++self->num_active;
		float force = self->force * (float)((3000+(g_noise%3000)) / 1000.0f);
		uint32_t t;
		self->particles[i].usecs_left  = self->duration;
		self->particles[i].color       = self->color;
		self->particles[i].velocity    = self->root_velocity;
		self->particles[i].velocity.x += self->direction.x * force;
		self->particles[i].velocity.y += self->direction.y * force;
		self->particles[i].center      = self->center;

		g_noise += self->center.x + z;
		t = g_noise % self->radius;
		self->particles[i].center.x += right.x * ((float)self->radius - ((t*2)));
		g_noise += self->center.y + z;
		t = g_noise % self->radius;
		self->particles[i].center.y += right.y * ((float)self->radius - ((t*2)));

		if (self->particles[i].center.x >= g_screen->width
				|| self->particles[i].center.x <= 0.0f
				|| self->particles[i].center.y >= g_screen->height
				|| self->particles[i].center.y <= 0.0f) {

			--self->num_active;
		}
	}
	return 0;
}

static int idx_in_free_list_rev(struct emitter *self, uint32_t idx)
{
	uint32_t i = self->free_count;
	if (i == 0)
		return 0;
	while (--i < UINT32_MAX)
	{
		if (self->free_list[i] == idx)
			return 1;
		if (i == 0)
			break;
	}
	return 0;
}

static int emitter_remove_inactive(struct emitter *self)
{
	uint32_t i = 0;
	uint32_t head_free = 0;

	for (i = 0; i < self->free_count; ++i)
	{
		uint32_t tail;

		/* there is probably an optimization to be made here */
		tail = self->num_active-1;
		if (idx_in_free_list_rev(self, tail)) {
			--self->free_count;
			continue;
		}

		memcpy(&self->particles[self->free_list[head_free]],
				&self->particles[tail],
				sizeof(struct particle));
		head_free++;
		--self->num_active;
		--self->free_count;

	}

	self->free_count = 0;
	return 0;
}

int emitter_update_draw(struct emitter *self)
{
	struct timespec tcur;
	uint32_t i;
	unsigned int usec;
	float timeslice;

	/* TODO global timer instead of syscalls everywhere */
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tcur))
		return -1;
	g_noise += tcur.tv_nsec+tcur.tv_sec;
	usec = usecs_elapsed(self->tlast, tcur);
	if (self->num_active == 0)
		return 0;
	timeslice = (float)usec / 1000000.0f;

	if ((usec == 0 || usec < /*min_uslice*/ 1000)) {
		/* draw without updating position */
		for (i = 0, self->free_count = 0; i < self->num_active; ++i)
		{
			if (self->particles[i].center.x >= g_screen->width
					|| self->particles[i].center.x <= 1.0f
					|| self->particles[i].center.y >= g_screen->height
					|| self->particles[i].center.y <= 1.0f) {
				self->free_list[self->free_count++] = i;
				continue;
			}
			draw_pixel(g_screen, self->particles[i].center.x,
					self->particles[i].center.y,
					self->particles[i].color);
		}
		return 0;
	}

	self->tlast = tcur;
	for (i = 0, self->free_count = 0; i < self->num_active; ++i)
	{
		self->particles[i].center.x += self->particles[i].velocity.x * timeslice;
		self->particles[i].center.y += self->particles[i].velocity.y * timeslice;
		if (self->particles[i].center.x >= g_screen->width
				|| self->particles[i].center.x <= 1.0f
				|| self->particles[i].center.y >= g_screen->height
				|| self->particles[i].center.y <= 1.0f) {
			self->free_list[self->free_count++] = i;
			continue;
		}
		else {
			draw_pixel(g_screen, self->particles[i].center.x,
					self->particles[i].center.y,
					self->particles[i].color);
		}

		self->particles[i].usecs_left -= usec;
		if (self->particles[i].usecs_left <= 0)
			self->free_list[self->free_count++] = i;
	}
	return emitter_remove_inactive(self);
}

int emitter_free(struct emitter *self)
{
	if (self == NULL || self->particles == NULL)
		return -1;
	free(self->particles);
	free(self);
	return 0;
}
