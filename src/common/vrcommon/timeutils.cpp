//**********************************************************
// High-precision time functions
//
#include "timeutils.h"
#include "SDL_timer.h"

static bool systemTimeInitialized = false;
static double systemTicksPerSecond = 0;
static double systemSecondsPerTick = 0;
static uint64_t systemBaseTicks = 0;

static void InitSystemTime()
{
	systemTimeInitialized = true;

	systemSecondsPerTick = (double)SDL_GetPerformanceFrequency();
	systemTicksPerSecond = 1.0 / systemSecondsPerTick;
	systemBaseTicks = SDL_GetPerformanceCounter();
}

// Get current system time in ticks
uint64_t GetSystemTimeInTicks()
{
	return SDL_GetPerformanceCounter();
}

// Get current system in seconds since first call to a timing function
double GetSystemTime()
{
	if (!systemTimeInitialized)
		InitSystemTime();	// init before calling SDL_GetPerformanceCounter()

	return (SDL_GetPerformanceCounter() - systemBaseTicks) * systemTicksPerSecond;
}

// convert time in ticks to time in seconds
double GetSystemTimeFromTicks(uint64_t ticks)
{
	if (!systemTimeInitialized)
		InitSystemTime();

	// Cast to signed value, in case ticks is earlier than systemBaseTicks.
	return ((int64_t)(ticks - systemBaseTicks)) * systemTicksPerSecond;
}

// convert time in seconds to ticks
uint64_t GetSystemTicksFromTime(double time)
{
	if (!systemTimeInitialized)
		InitSystemTime();

	// cast to signed value, in case time value is negative
	return systemBaseTicks + (int64_t)(time * systemSecondsPerTick);
}
