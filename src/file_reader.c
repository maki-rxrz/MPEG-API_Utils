/*****************************************************************************
 * file_reader.c
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

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mpegts_def.h"
#include "file_common.h"
#include "file_reader.h"

/*============================================================================
 *  Definition
 *==========================================================================*/

#define READ_BUFFER_DEFAULT_SIZE            (2048)

typedef struct {
    FILE       *fp;
    uint64_t    read_pos;
    int64_t     file_size;
    uint64_t    buffer_size;
    struct {
        uint8_t    *buf;
        uint64_t    size;
        uint64_t    pos;
    } cache;
} file_read_context_t;

/*============================================================================
 *  File Reader functions
 *==========================================================================*/

static uint64_t fr_get_file_size( void *ctx )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;
    return fr_ctx->file_size;
}

static int64_t fr_ftell( void *ctx )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;
    return fr_ctx->read_pos + fr_ctx->cache.pos;
}

static int fr_fread( void *ctx, uint8_t *read_buffer, int64_t read_size, int64_t *dst_size )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;

    uint8_t *buf  = read_buffer;
    int64_t  size = read_size;

    if( dst_size )
        *dst_size = 0;

    while( read_size )
    {
        uint64_t rest_size = fr_ctx->cache.size - fr_ctx->cache.pos;

        if( rest_size < read_size )
        {
            if( rest_size )
            {
                /* Copy the rest data from cache. */
                memcpy( buf, &(fr_ctx->cache.buf[fr_ctx->cache.pos]), rest_size );
                buf += rest_size;
                read_size -= rest_size;
                rest_size = 0;
            }

            /* Update the information of file read position. */
            fr_ctx->read_pos += fr_ctx->cache.size;

            /* Read data to cache. */
            uint64_t cache_size = fread( fr_ctx->cache.buf, 1, fr_ctx->buffer_size, fr_ctx->fp );
            if( cache_size == 0 )
                goto fail;
            fr_ctx->cache.size = cache_size;
            fr_ctx->cache.pos  = 0;
        }
        else
        {
            /* Copy data from cache. */
            if( buf )
                memcpy( buf, &(fr_ctx->cache.buf[fr_ctx->cache.pos]), read_size );
            fr_ctx->cache.pos += read_size;
            read_size = 0;
        }
    }

    if( dst_size )
        *dst_size = size;

    return MAPI_SUCCESS;

fail:
    if( dst_size )
        *dst_size = size - read_size;

    return MAPI_EOF;
}

static int fr_fseek( void *ctx, int64_t seek_offset, int origin )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;

    int64_t position = -1;

    /* Check seek position. */
    switch( origin )
    {
        case SEEK_SET :
            position = seek_offset;
            break;
        case SEEK_END :
            position = fr_ctx->file_size - seek_offset;
            break;
        case SEEK_CUR :
         /* position = fr_ftell( fr_ctx ) + seek_offset; */
            position = (fr_ctx->read_pos + fr_ctx->cache.pos) + seek_offset;
            break;
        default :
            break;
    }
    if( position < 0 || fr_ctx->file_size < position )
        return MAPI_FAILURE;

    /* Seek. */
    int64_t offset = position - fr_ctx->read_pos;
    if( 0 <= offset && offset < fr_ctx->cache.size )
    {
        /* Cache hit. */
        fr_ctx->cache.pos = offset;
    }
    else
    {
        /* No data in cache. */
        int64_t cache_start_pos = position / fr_ctx->buffer_size * fr_ctx->buffer_size;

        fseeko( fr_ctx->fp, cache_start_pos, SEEK_SET );
        fr_ctx->read_pos   = cache_start_pos;
        fr_ctx->cache.size = 0;
        fr_ctx->cache.pos  = 0;

        offset = position - cache_start_pos;
        if( offset )
            fr_fread( fr_ctx, NULL, offset, NULL );
    }
    return MAPI_SUCCESS;
}

static int fr_open( void *ctx, char *file_name, uint64_t buffer_size )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;

    int64_t   file_size = 0;
    uint8_t  *buffer    = NULL;
    FILE     *fp        = fopen( file_name, "rb" );
    if( !fp )
        return MAPI_FILE_ERROR;

    file_size = get_file_size( file_name );

    if( buffer_size == 0 )
        buffer_size = READ_BUFFER_DEFAULT_SIZE;

    buffer = (uint8_t *)malloc( buffer_size );
    if( !buffer )
        goto fail;

    /* Set up. */
    memset( fr_ctx, 0, sizeof(file_read_context_t) );
    fr_ctx->fp          = fp;
    fr_ctx->file_size   = file_size;
    fr_ctx->buffer_size = buffer_size;
    fr_ctx->cache.buf   = buffer;

    return MAPI_SUCCESS;

fail:
    fclose( fp );

    return MAPI_FAILURE;
}

static void fr_close( void *ctx )
{
    file_read_context_t *fr_ctx = (file_read_context_t *)ctx;

    if( fr_ctx->cache.buf )
        free( fr_ctx->cache.buf );
    if( fr_ctx->fp )
        fclose( fr_ctx->fp );

    memset( fr_ctx, 0, sizeof(file_read_context_t) );
}

static int fr_init( void **fr_ctx )
{
    file_read_context_t *ctx = (file_read_context_t *)malloc( sizeof(file_read_context_t) );
    if( !ctx )
        goto fail;

    memset( ctx, 0, sizeof(file_read_context_t) );
    *fr_ctx = (void *)ctx;

    return MAPI_SUCCESS;

fail:
    return MAPI_FAILURE;
}

static void fr_release( void **fr_ctx )
{
    if( !fr_ctx )
        return;

    file_read_context_t *ctx = (file_read_context_t *)(*fr_ctx);
    free( ctx );

    *fr_ctx = NULL;
}

/*============================================================================
 *  External reference items
 *==========================================================================*/

file_reader_t file_reader = {
    .get_size = fr_get_file_size,
    .ftell    = fr_ftell,
    .fread    = fr_fread,
    .fseek    = fr_fseek,
    .open     = fr_open,
    .close    = fr_close,
    .init     = fr_init,
    .release  = fr_release
};
