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

#ifndef DEFINES_H__
#define DEFINES_H__


/*
 * pointer size
 */
#ifndef PTRBITCOUNT
	#define PTRBITCOUNT 32
#endif

/* kernel structs use __u64 for pointer types */
#if (PTRBITCOUNT == 32)
	#define ptr_from_krn(ptr) ((void *)(uint32_t)(ptr))
	#define ptr_to_krn(ptr)   ((uint32_t)(ptr))
#elif (PTRBITCOUNT == 64)
	#define ptr_from_krn(ptr) ((void *)(uint64_t)(ptr))
	#define ptr_to_krn(ptr)   ((uint64_t)(ptr))
#else
	#error "PTRBITCOUNT is undefined"
#endif


/*
 * resource limits
 */
#ifndef MAX_FBS
	#define MAX_FBS   12
#endif
#ifndef MAX_CRTCS
	#define MAX_CRTCS 12
#endif
#ifndef MAX_CONNECTORS
	#define MAX_CONNECTORS 12
#endif
#ifndef MAX_ENCODERS
	#define MAX_ENCODERS 12
#endif
#ifndef MAX_PROPS
	#define MAX_PROPS 256
#endif
#ifndef MAX_MODES
	#define MAX_MODES 256
#endif



#endif
