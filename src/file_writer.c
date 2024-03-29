/*****************************************************************************
 * file_writer.c
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
#include <string.h>

#include "file_writer.h"

/*============================================================================
 *  Definition
 *==========================================================================*/

#define WRITE_BUFFER_DEFAULT_SIZE           (4096)

typedef enum {
    FW_STATUS_NOINIT = 0x00,
    FW_STATUS_CLOSED = 0x01,
    FW_STATUS_OPENED = 0x02,
    FW_STATUS_PIPE   = 0x04
} fw_status_type;

typedef struct {
    FILE           *fp;
    uint64_t        write_pos;
    int64_t         file_size;
    uint64_t        buffer_size;
    struct {
        uint8_t    *buf;
        uint64_t    size;
        uint64_t    pos;
    } cache;
    fw_status_type  status;
} file_write_context_t;

/*============================================================================
 *  File Writer functions
 *==========================================================================*/

static int fw_pipe( void *ctx )
{
    if( !ctx )
        return MAPI_FAILURE;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if( !(fw_ctx->status & FW_STATUS_OPENED) )
        return MAPI_FAILURE;
    fw_ctx->status |= FW_STATUS_PIPE;
    return MAPI_SUCCESS;
}

static inline void fw_flush_buffer( file_write_context_t *fw_ctx )
{
    if( fw_ctx->fp )
        fwrite( fw_ctx->cache.buf, 1, fw_ctx->cache.pos, fw_ctx->fp );
    if( fw_ctx->status & FW_STATUS_PIPE )
        fwrite( fw_ctx->cache.buf, 1, fw_ctx->cache.pos, stdout );
    fw_ctx->cache.pos = 0;
}

static int64_t fw_ftell( void *ctx )
{
    if( !ctx )
        return -1;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if( !(fw_ctx->status & FW_STATUS_OPENED) )
        return -1;
    return fw_ctx->file_size;
}

static int fw_fwrite( void *ctx, uint8_t *src_buffer, int64_t src_size, int64_t *dest_size )
{
    if( !ctx )
        return MAPI_FAILURE;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if( !(fw_ctx->status & FW_STATUS_OPENED) )
        return MAPI_FAILURE;

    uint8_t *buf  = src_buffer;
    int64_t  size = src_size;

    if( dest_size )
        *dest_size = 0;

    if( fw_ctx->cache.pos + size > fw_ctx->buffer_size )
    {
        int64_t write_size = 0;

        /* Output the data in cache.  */
        write_size += fw_ctx->cache.pos;
        fw_flush_buffer( fw_ctx );

        if( size > (int64_t)fw_ctx->buffer_size )
        {
            /* Output the data in source. */
            if( fw_ctx->fp )
                fwrite( buf, 1, size, fw_ctx->fp );
            if( fw_ctx->status & FW_STATUS_PIPE )
                fwrite( buf, 1, size, stdout );
            write_size += size;
            size = 0;
        }

        /* Update the information of file write position. */
        fw_ctx->write_pos += write_size;
    }

    if( size )
    {
        /* Cache the data to buffer. */
        memcpy( &(fw_ctx->cache.buf[fw_ctx->cache.pos]), buf, size );
        fw_ctx->cache.pos += size;
        size = 0;
    }

    /* Update the information of file size. */
    fw_ctx->file_size += src_size;

    if( dest_size )
        *dest_size = src_size;

    return MAPI_SUCCESS;

#if 0       // FIXME: This code assumed that fwrite fails.
fail:
    if( dest_size )
        *dest_size = 0;

    return MAPI_FAILURE;
#endif
}

static int fw_fseek( void *ctx, int64_t seek_offset, int origin )
{
    if( !ctx )
        return MAPI_FAILURE;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if( fw_ctx->status != FW_STATUS_OPENED )
        return MAPI_FAILURE;

    int64_t position = -1;

    /* Check seek position. */
    switch( origin )
    {
        case SEEK_SET :
            position = seek_offset;
            break;
        case SEEK_END :
            position = fw_ctx->file_size - seek_offset;
            break;
        case SEEK_CUR :
            position = fw_ctx->file_size + seek_offset;
            break;
        default :
            break;
    }
    if( position < 0 || fw_ctx->file_size < position )
        return MAPI_FAILURE;

    /* Seek. */
    if( fw_ctx->fp )
        fseeko( fw_ctx->fp, position, SEEK_SET );

    /* Clear cache. */
    fw_ctx->write_pos  = position;
    fw_ctx->file_size  = position;
    fw_ctx->cache.size = 0;
    fw_ctx->cache.pos  = 0;

    return MAPI_SUCCESS;
}

static int fw_open( void *ctx, char *file_name, uint64_t buffer_size )
{
    if( !ctx )
        return MAPI_FAILURE;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if(  fw_ctx->status != FW_STATUS_CLOSED )
        return MAPI_FAILURE;

    uint8_t *buffer = NULL;
    FILE    *fp     = NULL;
    if( file_name && !(fp = mapi_fopen( file_name, "wb" )) )
        return MAPI_FILE_ERROR;

    if( buffer_size == 0 )
        buffer_size = WRITE_BUFFER_DEFAULT_SIZE;

    buffer = (uint8_t *)malloc( buffer_size );
    if( !buffer )
        goto fail;

    /* Set up. */
    memset( fw_ctx, 0, sizeof(file_write_context_t) );
    fw_ctx->fp          = fp;
    fw_ctx->buffer_size = buffer_size;
    fw_ctx->cache.buf   = buffer;
    fw_ctx->status      = FW_STATUS_OPENED;

    return MAPI_SUCCESS;

fail:
    if( file_name )
        fclose( fp );

    return MAPI_FAILURE;
}

static void fw_close( void *ctx )
{
    if( !ctx )
        return;
    file_write_context_t *fw_ctx = (file_write_context_t *)ctx;
    if( !(fw_ctx->status & FW_STATUS_OPENED) )
        return;

    /* Output data in cache. */
    if( fw_ctx->cache.pos )
        fw_flush_buffer( fw_ctx );

    if( fw_ctx->cache.buf )
        free( fw_ctx->cache.buf );
    if( fw_ctx->fp )
        fclose( fw_ctx->fp );

    memset( fw_ctx, 0, sizeof(file_write_context_t) );
    fw_ctx->status = FW_STATUS_CLOSED;
}

static int fw_init( void **fw_ctx )
{
    if( !fw_ctx || *fw_ctx )
        return MAPI_FAILURE;
    file_write_context_t *ctx = (file_write_context_t *)malloc( sizeof(file_write_context_t) );
    if( !ctx )
        return MAPI_FAILURE;

    memset( ctx, 0, sizeof(file_write_context_t) );
    ctx->status = FW_STATUS_CLOSED;
    *fw_ctx = (void *)ctx;

    return MAPI_SUCCESS;
}

static void fw_release( void **fw_ctx )
{
    if( !fw_ctx || !(*fw_ctx) )
        return;
    file_write_context_t *ctx = (file_write_context_t *)(*fw_ctx);

    fw_close( (void *)ctx );

    free( ctx );

    *fw_ctx = NULL;
}

/*============================================================================
 *  External reference items
 *==========================================================================*/

file_writer_t file_writer = {
    .pipe     = fw_pipe,
    .ftell    = fw_ftell,
    .fwrite   = fw_fwrite,
    .fseek    = fw_fseek,
    .open     = fw_open,
    .close    = fw_close,
    .init     = fw_init,
    .release  = fw_release
};
