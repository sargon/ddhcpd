#include <stdarg.h>

#include "logger.h"

int log_level = LOG_LEVEL_DEFAULT;

ATTR_NONNULL_ALL void logger(int level, const char *prefix, ...)
{
	if (log_level >= level) {
		va_list args;
		char *fmt;

		fprintf(stderr, "%s", prefix);
		va_start(args, prefix);
		fmt = va_arg(args, __typeof__(fmt));
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}
