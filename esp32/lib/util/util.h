#ifndef _UTIL_H_
#define _UTIL_H_

#define MSEC(x) (x)
#define SECS(x) MSEC(x * 1000)
#define MINS(x) SECS(x * 60)
#define HOURS(x) MINS(x * 60)

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.0.0"
#endif

#endif