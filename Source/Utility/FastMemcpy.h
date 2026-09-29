
#ifndef UTILITY_FASTMEMCPY_H_
#define UTILITY_FASTMEMCPY_H_

#include <stdlib.h>
#include <string.h>

//#define PROFILE_MEMCPY

#ifdef PROFILE_MEMCPY
void memcpy_test( void * dst, const void * src, size_t size );
#endif

void memcpy_byteswap( void* dst, const void* src, size_t size );	// Little endian, platform independent, ALWAYS swaps.

// memcpy_swizzle is just a regular memcpy on big-endian targets.
#define memcpy_swizzle 		memcpy_byteswap


// The PSP build defines fast versions of memcpy/memcpy_swizzle.

void memcpy_vfpu( void* dst, const void* src, size_t size );
void memcpy_vfpu_byteswap( void* dst, const void* src, size_t size );

#define fast_memcpy 		memcpy_vfpu
#define fast_memcpy_swizzle memcpy_vfpu_byteswap



#endif // UTILITY_FASTMEMCPY_H_
