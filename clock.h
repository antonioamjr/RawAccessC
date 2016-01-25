#pragma once

#include <stdint.h>
#include <time.h>

static inline uint64_t
cf_getms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_nsec / 1000000) + ((uint64_t)ts.tv_sec * 1000);
}

static inline uint64_t
cf_getus()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_nsec / 1000) + ((uint64_t)ts.tv_sec * 1000000);
}

static inline uint64_t
cf_getns()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_nsec + ((uint64_t)ts.tv_sec * 1000000000);
}