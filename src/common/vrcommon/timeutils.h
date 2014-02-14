//========= Copyright Valve Corporation ============//
#pragma once

//*** High precision time support

#include <stdint.h>

// Returns a high-precision time in seconds since the first call to GetSystemTime() (or GetSystemTimeFromTicks()).
// NOTE: Time values are not valid across processes.
double GetSystemTime();

// Returns a high-precision 64-bit counter that can be used as a time stamp that
// is valid in context of any process on a machine.
// Use GetSystemTimeFromTicks() to convert to a time value in seconds.
uint64_t GetSystemTimeInTicks();

// Convert a 64-bit tick count obtained from GetSystemTickCount() to time in seconds.
// Note that ticks may be a value obtained in another process, but the time value
// returned is time since first call in this process.
double GetSystemTimeFromTicks(uint64_t ticks);

// Convert a time in seconds (returned by GetSystemTime() or GetSystemTimeFromTicks())
// into a system tick count value.
uint64_t GetSystemTicksFromTime(double time);
