/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef SYSPSP_BENCHMARK_H_
#define SYSPSP_BENCHMARK_H_

#include "Utility/DaedalusTypes.h"

// Benchmark mode: if benchmark.ini is in the DaedalusX64 folder, every ROM is run in
// turn for a fixed time with the timing display on, and a summary line per game is
// written to benchmark.txt. Progress is kept in benchmark_progress.txt, so after a
// crash or a restart the run carries on with the next ROM.

// N64 buttons the benchmark is pressing (ORed into controller 1)
extern volatile u32 gBenchmarkButtons;

bool	Benchmark_IsRequested();
void	Benchmark_Run( const char * eboot_path );		// Does not return

// Called from the crash handler: records the crash and restarts the EBOOT (or quits)
bool	Benchmark_IsRunning();
void	Benchmark_OnCrash();

#endif // SYSPSP_BENCHMARK_H_
