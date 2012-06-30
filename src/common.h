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
 *   A-4. When you release a modified version to public, you
 *       must publish it with your name.
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

typedef enum {
    LOG_LV0,
    LOG_LV1,
    LOG_LV2,
    LOG_LV3,
    LOG_LV4,
    LOG_LV_ALL
} log_level;

extern void dprintf( log_level level, const char *format, ... );

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>

#ifndef fseeko

#if defined(__MINGW32__) && defined(__i386__)
#define fseeko fseeko64
#define ftello ftello64
#elif _MSC_VER
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

#endif

#endif /* __COMMON_H__ */
