/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef DYNAREC_MIPS_CODEBUFFERINFOPSP_H_
#define DYNAREC_MIPS_CODEBUFFERINFOPSP_H_

#include "Base/Types.h"

// Where the PSP dynarec code buffers are and how much of them is used (for perf.txt / exception.txt)
struct SCodeBufferInfoPSP
{
	const u8 *	PrimaryBase;
	u32			PrimaryUsed;
	const u8 *	SecondaryBase;
	u32			SecondaryUsed;
	u32			Size;
};

bool	CodeBufferPSP_GetInfo( SCodeBufferInfoPSP * info );

#endif // DYNAREC_MIPS_CODEBUFFERINFOPSP_H_
