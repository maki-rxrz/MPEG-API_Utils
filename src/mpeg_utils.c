/*****************************************************************************
 * mpeg_utils.c
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

#include "mpeg_parser.h"
#include "mpeg_utils.h"

typedef struct {
    uint8_t                 progressive_sequence;
    uint8_t                 closed_gop;
} gop_list_data_t;

typedef struct {
    int64_t                 pts;
    int64_t                 dts;
} timestamp_t;

typedef struct {
    int64_t                 file_position;
    uint32_t                sample_size;
    int64_t                 gop_number;
    timestamp_t             timestamp;
    uint8_t                 picture_coding_type;
    int16_t                 temporal_reference;
    uint8_t                 picture_structure;
    uint8_t                 progressive_frame;
    uint8_t                 repeat_first_field;
    uint8_t                 top_field_first;
} sample_list_data_t;

typedef struct {
    gop_list_data_t        *video_gop;
    int64_t                 video_gop_num;
    sample_list_data_t     *video;
    int64_t                 video_num;
    sample_list_data_t     *audio;
    int64_t                 audio_num;
} sample_list_t;

typedef struct {
    mpeg_parser_t          *parser;
    void                   *parser_info;
    sample_list_t           sample_list;
    int64_t                 wrap_around_check_v;
} mpeg_api_info_t;

#define DEFAULT_GOP_SAMPLE_NUM              (40000)
#define DEFAULT_VIDEO_SAMPLE_NUM            (50000)
#define DEFAULT_AUDIO_SAMPLE_NUM            (80000)

#define TS_TIMESTAMP_WRAP_AROUND_CHECK_VALUE        (0x0FFFFFFFFLL)

extern int mpeg_api_create_sample_list( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser = info->parser;
    void *parser_info = info->parser_info;
    memset( &(info->sample_list), 0, sizeof(sample_list_t) );
    int64_t start_position = parser->get_sample_position( parser_info );
    /* allocate lists. */
    int64_t gop_list_size   = DEFAULT_GOP_SAMPLE_NUM;
    int64_t video_list_size = DEFAULT_VIDEO_SAMPLE_NUM;
    int64_t audio_list_size = DEFAULT_AUDIO_SAMPLE_NUM;
    gop_list_data_t    *gop_list   = malloc( sizeof(gop_list_data_t)    * gop_list_size   );
    sample_list_data_t *video_list = malloc( sizeof(sample_list_data_t) * video_list_size );
    sample_list_data_t *audio_list = malloc( sizeof(sample_list_data_t) * audio_list_size );
    if( !gop_list || !video_list || !audio_list )
        goto fail_create_list;
    /* create video lists. */
    uint32_t wrap_around_count = 0;
    int64_t comapre_ts = 0;
    int64_t i;
    int64_t gop_number = -1;
    for( i = 0; ; ++i )
    {
        if( i >= video_list_size )
        {
            video_list_size += DEFAULT_VIDEO_SAMPLE_NUM;
            sample_list_data_t *tmp = realloc( video_list, sizeof(sample_list_data_t) * video_list_size );
            if( !tmp )
                goto fail_create_list;
            video_list = tmp;
        }
        parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_VIDEO );
        video_sample_info_t video_sample_info;
        int result = parser->get_video_info( parser_info, &video_sample_info );
        if( result )
            break;
        /* setup GOP list. */
        if( video_sample_info.gop_number < 0 )
        {
            i = -1;
            continue;
        }
        if( gop_number < video_sample_info.gop_number )
        {
            gop_number = video_sample_info.gop_number;

            if( gop_number >= gop_list_size )
            {
                gop_list_size += DEFAULT_GOP_SAMPLE_NUM;
                gop_list_data_t *tmp = realloc( gop_list, sizeof(gop_list_data_t) * gop_list_size );
                if( !tmp )
                    goto fail_create_list;
                gop_list = tmp;
            }
            gop_list[gop_number].progressive_sequence = video_sample_info.progressive_sequence;
            gop_list[gop_number].closed_gop           = video_sample_info.closed_gop;
            /* correct check. */
            if( comapre_ts > video_sample_info.pts + info->wrap_around_check_v )
                ++ wrap_around_count;
            comapre_ts = video_sample_info.pts;
        }
        /* setup. */
        video_list[i].file_position       = video_sample_info.file_position;
        video_list[i].sample_size         = video_sample_info.sample_size;
        video_list[i].gop_number          = video_sample_info.gop_number;
        video_list[i].timestamp.pts       = video_sample_info.pts + (wrap_around_count + (comapre_ts > video_sample_info.pts + info->wrap_around_check_v) ? 1 : 0 ) * MPEG_TIMESTAMP_MAX_VALUE;
        video_list[i].timestamp.dts       = video_sample_info.dts + (wrap_around_count + (comapre_ts > video_sample_info.dts + info->wrap_around_check_v) ? 1 : 0 ) * MPEG_TIMESTAMP_MAX_VALUE;
        video_list[i].picture_coding_type = video_sample_info.picture_coding_type;
        video_list[i].temporal_reference  = video_sample_info.temporal_reference;
        video_list[i].progressive_frame   = video_sample_info.progressive_frame;
        video_list[i].picture_structure   = video_sample_info.picture_structure;
        video_list[i].repeat_first_field  = video_sample_info.repeat_first_field;
        video_list[i].top_field_first     = video_sample_info.top_field_first;
    }
    if( i > 0 )
    {
        info->sample_list.video_gop     = gop_list;
        info->sample_list.video_gop_num = gop_number + 1;
        info->sample_list.video         = video_list;
        info->sample_list.video_num     = i;
    }
    else
    {
        free( gop_list );
        free( video_list );
    }
    parser->set_sample_position( parser_info, start_position );
    /* create audio sample list. */
    comapre_ts = 0;
    wrap_around_count = 0;
    for( i = 0; ; ++i )
    {
        if( i >= audio_list_size )
        {
            audio_list_size += DEFAULT_AUDIO_SAMPLE_NUM;
            sample_list_data_t *tmp = realloc( audio_list, sizeof(sample_list_data_t) * audio_list_size );
            if( !tmp )
                goto fail_create_list;
            audio_list = tmp;
        }
        parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_AUDIO );
        audio_sample_info_t audio_sample_info;
        int result = parser->get_audio_info( parser_info, &audio_sample_info );
        if( result )
            break;
        /* correct check. */
        if( comapre_ts > audio_sample_info.pts + info->wrap_around_check_v )
            ++ wrap_around_count;
        comapre_ts = audio_sample_info.pts;
        /* setup. */
        audio_list[i].file_position       = audio_sample_info.file_position;
        audio_list[i].sample_size         = audio_sample_info.sample_size;
        audio_list[i].gop_number          = 0;
        audio_list[i].timestamp.pts       = audio_sample_info.pts + wrap_around_count * MPEG_TIMESTAMP_MAX_VALUE;
        audio_list[i].timestamp.dts       = audio_sample_info.dts + wrap_around_count * MPEG_TIMESTAMP_MAX_VALUE;
        audio_list[i].picture_coding_type = 0;
        audio_list[i].temporal_reference  = 0;
        audio_list[i].picture_structure   = 0;
        audio_list[i].progressive_frame   = 0;
    }
    if( i > 0 )
    {
        info->sample_list.audio     = audio_list;
        info->sample_list.audio_num = i;
    }
    else
        free( audio_list );
    parser->set_sample_position( parser_info, start_position );
    /* last check. */
    if( !info->sample_list.video && !info->sample_list.audio )
        return -1;
    return 0;
fail_create_list:
    if( gop_list )
        free( gop_list );
    if( video_list )
        free( video_list );
    if( audio_list )
        free( audio_list );
    memset( &(info->sample_list), 0, sizeof(sample_list_t) );
    parser->set_sample_position( parser_info, start_position );
    return -1;
}

extern mpeg_stream_type mpeg_api_get_sample_stream_type( void *ih, mpeg_sample_type sample_type )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_sample_stream_type( info->parser_info, sample_type );
}

extern uint32_t mpeg_api_get_sample_num( void *ih, mpeg_sample_type sample_type )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return 0;
    uint32_t sample_number = 0;
    if( sample_type == SAMPLE_TYPE_VIDEO )
        sample_number = info->sample_list.video_num;
    else if( sample_type == SAMPLE_TYPE_AUDIO )
        sample_number = info->sample_list.audio_num;
    return sample_number;
}

extern int mpeg_api_get_sample_info( void *ih, mpeg_sample_type sample_type, uint32_t sample_number, stream_info_t *stream_info )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info || !stream_info )
        return -1;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        sample_list_data_t *list = info->sample_list.video;
        int64_t list_num         = info->sample_list.video_num;
        if( !list || sample_number >= list_num )
            return -1;
        gop_list_data_t *gop = &(info->sample_list.video_gop[list[sample_number].gop_number]);
        stream_info->file_position        = list[sample_number].file_position;
        stream_info->sample_size          = list[sample_number].sample_size;
        stream_info->video_pts            = list[sample_number].timestamp.pts;
        stream_info->video_dts            = list[sample_number].timestamp.dts;
        stream_info->gop_number           = list[sample_number].gop_number;
        stream_info->progressive_sequence = gop->progressive_sequence;
        stream_info->closed_gop           = gop->closed_gop;
        stream_info->picture_coding_type  = list[sample_number].picture_coding_type;
        stream_info->temporal_reference   = list[sample_number].temporal_reference;
        stream_info->picture_structure    = list[sample_number].picture_structure;
        stream_info->progressive_frame    = list[sample_number].progressive_frame;
        stream_info->repeat_first_field   = list[sample_number].repeat_first_field;
        stream_info->top_field_first      = list[sample_number].top_field_first;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        sample_list_data_t *list = info->sample_list.audio;
        int64_t list_num         = info->sample_list.audio_num;
        if( !list || sample_number >= list_num )
            return -1;
        stream_info->file_position = list[sample_number].file_position;
        stream_info->sample_size   = list[sample_number].sample_size;
        stream_info->audio_pts     = list[sample_number].timestamp.pts;
        stream_info->audio_dts     = list[sample_number].timestamp.dts;
    }
    else
        return -1;
    return 0;
}

extern int mpeg_api_get_sample_data( void *ih, mpeg_sample_type sample_type, uint32_t sample_number, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info || !dst_buffer || !dst_read_size )
        return -1;
    mpeg_parser_t *parser = info->parser;
    void *parser_info = info->parser_info;
    sample_list_data_t *list;
    int64_t list_num;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        list     = info->sample_list.video;
        list_num = info->sample_list.video_num;
        if( !list || sample_number >= list_num )
            return -1;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        list     = info->sample_list.audio;
        list_num = info->sample_list.audio_num;
        if( !list || sample_number >= list_num )
            return -1;
        if( !sample_number && get_mode == GET_SAMPLE_DATA_RAW )
            get_mode = GET_SAMPLE_DATA_RAW_SEARCH_HEAD;
    }
    else
        return -1;
    /* get sample data. */
    int64_t file_position = list[sample_number].file_position;
    uint32_t sample_size  = list[sample_number].sample_size;
    return parser->get_sample_data( parser_info, sample_type, file_position, sample_size, dst_buffer, dst_read_size, get_mode );
}

extern int64_t mpeg_api_get_pcr( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_pcr( info->parser_info );
}

extern int mpeg_api_get_video_frame( void *ih, stream_info_t *stream_info )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser = info->parser;
    void *parser_info = info->parser_info;
    parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_VIDEO );
    /* get video. */
    video_sample_info_t video_sample_info;
    int result = parser->get_video_info( parser_info, &video_sample_info );
    if( result )
        return -1;
    stream_info->file_position        = video_sample_info.file_position;
    stream_info->sample_size          = video_sample_info.sample_size;
    stream_info->video_pts            = video_sample_info.pts;
    stream_info->video_dts            = video_sample_info.dts;
    stream_info->gop_number           = video_sample_info.gop_number;
    stream_info->progressive_sequence = video_sample_info.progressive_sequence;
    stream_info->closed_gop           = video_sample_info.closed_gop;
    stream_info->picture_coding_type  = video_sample_info.picture_coding_type;
    stream_info->temporal_reference   = video_sample_info.temporal_reference;
    stream_info->picture_structure    = video_sample_info.picture_structure;
    stream_info->progressive_frame    = video_sample_info.progressive_frame;
    stream_info->repeat_first_field   = video_sample_info.repeat_first_field;
    stream_info->top_field_first      = video_sample_info.top_field_first;
    return 0;
}

extern int mpeg_api_get_audio_frame( void *ih, stream_info_t *stream_info )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser = info->parser;
    void *parser_info = info->parser_info;
    /* get audio. */
    parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_AUDIO );
    audio_sample_info_t audio_sample_info;
    int result = parser->get_audio_info( parser_info, &audio_sample_info );
    if( result )
        return -1;
    stream_info->file_position = audio_sample_info.file_position;
    stream_info->sample_size   = audio_sample_info.sample_size;
    stream_info->audio_pts     = audio_sample_info.pts;
    stream_info->audio_dts     = audio_sample_info.dts;
    return 0;
}

extern int mpeg_api_parse( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->parse( info->parser_info );
}

extern int mpeg_api_get_stream_info( void *ih, stream_info_t *stream_info, int64_t *video_1st_pts, int64_t*video_key_pts )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser = info->parser;
    void *parser_info = info->parser_info;
    int64_t start_position = parser->get_sample_position( parser_info );
    /* parse. */
    if( parser->parse( parser_info ) )
        return -1;
    parser->set_sample_position( parser_info, start_position );
    /* check video and audio PTS. */
    enum {
        BOTH_VA_NONE  = 0x11,
        VIDEO_NONE    = 0x10,
        AUDIO_NONE    = 0x01,
        BOTH_VA_EXIST = 0x00
    };
    int check_stream_exist = BOTH_VA_EXIST;
    /* get video. */
    video_sample_info_t video_sample_info;
    check_stream_exist |= parser->get_video_info( parser_info, &video_sample_info ) ? VIDEO_NONE : 0;
    if( !(check_stream_exist & VIDEO_NONE) )
    {
        *video_1st_pts = video_sample_info.pts;
        *video_key_pts = (video_sample_info.picture_coding_type == MPEG_VIDEO_I_FRAME) ? video_sample_info.pts : -1;
        while( video_sample_info.temporal_reference || *video_key_pts < 0 )
        {
            if( parser->get_video_info( parser_info, &video_sample_info ) )
                break;
            if( *video_1st_pts > video_sample_info.pts && *video_1st_pts < video_sample_info.pts + info->wrap_around_check_v )
                *video_1st_pts = video_sample_info.pts;
            if( *video_key_pts < 0 && video_sample_info.picture_coding_type == MPEG_VIDEO_I_FRAME )
                *video_key_pts = video_sample_info.pts;
        }
    }
    parser->set_sample_position( parser_info, start_position );
    /* get audio. */
    audio_sample_info_t audio_sample_info;
    check_stream_exist |= parser->get_audio_info( parser_info, &audio_sample_info ) ? AUDIO_NONE : 0;
    parser->set_sample_position( parser_info, start_position );
    /* setup. */
    stream_info->pcr           = parser->get_pcr( info->parser_info );
    stream_info->video_pts     = video_sample_info.pts;
    stream_info->audio_pts     = audio_sample_info.pts;
    return (check_stream_exist == BOTH_VA_NONE);
}

extern int mpeg_api_set_pmt_program_id( void *ih, uint16_t pmt_program_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    info->parser->set_program_id( info->parser_info, PID_TYPE_PMT, pmt_program_id );
    return 0;
}

extern void *mpeg_api_initialize_info( const char *mpeg )
{
    mpeg_api_info_t *info = malloc( sizeof(mpeg_api_info_t) );
    if( !info )
        return NULL;
    mpeg_parser_t *parser;
    void *parser_info;
    static mpeg_parser_t *parsers[MPEG_PARSER_NUM] =
        {
            &mpegts_parser,
            &mpeges_parser
        };
    for( int i = 0; i < MPEG_PARSER_NUM; ++i )
    {
        parser = parsers[i];
        parser_info = parser->initialize( mpeg );
        if( parser_info )
            break;
    }
    if( !parser_info )
        goto fail_initialize;
    memset( info, 0, sizeof(mpeg_api_info_t) );
    info->parser              = parser;
    info->parser_info         = parser_info;
    info->wrap_around_check_v = TS_TIMESTAMP_WRAP_AROUND_CHECK_VALUE;
    return info;
fail_initialize:
    if( parser_info )
        free( parser_info );
    if( info )
        free( info );
    return NULL;
}

extern void mpeg_api_release_info( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info )
        return;
    if( info->parser_info )
        info->parser->release( info->parser_info );
    if( info->sample_list.video_gop )
        free( info->sample_list.video_gop );
    if( info->sample_list.video )
        free( info->sample_list.video );
    if( info->sample_list.audio )
        free( info->sample_list.audio );
    free( info );
}
