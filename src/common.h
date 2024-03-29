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
 * C. Moral rights of author belong to maki. Copyright is abandoned.
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
  #if __GNUC__ >= 4
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

#if   defined( MAPI_INTERNAL_CODE_ENABLED )
#define mapi_log mapi_debug_log
extern void mapi_log( log_level level, const char *format, ... );
#elif defined( MAPI_UTILS_CODE_ENABLED )
#define mapi_log mapi_utils_log
extern void mapi_log( log_level level, const char *format, ... );
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

/* OS depncdent */
#if defined( MAPI_INTERNAL_CODE_ENABLED ) || defined( MAPI_UTILS_CODE_ENABLED )

#ifdef _WIN32
typedef enum {
    SETMODE_TXT = 0,
    SETMODE_BIN = 1
} setmode_type;
extern int mapi_setmode( FILE *fp, setmode_type type );
extern FILE *mapi_fopen( const char *file_name, const char *mode );
extern int mapi_convert_args_to_utf8( int *argc_p, char ***argv_p );
#if   defined( MAPI_INTERNAL_CODE_ENABLED )
#define mapi_vfprintf vfprintf
#elif defined( MAPI_UTILS_CODE_ENABLED )
extern int mapi_vfprintf( FILE *stream, const char *format, va_list arg );
#endif
#else
#define mapi_setmode( fd, mode ) (0)
#define mapi_fopen  fopen
#define mapi_convert_args_to_utf8( argc_p, argv_p ) (0)
#define mapi_vfprintf vfprintf
#endif

#endif

/* etc */
#if defined(_WIN32)

#ifdef _MSC_VER

#define strdup    _strdup
#define wcsstrdup _wcstrdup
#define mbsdup    _mbsdup

#endif

#endif

/* Suppress warning by unused parameter. */
#define ENABLE_SUPPRESS_WARNINGS    1

#endif /* __COMMON_H__ */
