/*****************************************************************************
 * thread_posix.h
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

#define PTW32_STATIC_LIB 1

#include <pthread.h>

#include "thread_utils.h"

typedef struct {
    pthread_t       thread;
} thread_control_t;

extern void *thread_create( thread_func func, void *func_arg )
{
    thread_control_t *thread_ctrl = (thread_control_t *)malloc( sizeof(thread_control_t) );
    if( !thread_ctrl )
        return NULL;
    int result = pthread_create( &(thread_ctrl->thread), NULL, func, func_arg );
    if( result )
    {
        dprintf( LOG_LV0, "[log] thread_create()  result:%d\n", result );
        free( thread_ctrl );
        thread_ctrl = NULL;
    }
    return thread_ctrl;
}

extern void thread_wait_end( void * th, void **value_ptr )
{
    thread_control_t *thread_ctrl = (thread_control_t *)th;
    int result = pthread_join( thread_ctrl->thread, value_ptr );
    if( result )
        dprintf( LOG_LV0, "[log] thread_wait_end()  result:%d\n", result );
    free( thread_ctrl );
}
