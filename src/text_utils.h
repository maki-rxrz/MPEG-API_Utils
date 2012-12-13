/*****************************************************************************
 * text_utils.h
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
#ifndef __TEXT_UTILS_H__
#define __TEXT_UTILS_H__

#include <stdint.h>

#include "utils_def.h"

#define PUSH_LIST_DATA( p, a, b )                       \
do {                                                    \
    p->list_data[p->list_data_count].start = (a);       \
    p->list_data[p->list_data_count].end   = (b);       \
    ++ p->list_data_count;                              \
} while( 0 )

extern int text_load_cut_list( common_param_t *p, FILE *list );

extern void text_get_cut_list_type( common_param_t *p, const char *ext );

#endif /* __TEXT_UTILS_H__ */
