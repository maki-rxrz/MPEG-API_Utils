/*****************************************************************************
 * utils_def.h
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
#ifndef __UTILS_DEF_H__
#define __UTILS_DEF_H__

#include <stdint.h>

typedef enum {
    MPEG_READER_M2VVFAPI = 0,       /* default */
    MPEG_READER_DGDECODE = 1,
    MPEG_READER_LIBAV    = 2,
    MPEG_READER_TMPGENC  = 3,
    MPEG_READER_NONE     = 4,
    READER_TYPE_MAX      = MPEG_READER_NONE
} mpeg_reader_type;

typedef enum {
    CUT_LIST_DEL_TEXT  = 0,         /* default */
    CUT_LIST_AVS_TRIM  = 1,
    CUT_LIST_VCF_RANGE = 2,
    CUT_LIST_KEY_AUTO  = 3,
    CUT_LIST_KEY_CUT_O = 4,
    CUT_LIST_KEY_CUT_E = 5,
    CUT_LIST_KEY_TRIM  = 6,
    CUT_LIST_TYPE_MAX
} cut_list_type;

typedef struct {
    int32_t             start;
    int32_t             end;
} cut_list_data_t;

#define UTILS_COMMON_PARAMTER               \
    char               *list;               \
    char               *list_search_word;   \
    cut_list_type       list_type;          \
    int                 list_key_type;      \
    cut_list_data_t    *list_data;          \
    int                 list_data_count;    \
    mpeg_reader_type    reader;             \
    int64_t             reader_delay;       \
    int64_t             delay_time;         \
    uint32_t            line_max;           \

typedef struct {
    UTILS_COMMON_PARAMTER
} common_param_t;

#endif /* __UTILS_DEF_H__ */
