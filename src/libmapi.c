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
 * C. Moral rights of author belong to maki. Copyright is abandoned.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/

#include "common.h"

#include <stdarg.h>

#include "mpeg_utils.h"

static struct {
    log_level   log_lv;
    FILE       *msg_out;
} debug_ctrl = { 0 };

extern void mapi_log( log_level level, const char *format, ... )
{
    FILE *msg_out = debug_ctrl.msg_out;
    if( level == LOG_LV_OUTPUT )
        msg_out = stdout;
    else if( level == LOG_LV_PROGRESS )
        msg_out = stderr;
    if( debug_ctrl.log_lv < level || !msg_out )
        return;
    va_list argptr;
    va_start( argptr, format );
    vfprintf( msg_out, format, argptr );
    va_end( argptr );
}

MAPI_EXPORT void mpeg_api_setup_log_lv( log_level level, FILE *output )
{
    if( level != LOG_LV_KEEP )
        debug_ctrl.log_lv = level;
    if( output )
        debug_ctrl.msg_out = output;
}
