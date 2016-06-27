/*****************************************************************************
 * thread_utils.c
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

#include <stdlib.h>

#include "thread_utils.h"

enum {
    MAPI_MCFGTHREAD    = 0,
    MAPI_PTHREAD       = 1,
    MAPI_WIN32THREAD   = 2,
    MAPI_UNKNOWNTHREAD = 3
};

#if defined(MAPI_MCFGTHREAD_ENABLED)

#include "thread_mcf.h"

#elif defined(MAPI_PTHREAD_ENABLED)

#include "thread_posix.h"

#elif defined(MAPI_WIN32THREAD_ENABLED)

#include "thread_win32.h"

#else
#error "Need the thread library..."
#endif

extern const char *thread_get_model_name( void )
{
    static const char *model_name[4] = { "mcf", "posix", "win32", "(unknown)" };
    static int model_type =
#if defined(MAPI_MCFGTHREAD_ENABLED)
        MAPI_MCFGTHREAD;
#elif defined(MAPI_PTHREAD_ENABLED)
        MAPI_PTHREAD;
#elif defined(MAPI_WIN32THREAD_ENABLED)
        MAPI_WIN32THREAD;
#else
        MAPI_UNKNOWNTHREAD;
#endif
    return model_name[model_type];
}
