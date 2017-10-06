/* wtime.h
 * for Joker TV under Windows OS
 */

#ifndef _WTIME_H
#define _WTIME_H 1

/* based on https://github.com/mayah/tinytoml */

#if defined(_WIN32)
// On Windows, Visual Studio does not define gmtime_r. However, mingw might
// do (or might not do). See https://github.com/mayah/tinytoml/issues/25,
#ifndef gmtime_r
static struct tm* gmtime_r(const time_t* t, struct tm* r)
{
	// gmtime is threadsafe in windows because it uses TLS
	struct tm *theTm = gmtime(t);
	if (theTm) {
		*r = *theTm;
		return r;
	} else {
		return 0;
	}
}
#endif  // gmtime_r
#endif  // _WIN32
#endif
