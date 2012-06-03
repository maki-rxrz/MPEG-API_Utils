/*****************************************************************************
 * mpeges_parser.c
 *****************************************************************************
 *
 * Copyright (C) 2012 maki
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
 *   A-4. When you release a modified version to public, you
 *       must publish it with your name.
 *
 * B. The author is not responsible for any kind of damages or loss
 *   while using or misusing this software, which is distributed
 *   "AS IS". No warranty of any kind is expressed or implied.
 *   You use AT YOUR OWN RISK.
 *
 * C. Copyrighted to maki.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/

#include "common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mpeg_common.h"
#include "mpeg_stream.h"
#include "mpeg_parser.h"

typedef struct {
    parser_status_type      status;
    FILE                   *input;
    int64_t                 read_position;
    int64_t                 video_position;
    uint8_t                 video_stream_type;
    int64_t                 gop_number;
    uint32_t                fps_numerator;
    uint32_t                fps_denominator;
    int64_t                 timestamp_base;
    int32_t                 total_picture_num;
    int32_t                 picture_num;
} mpeges_info_t;

static int mpeges_first_check( mpeges_info_t *info )
{
    int result = -1;
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    if( fread( mpeg_video_head_data, 1, MPEG_VIDEO_START_CODE_SIZE, info->input ) != MPEG_VIDEO_START_CODE_SIZE )
        goto end_first_check;
    if( mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SHC ) )
        goto end_first_check;
    result = 0;
end_first_check:
    fsetpos( info->input, &start_position );
    return result;
}

#define BYTE_DATA_SHIFT( data, size )           \
{                                               \
    for( int i = 1; i < size; ++i )             \
        data[i - 1] = data[i];                  \
}

static int mpeges_parse_stream_type( mpeges_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpeges_parse_stream_type()\n" );
    /* parse raw data. */
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    if( fread( mpeg_video_head_data, 1, MPEG_VIDEO_START_CODE_SIZE - 1, info->input ) != MPEG_VIDEO_START_CODE_SIZE - 1)
        return -1;
    int result = -1;
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    mpeg_video_info_t video_info;
    while( 1 )
    {
        if( !fread( &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1, 1, info->input ) )
            break;
        if( !mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
        {
            uint8_t identifier;
            if( !fread( &identifier, 1, 1, info->input ) )
                goto end_parse_stream_type;
            fseeko( info->input, -1, SEEK_CUR );
            mpeg_video_start_code_info_t start_code_info;
            if( mpeg_video_judge_start_code( mpeg_video_head_data, identifier, &start_code_info ) )
            {
                BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
                continue;
            }
            uint32_t read_size = start_code_info.read_size;
            int64_t start_code_position = ftello( info->input ) - MPEG_VIDEO_START_CODE_SIZE;
            /* get header/extension information. */
            uint8_t buf[read_size];
            if( fread( buf, 1, read_size, info->input ) != read_size )
                goto end_parse_stream_type;
            int64_t check_size = mpeg_video_get_header_info( buf, start_code_info.start_code, &video_info );
            if( check_size < read_size )
                fseeko( info->input, start_code_position + MPEG_VIDEO_START_CODE_SIZE + check_size, SEEK_SET );
            /* debug. */
            mpeg_video_debug_header_info( &video_info, start_code_info.searching_status );
            /* check the status detection. */
            if( start_code_info.searching_status == DETECT_SHC )
            {
                result = 0;
                info->video_stream_type = STREAM_VIDEO_MPEG1;
            }
            else if( start_code_info.searching_status == DETECT_SESC )
            {
                info->video_stream_type = STREAM_VIDEO_MPEG2;
                goto end_parse_stream_type;
            }
            else if( !result )
                goto end_parse_stream_type;
            /* cleanup buffer. */
            memset( mpeg_video_head_data, 0xFF, MPEG_VIDEO_START_CODE_SIZE );
        }
        BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
    }
end_parse_stream_type:
    fsetpos( info->input, &start_position );
    return result;
}

static void mpeges_get_stream_timestamp( mpeges_info_t *info, mpeg_video_info_t *video_info, int64_t *pts_set_p, int64_t *dts_set_p )
{
    dprintf( LOG_LV2, "[check] mpeges_get_stream_timestamp()\n" );
    dprintf( LOG_LV3, "[debug] fps: %u / %u\n"
                      "        timestamp_base:%"PRId64"\n"
                      "        total_picture_num:%d\n"
                      "        picture_num:%d\n"
                      "        picture_coding_type:%u\n"
                      "        temporal_reference:%u\n"
                      , info->fps_numerator, info->fps_denominator, info->timestamp_base, info->total_picture_num, info->picture_num
                      , video_info->picture.picture_coding_type, video_info->picture.temporal_reference );
    /* calculate timestamp. */
    int64_t pts = info->timestamp_base * (info->total_picture_num + video_info->picture.temporal_reference);
    int64_t dts = (video_info->picture.picture_coding_type != MPEG_VIDEO_B_FRAME
                 && video_info->picture.temporal_reference != info->picture_num)
                 ? info->timestamp_base * (info->total_picture_num + info->picture_num - 1)
                 : pts;
    /* setup. */
    *pts_set_p = pts;
    *dts_set_p = dts;
}

static int mpeges_get_mpeg_video_picture_info( mpeges_info_t *info, mpeg_video_info_t *video_info )
{
    dprintf( LOG_LV2, "[check] mpeges_get_mpeg_video_picture_info()\n" );
    /* parse raw data. */
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    if( fread( mpeg_video_head_data, 1, MPEG_VIDEO_START_CODE_SIZE - 1, info->input ) != MPEG_VIDEO_START_CODE_SIZE - 1)
        return -1;
    int result = -1;
    int64_t read_position = -1;
    while( 1 )
    {
        if( !fread( &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1, 1, info->input ) )
            break;
        if( !mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
        {
            uint8_t identifier;
            if( !fread( &identifier, 1, 1, info->input ) )
                goto end_get_video_picture_info;
            fseeko( info->input, -1, SEEK_CUR );
            mpeg_video_start_code_info_t start_code_info;
            if( mpeg_video_judge_start_code( mpeg_video_head_data, identifier, &start_code_info ) )
            {
                BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
                continue;
            }
            uint32_t read_size = start_code_info.read_size;
            int64_t start_code_position = ftello( info->input ) - MPEG_VIDEO_START_CODE_SIZE;
            /* get header/extension information. */
            uint8_t buf[read_size];
            if( fread( buf, 1, read_size, info->input ) != read_size )
                goto end_get_video_picture_info;
            int64_t check_size = mpeg_video_get_header_info( buf, start_code_info.start_code, video_info );
            if( check_size < read_size )
                fseeko( info->input, start_code_position + MPEG_VIDEO_START_CODE_SIZE + check_size, SEEK_SET );
            /* debug. */
            mpeg_video_debug_header_info( video_info, start_code_info.searching_status );
            /* check the status detection. */
            if( start_code_info.searching_status == DETECT_SHC )
                read_position = start_code_position;
            else if( start_code_info.searching_status == DETECT_SESC )
            {
                mpeg_video_get_frame_rate( video_info, &(info->fps_numerator), &(info->fps_denominator) );
                info->timestamp_base = 90 * 1000 * info->fps_denominator / info->fps_numerator;
            }
            else if( start_code_info.searching_status == DETECT_GSC )
            {
                ++ info->gop_number;
                info->total_picture_num += info->picture_num;
                info->picture_num = -1;
            }
            else if( start_code_info.searching_status == DETECT_PSC )
            {
                if( read_position == -1 )
                    read_position = start_code_position;
                ++ info->picture_num;
                result = 0;
            }
            else if( start_code_info.searching_status == DETECT_SSC
                  || start_code_info.searching_status == DETECT_SEC )
                goto end_get_video_picture_info;
            /* cleanup buffer. */
            memset( mpeg_video_head_data, 0xFF, MPEG_VIDEO_START_CODE_SIZE );
        }
        BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
    }
end_get_video_picture_info:
    info->read_position = (result) ? -1 : read_position;
    return result;
}

int64_t mpeges_seek_next_start_position( mpeges_info_t *info )
{
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    if( fread( mpeg_video_head_data, 1, MPEG_VIDEO_START_CODE_SIZE - 1, info->input ) != MPEG_VIDEO_START_CODE_SIZE - 1 )
        return -1;
    while( 1 )
    {
        if( !fread( &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1, 1, info->input ) )
            break;
        if( !mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
        {
            if( !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SEC ) )
                break;
            else if( !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SHC )
                  || !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_PSC ) )
            {
                fseeko( info->input, -(MPEG_VIDEO_START_CODE_SIZE), SEEK_CUR );
                break;
            }
        }
        for( int i = 1; i < MPEG_VIDEO_START_CODE_SIZE; ++i )
            mpeg_video_head_data[i - 1] = mpeg_video_head_data[i];
    }
    return ftello( info->input );
}

static mpeg_stream_type get_sample_stream_type( void *ih, mpeg_sample_type sample_type )
{
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info )
        return STREAM_INVAILED;
    /* check stream type. */
    mpeg_stream_type stream_type = STREAM_INVAILED;
    if( sample_type == SAMPLE_TYPE_VIDEO )
        stream_type = info->video_stream_type;
    return stream_type;
}

static int get_sample_data( void *ih, mpeg_sample_type sample_type, int64_t position, uint32_t sample_size, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode )
{
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    if( sample_type != SAMPLE_TYPE_VIDEO )
        return -1;
    /* seek reading start position. */
    fseeko( info->input, position, SEEK_SET );
    /* allocate buffer. */
    uint8_t *buffer = malloc( sample_size );
    if( !buffer )
        return -1;
    dprintf( LOG_LV3, "[debug] buffer_size:%d\n", sample_size );
    /* get data. */
    uint32_t read_size = fread( buffer, 1, sample_size, info->input );
    dprintf( LOG_LV3, "[debug] read_size:%d\n", read_size );
    *dst_buffer    = buffer;
    *dst_read_size = read_size;
    return 0;
}

static int64_t get_sample_position( void *ih )
{
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info )
        return -1;
    return ftello( info->input );
}

static int set_sample_position( void *ih, int64_t position )
{
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    fseeko( info->input, position, SEEK_SET );
    return 0;
}

static int seek_next_sample_position( void *ih, mpeg_sample_type sample_type )
{
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info )
        return -1;
    if( sample_type != SAMPLE_TYPE_VIDEO )
        return -1;
    int64_t seek_position = info->video_position;
    if( seek_position < 0 )
        return -1;
    fseeko( info->input, seek_position, SEEK_SET );
    return 0;
}

static int64_t get_pcr( void *ih )
{
    return -1;
}

static int get_video_info( void *ih, video_sample_info_t *video_sample_info )
{
    dprintf( LOG_LV2, "[mpeges_parser] get_video_info()\n" );
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info )
        return -1;
    /* parse data. */
    mpeg_video_info_t video_info;
    //memset( &video_info, 0, sizeof(mpeg_video_info_t) );
    if( mpeges_get_mpeg_video_picture_info( info, &video_info ) )
        return -1;
    int64_t gop_number           = info->gop_number;
    uint8_t progressive_sequence = video_info.sequence_ext.progressive_sequence;
    uint8_t closed_gop           = video_info.gop.closed_gop;
    uint8_t picture_coding_type  = video_info.picture.picture_coding_type;
    int16_t temporal_reference   = video_info.picture.temporal_reference;
    uint8_t picture_structure    = video_info.picture_coding_ext.picture_structure;
    uint8_t progressive_frame    = video_info.picture_coding_ext.progressive_frame;
    uint8_t repeat_first_field   = video_info.picture_coding_ext.repeat_first_field;
    uint8_t top_field_first      = video_info.picture_coding_ext.top_field_first;
    /* search next start position. */
    int64_t read_last_position = mpeges_seek_next_start_position( info );
    /* get timestamp. */
    int64_t pts = -1, dts = -1;
    mpeges_get_stream_timestamp( info, &video_info, &pts, &dts );
    /* setup. */
    video_sample_info->file_position        = info->read_position;
    video_sample_info->sample_size          = read_last_position - info->read_position;
    video_sample_info->pts                  = pts;
    video_sample_info->dts                  = dts;
    video_sample_info->gop_number           = gop_number;
    video_sample_info->progressive_sequence = progressive_sequence;
    video_sample_info->closed_gop           = closed_gop;
    video_sample_info->picture_coding_type  = picture_coding_type;
    video_sample_info->temporal_reference   = temporal_reference;
    video_sample_info->picture_structure    = picture_structure;
    video_sample_info->progressive_frame    = progressive_frame;
    video_sample_info->repeat_first_field   = repeat_first_field;
    video_sample_info->top_field_first      = top_field_first;
    static const char frame[4] = { '?', 'I', 'P', 'B' };
    dprintf( LOG_LV2, "[check] Video PTS:%"PRId64" [%"PRId64"ms], [%c] temporal_reference:%d\n"
                    , pts, pts / 90, frame[picture_coding_type], temporal_reference );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", info->read_position );
    /* ready next. */
    info->video_position = read_last_position;
    return 0;
}

static int get_audio_info( void *ih, audio_sample_info_t *audio_sample_info )
{
    return -1;
}

static int set_program_id( void *ih, mpegts_select_pid_type pid_type, uint16_t program_id )
{
    return -1;
}

static uint16_t get_program_id( void *ih, mpeg_stream_type stream_type )
{
    return -1;
}

static int parse( void *ih )
{
    dprintf( LOG_LV2, "[mpeges_parser] parse()\n" );
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info || !info->input )
        return -1;
    if( mpeges_parse_stream_type( info ) )
        return -1;
    info->status = PARSER_STATUS_PARSED;
    return 0;
}

static void *initialize( const char *mpeges )
{
    dprintf( LOG_LV2, "[mpeges_parser] initialize()\n" );
    mpeges_info_t *info = calloc( sizeof(mpeges_info_t), 1 );
    if( !info )
        return NULL;
    FILE *input = fopen( mpeges, "rb" );
    if( !input )
        goto fail_initialize;
    /* initialize. */
    info->input             = input;
    info->read_position     = -1;
    info->gop_number        = -1;
    info->video_stream_type = STREAM_INVAILED;
    info->fps_numerator     = NTSC_FRAME_RATE_NUM;
    info->fps_denominator   = NTSC_FRAME_RATE_DEN;
    info->timestamp_base    = 90 * 1000 * info->fps_denominator / info->fps_numerator;
    /* first check. */
    if( mpeges_first_check( info ) )
        goto fail_initialize;
    return info;
fail_initialize:
    dprintf( LOG_LV2, "[mpeges_parser] failed initialize.\n" );
    if( input )
        fclose( input );
    if( info )
        free( info );
    return NULL;
}

static void release( void *ih )
{
    dprintf( LOG_LV2, "[mpeges_parser] release()\n" );
    mpeges_info_t *info = (mpeges_info_t *)ih;
    if( !info )
        return;
    /*  release. */
    if( info->input )
        fclose( info->input );
    free( info );
}

mpeg_parser_t mpeges_parser = {
    initialize,
    release,
    parse,
    set_program_id,
    get_program_id,
    get_video_info,
    get_audio_info,
    get_pcr,
    get_sample_position,
    set_sample_position,
    seek_next_sample_position,
    get_sample_data,
    get_sample_stream_type
};
