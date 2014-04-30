#ifndef __mekdotlu_clock_h
#define __mekdotlu_clock_h

#include <sys/time.h>
#include <time.h>

#if defined(__APPLE__)
#define CLOCK_REALTIME  0x01
#define CLOCK_MONOTONIC 0x02

int clock_gettime(int clock_id, struct timespec *ts);
#endif /* defined(__APPLE__) */

#endif /* __mekdotlu_clock_h */

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
