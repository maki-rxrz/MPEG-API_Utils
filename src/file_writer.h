/*****************************************************************************
 * file_writer.h
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
#ifndef __FILE_WRITER_H__
#define __FILE_WRITER_H__

#include <stdint.h>

/*============================================================================
 *  Definition
 *==========================================================================*/

typedef struct  {
    int         (* pipe    )( void *fr_ctx );
    int64_t     (* ftell   )( void *fr_ctx );
    int         (* fwrite  )( void *fw_ctx, uint8_t *src_buffer, int64_t src_size, int64_t *dest_size );
    int         (* fseek   )( void *fw_ctx, int64_t offset, int origin );
    int         (* open    )( void *fw_ctx, char *file_name, uint64_t buffer_size );
    void        (* close   )( void *fw_ctx );
    int         (* init    )( void **fw_ctx );
    void        (* release )( void **fw_ctx );
} file_writer_t;

/*============================================================================
 *  External reference items
 *==========================================================================*/

extern file_writer_t file_writer;

#endif /* __FILE_WRITER_H__ */
