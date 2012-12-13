/*****************************************************************************
 * d2v_parser.h
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
#ifndef __D2V_PARSER_H__
#define __D2V_PARSER_H__

#include <inttypes.h>

typedef struct {
    void *          (*parse)(const char *input);
    void            (*release)(void *h);
    uint8_t *       (*create_keyframe_list)(void *h);
    uint32_t        (*get_total_frames)(void *h);
    const char *    (*get_filename)(void *h, int get_index);
} d2v_parser_t;

extern d2v_parser_t d2v_parser;

#endif /* __D2V_PARSER_H__ */
