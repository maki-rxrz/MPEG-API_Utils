/*****************************************************************************
 * libmapi.c
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

#include "common.h"

#include <stdio.h>
#include <stdarg.h>

#include "mpeg_utils.h"

static struct {
    log_level   log_lv;
    FILE       *msg_out;
} debug_ctrl = { LOG_LV0, NULL };

extern void dprintf( log_level level, const char *format, ... )
{
    if( debug_ctrl.log_lv < level || !debug_ctrl.msg_out )
        return;
    va_list argptr;
    va_start( argptr, format );
    vfprintf( debug_ctrl.msg_out, format, argptr );
    va_end( argptr );
}

MAPI_EXPORT void mpeg_api_setup_log_lv( log_level level, FILE *output )
{
    debug_ctrl.log_lv  = level;
    debug_ctrl.msg_out = output;
}
