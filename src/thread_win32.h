/*****************************************************************************
 * thread_win32.h
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

//#include "thread_utils.h"

#include <windows.h>
#include <process.h>

typedef struct {
    HANDLE              thread;
    thread_func         func;
    void               *func_arg;
    thread_func_ret     ret;
} thread_control_t;

static unsigned __stdcall win32_thread_starter( void *arg )
{
    thread_control_t *thread_ctrl = (thread_control_t *)arg;
    thread_ctrl->ret = thread_ctrl->func( thread_ctrl->func_arg );
    return 0;
}

extern void *thread_create( thread_func func, void *func_arg )
{
    thread_control_t *thread_ctrl = (thread_control_t *)malloc( sizeof(thread_control_t) );
    if( !thread_ctrl )
        return NULL;
    thread_ctrl->func     = func;
    thread_ctrl->func_arg = func_arg;
    thread_ctrl->ret      = (thread_func_ret)(0);
    thread_ctrl->thread   = (HANDLE)_beginthreadex( NULL, 0, win32_thread_starter, (void *)thread_ctrl, 0, NULL );
    if( !thread_ctrl->thread )
    {
        mapi_log( LOG_LV0, "[log] thread_create()  result: Failed!\n" );
        free( thread_ctrl );
        thread_ctrl = NULL;
    }
    return thread_ctrl;
}

extern void thread_wait_end( void * th, void **value_ptr )
{
    thread_control_t *thread_ctrl = (thread_control_t *)th;
    WaitForSingleObject( thread_ctrl->thread, INFINITE );
    CloseHandle( thread_ctrl->thread );
    if( value_ptr )
        *value_ptr = thread_ctrl->ret;
    free( thread_ctrl );
}
