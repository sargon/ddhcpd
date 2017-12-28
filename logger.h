#ifndef _LOGGER_H
#define _LOGGER_H

/**
 * A set of logging function which allow compile time and runtime logging decissions.
 */

#include <stdio.h>

#define LOG_FATAL   0
#define LOG_ERROR   5
#define LOG_WARNING 10
#define LOG_INFO    15
#define LOG_DEBUG   20

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_WARNING
#endif

#define HEX_NODE_ID(x) ((uint8_t*) x)[0],((uint8_t*) x)[1],((uint8_t*) x)[2],((uint8_t*) x)[3],((uint8_t*) x)[4],((uint8_t*) x)[5],((uint8_t*) x)[6],((uint8_t*) x)[7]

#define LOG(...) fprintf(stderr,__VA_ARGS__)

#if LOG_LEVEL >= LOG_FATAL
#define FATAL(...) do { \
    fprintf(stderr,"FATAL: "); \
    fprintf(stderr,__VA_ARGS__); \
  } while(0)
#else
#define FATAL(...)
#endif

#if LOG_LEVEL >= LOG_ERROR
#define ERROR(...) do { \
    fprintf(stderr,"ERROR: "); \
    fprintf(stderr,__VA_ARGS__); \
  } while(0)
#else
#define ERROR(...)
#endif

#if LOG_LEVEL >= LOG_WARNING
#define WARNING(...) do { \
    fprintf(stderr,"WARNING: "); \
    fprintf(stderr,__VA_ARGS__); \
  } while(0)
#else
#define WARNING(...)
#endif

#if LOG_LEVEL >= LOG_INFO
#define INFO(...) do { \
    fprintf(stderr,"INFO: "); \
    fprintf(stderr,__VA_ARGS__); \
  } while(0)
#else
#define INFO(...)
#endif

#if LOG_LEVEL >= LOG_DEBUG
#define DEBUG(...) do { \
    fprintf(stderr,"DEBUG: "); \
    fprintf(stderr,__VA_ARGS__); \
  } while(0)
#else
#define DEBUG(...)
#endif

#endif
