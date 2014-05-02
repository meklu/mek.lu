#if !defined(__APPLE__)
# error clock.c is only needed on Mac OS X
#endif /* defined(__APPLE__) */

#include "clock.h"
#include <mach/mach_time.h>
#include <mach/clock.h>
#include <mach/mach.h>
#include <errno.h>

static uint64_t timebase_factor = 0;
int clock_gettime(int clock_id, struct timespec *ts) {
	if (timebase_factor == 0) {
		mach_timebase_info_data_t timebase_info;
		(void)mach_timebase_info(&timebase_info);
		timebase_factor = timebase_info.numer / timebase_info.denom;
	}

	switch (clock_id)
	{
		case CLOCK_MONOTONIC:
		{
			uint64_t nanoseconds = (mach_absolute_time() * timebase_factor);
			ts->tv_sec  = nanoseconds / 1E9;
			ts->tv_nsec = nanoseconds - ts->tv_sec;
			return 0;
		}
		case CLOCK_REALTIME:
		{
			clock_serv_t cclock = MACH_PORT_NULL;
			mach_timespec_t mts;

			host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
			clock_get_time(cclock, &mts);
			mach_port_deallocate(mach_task_self(), cclock);

			ts->tv_sec = mts.tv_sec;
			ts->tv_nsec = mts.tv_nsec;
			return 0;
		}
		default:
			errno = EINVAL;
			return -1;
	}
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
