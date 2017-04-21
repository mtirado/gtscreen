/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
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
