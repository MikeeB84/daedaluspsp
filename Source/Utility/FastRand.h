#ifndef UTILITY_FASTRAND_H_
#define UTILITY_FASTRAND_H_

#include "Base/Types.h"

// Cheap xorshift32 generator. The quality only needs to be good enough to
// spread work (texture checks, texture expiry) across frames.
inline u32 FastRand()
{
	static u32 state = 0x2545F491;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

#endif // UTILITY_FASTRAND_H_
