#pragma once

/**
 * A set of logging function which allow compile time and runtime logging decissions.
 */

#include <stdio.h>

#include "types.h"

#define LOG_FATAL 0
#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_INFO 3
#define LOG_DEBUG 4

#define LOG_LEVEL_MAX LOG_DEBUG

#ifndef LOG_LEVEL_LIMIT
#define LOG_LEVEL_LIMIT 255
#endif

#ifndef LOG_LEVEL_DEFAULT
#define LOG_LEVEL_DEFAULT LOG_WARNING
#endif

#define HEX_NODE_ID(x) ((uint8_t*) x)[0],((uint8_t*) x)[1],((uint8_t*) x)[2],((uint8_t*) x)[3],((uint8_t*) x)[4],((uint8_t*) x)[5],((uint8_t*) x)[6],((uint8_t*) x)[7]

ATTR_NONNULL_ALL void logger(int level, const char* prefix, ...);

#define LOG(...) logger(-1, "",__VA_ARGS__)

#if LOG_LEVEL_LIMIT >= LOG_FATAL
#define FATAL(...) logger(LOG_FATAL, "FATAL: ", __VA_ARGS__)
#else
#define FATAL(...)
#endif

#if LOG_LEVEL_LIMIT >= LOG_ERROR
#define ERROR(...) logger(LOG_ERROR, "ERROR: ", __VA_ARGS__)
#else
#define ERROR(...)
#endif

#if LOG_LEVEL_LIMIT >= LOG_WARNING
#define WARNING(...) logger(LOG_WARNING, "WARNING: ", __VA_ARGS__)
#else
#define WARNING(...)
#endif

#if LOG_LEVEL_LIMIT >= LOG_INFO
#define INFO(...) logger(LOG_INFO, "INFO: ", __VA_ARGS__)
#else
#define INFO(...)
#endif

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
#define DEBUG(...) logger(LOG_DEBUG, "DEBUG: ", __VA_ARGS__)
#else
#define DEBUG(...)
#endif
