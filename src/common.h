/*****************************************************************************
 * common.h
 *****************************************************************************
 *
 * Authors: Masaki Tanaka <maki.rxrz@gmail.com>
 *
 * NYSL Version 0.9982 (en) (Unofficial)
 * ----------------------------------------
 * A. This software is "Everyone'sWare". It means:
 *   Anybody who has this software can use it as if he/she is
 *   the author.
 *
 *   A-1. Freeware. No fee is required.
 *   A-2. You can freely redistribute this software.
 *   A-3. You can freely modify this software. And the source
 *       may be used in any software with no limitation.
 *
 * B. The author is not responsible for any kind of damages or loss
 *   while using or misusing this software, which is distributed
 *   "AS IS". No warranty of any kind is expressed or implied.
 *   You use AT YOUR OWN RISK.
 *
 * C. Moral rights of author belong to maki. Copyright abandons.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/
#ifndef __COMMON_H__
#define __COMMON_H__

#if   defined(USE_MAPI_LIB)
  #undef  MAPI_DLL_EXPORT
  #undef  MAPI_DLL_IMPORT
  #undef  MAPI_INTERNAL_CODE_ENABLED
  #undef  MAPI_UTILS_CODE_ENABLED
#elif defined(USE_MAPI_DLL)
  #undef  MAPI_DLL_EXPORT
  #define MAPI_DLL_IMPORT
  #undef  MAPI_INTERNAL_CODE_ENABLED
  #undef  MAPI_UTILS_CODE_ENABLED
#endif

/* export */
#ifdef _WIN32
  #ifdef MAPI_DLL_EXPORT
    #ifdef __GNUC__
      #define MAPI_EXPORT __attribute__((dllexport))
    #else
      #define MAPI_EXPORT __declspec(dllexport)
    #endif
  #elif defined(MAPI_DLL_IMPORT)
    #ifdef __GNUC__
      #define MAPI_EXPORT __attribute__((dllimport))
    #else
      #define MAPI_EXPORT __declspec(dllimport)
    #endif
  #else
    #define MAPI_EXPORT
  #endif
#else
  #if __GCC__ >= 4
    #define MAPI_EXPORT __attribute__((visibility("default")))
  #else
    #define MAPI_EXPORT
  #endif
#endif

/* common */
typedef enum {
    MAPI_FILE_ERROR  = -3,
    MAPI_PARAM_ERROR = -2,
    MAPI_FAILURE     = -1,
    MAPI_SUCCESS     =  0,
    MAPI_EOF         =  1
} mapi_return_code_type;

/* log */
typedef enum {
    LOG_MODE_SILENT = -1,
    LOG_MODE_NORMAL =  0,
    LOG_MODE_OUTPUT_ALL
} log_mode;

typedef enum {
    LOG_LV_KEEP     = -4,
    LOG_LV_OUTPUT   = -3,
    LOG_LV_PROGRESS = -2,
    LOG_LV_SILENT   = -1,
    LOG_LV0         =  0,
    LOG_LV1,
    LOG_LV2,
    LOG_LV3,
    LOG_LV4,
    LOG_LV_ALL
} log_level;

#ifdef MAPI_INTERNAL_CODE_ENABLED
#define dprintf mpeg_api_debug_log
extern void dprintf( log_level level, const char *format, ... );
#elif MAPI_UTILS_CODE_ENABLED
extern void dprintf( log_level level, const char *format, ... );
#endif

/* file */
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>

#ifndef fseeko

#if defined(__MINGW32__) && defined(__i386__)
#define fseeko fseeko64
#define ftello ftello64
#elif defined(_MSC_VER)
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#define fseeko fseek
#define ftello ftell
#endif

#endif

/* etc */
#if defined(_WIN32)

#define strdup    _strdup
#define wcsstrdup _wcstrdup
#define mbsdup    _mbsdup

#ifndef _MBCS
#define _MBCS
#endif

#endif

#endif /* __COMMON_H__ */
