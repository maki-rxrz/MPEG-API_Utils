/*****************************************************************************
 * mpeg_utils.c
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
#include <inttypes.h>

#include "mpeg_parser.h"
#include "mpeg_utils.h"
#include "thread_utils.h"
#include "file_utils.h"

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
    uint32_t                raw_data_size;
    int32_t                 raw_data_read_offset;
    uint32_t                au_size;
    uint16_t                program_id;
    /* video. */
    int64_t                 gop_number;
    timestamp_t             timestamp;
    uint8_t                 picture_coding_type;
    int16_t                 temporal_reference;
    uint8_t                 picture_structure;
    uint8_t                 progressive_frame;
    uint8_t                 repeat_first_field;
    uint8_t                 top_field_first;
    /* audio. */
    uint32_t                sampling_frequency;
    uint32_t                bitrate;
    uint16_t                channel;
    uint8_t                 layer;
    uint8_t                 bit_depth;
} sample_list_data_t;

typedef struct {
    gop_list_data_t        *video_gop;
    int64_t                 video_gop_num;
    sample_list_data_t     *video;
    int64_t                 video_num;
} video_stream_data_t;

typedef struct {
    sample_list_data_t     *audio;
    int64_t                 audio_num;
} audio_stream_data_t;

typedef struct {
    video_stream_data_t    *video_stream;
    int8_t                  video_stream_num;
    audio_stream_data_t    *audio_stream;
    int8_t                  audio_stream_num;
} sample_list_t;

typedef struct {
    mpeg_parser_t          *parser;
    void                   *parser_info;
    sample_list_t           sample_list;
    int64_t                 wrap_around_check_v;
    int64_t                 file_size;
} mpeg_api_info_t;

#define DEFAULT_GOP_SAMPLE_NUM              (40000)
#define DEFAULT_VIDEO_SAMPLE_NUM            (50000)
#define DEFAULT_AUDIO_SAMPLE_NUM            (80000)

#define TIMESTAMP_WRAP_AROUND_CHECK_VALUE       (0x0FFFFFFFFLL)

typedef struct {
    mpeg_api_info_t  *api_info;
    mpeg_sample_type  sample_type;
    uint8_t           stream_number;
    void             *list_data;
    uint16_t          thread_index;
    uint16_t          thread_num;
    int64_t          *progress;
} parse_param_t;

static void parse_progress( parse_param_t *param, int64_t position )
{
    if( param )
    {
        param->progress[param->thread_index] = position;
        uint64_t average = 0;
        for( uint16_t i = 0; i < param->thread_num; ++i )
            average += param->progress[i];
        average /= param->thread_num;
        mapi_log( LOG_LV_PROGRESS, "[parse_stream] %14" PRIu64 "/%-14" PRIu64 "\r", average, param->api_info->file_size );
    }
    else
        mapi_log( LOG_LV_PROGRESS, "[parse_stream] %14" PRIu64 "/%-14" PRIu64 "\n", position, position );
}

static thread_func_ret parse_stream( void *args )
{
    parse_param_t *param = (parse_param_t *)args;
    if( !param )
        return (thread_func_ret)(-1);
    mpeg_api_info_t  *info          = param->api_info;
    mpeg_sample_type  sample_type   = param->sample_type;
    uint8_t           stream_number = param->stream_number;
    void             *list_data     = param->list_data;
    mpeg_parser_t    *parser        = info->parser;
    void             *parser_info   = info->parser_info;
    /* parse */
    gop_list_data_t    *gop_list   = NULL;
    sample_list_data_t *video_list = NULL;
    sample_list_data_t *audio_list = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        video_stream_data_t *video_stream = (video_stream_data_t *)list_data;
        int64_t gop_list_size   = DEFAULT_GOP_SAMPLE_NUM;
        int64_t video_list_size = DEFAULT_VIDEO_SAMPLE_NUM;
        gop_list   = (gop_list_data_t    *)malloc( sizeof(gop_list_data_t)    * gop_list_size   );
        video_list = (sample_list_data_t *)malloc( sizeof(sample_list_data_t) * video_list_size );
        if( !gop_list || !video_list )
            goto fail_parse_stream;
        /* create video lists. */
        uint32_t wrap_around_count = 0;
        int64_t  compare_ts        = 0;
        int64_t  gop_number        = -1;
        int64_t i;
        for( i = 0; ; ++i )
        {
            if( i >= video_list_size )
            {
                video_list_size += DEFAULT_VIDEO_SAMPLE_NUM;
                sample_list_data_t *tmp = (sample_list_data_t *)realloc( video_list
                                                                       , sizeof(sample_list_data_t) * video_list_size );
                if( !tmp )
                    goto fail_parse_stream;
                video_list = tmp;
            }
            parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_VIDEO, stream_number );
            video_sample_info_t video_sample_info;
            if( parser->get_video_info( parser_info, stream_number, &video_sample_info ) )
                break;
            /* setup GOP list. */
            if( gop_number < video_sample_info.gop_number )
            {
                gop_number = video_sample_info.gop_number;
                if( gop_number >= gop_list_size )
                {
                    gop_list_size += DEFAULT_GOP_SAMPLE_NUM;
                    gop_list_data_t *tmp = (gop_list_data_t *)realloc( gop_list, sizeof(gop_list_data_t) * gop_list_size );
                    if( !tmp )
                        goto fail_parse_stream;
                    gop_list = tmp;
                }
                gop_list[gop_number].progressive_sequence = video_sample_info.progressive_sequence;
                gop_list[gop_number].closed_gop           = video_sample_info.closed_gop;
                /* correct check. */
                if( compare_ts > video_sample_info.pts + info->wrap_around_check_v )
                    ++wrap_around_count;
                compare_ts = video_sample_info.pts;
            }
#define CALCLATE_CORRECTION_TIMESTAMP( _timestamp )     \
( _timestamp + (wrap_around_count + ((compare_ts > _timestamp + info->wrap_around_check_v) ? 1 : 0)) * MPEG_TIMESTAMP_WRAPAROUND_VALUE )
            /* setup. */
            video_list[i].file_position        = video_sample_info.file_position;
            video_list[i].sample_size          = video_sample_info.sample_size;
            video_list[i].raw_data_size        = video_sample_info.raw_data_size;
            video_list[i].raw_data_read_offset = video_sample_info.raw_data_read_offset;
            video_list[i].au_size              = video_sample_info.au_size;
            video_list[i].program_id           = video_sample_info.program_id;
            video_list[i].gop_number           = video_sample_info.gop_number;
            video_list[i].timestamp.pts        = CALCLATE_CORRECTION_TIMESTAMP( video_sample_info.pts );
            video_list[i].timestamp.dts        = CALCLATE_CORRECTION_TIMESTAMP( video_sample_info.dts );
            video_list[i].picture_coding_type  = video_sample_info.picture_coding_type;
            video_list[i].temporal_reference   = video_sample_info.temporal_reference;
            video_list[i].progressive_frame    = video_sample_info.progressive_frame;
            video_list[i].picture_structure    = video_sample_info.picture_structure;
            video_list[i].repeat_first_field   = video_sample_info.repeat_first_field;
            video_list[i].top_field_first      = video_sample_info.top_field_first;
            video_list[i].sampling_frequency   = 0;
            video_list[i].bitrate              = 0;
            video_list[i].channel              = 0;
            video_list[i].layer                = 0;
            video_list[i].bit_depth            = 0;
#undef CALCLATE_CORRECTION_TIMESTAMP
            /* progress. */
            parse_progress( param, video_sample_info.file_position );
        }
        if( i > 0 )
        {
            /* correct check for no GOP picture. */
            int16_t temporal_reference = (int16_t)((1 << 15) - 1);
            compare_ts = 0;
            for( uint32_t j = 0; j < i; ++j )
            {
                if( video_list[j].gop_number >= 0 )
                    break;
                if( video_list[j].temporal_reference < temporal_reference )
                {
                    compare_ts         = video_list[j].timestamp.pts;
                    temporal_reference = video_list[j].temporal_reference;
                }
            }
            if( compare_ts )
            {
                for( uint32_t j = 0; j < i; ++j )
                {
                    if( video_list[j].gop_number >= 0 )
                        break;
#define CHECK_CORRECTION_TIME_VALUE( _timestamp )     \
( (compare_ts > _timestamp + info->wrap_around_check_v) ? MPEG_TIMESTAMP_WRAPAROUND_VALUE : 0 )
                    video_list[j].timestamp.pts += CHECK_CORRECTION_TIME_VALUE( video_list[j].timestamp.pts );
                    video_list[j].timestamp.dts += CHECK_CORRECTION_TIME_VALUE( video_list[j].timestamp.dts );
#undef CHECK_CORRECTION_TIME_VALUE
                }
            }
            /* setup video sample list. */
            video_stream->video_gop     = gop_list;
            video_stream->video_gop_num = gop_number + 1;
            video_stream->video         = video_list;
            video_stream->video_num     = i;
        }
        else
        {
            free( gop_list );
            free( video_list );
        }
        gop_list   = NULL;
        video_list = NULL;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        audio_stream_data_t *audio_stream = (audio_stream_data_t *)list_data;
        int64_t audio_list_size = DEFAULT_AUDIO_SAMPLE_NUM;
        audio_list = (sample_list_data_t *)malloc( sizeof(sample_list_data_t) * audio_list_size );
        if( !audio_list )
            goto fail_parse_stream;
        /* create audio sample list. */
        uint32_t wrap_around_count = 0;
        int64_t  compare_ts        = 0;
        int64_t i;
        for( i = 0; ; ++i )
        {
            if( i >= audio_list_size )
            {
                audio_list_size += DEFAULT_AUDIO_SAMPLE_NUM;
                sample_list_data_t *tmp = (sample_list_data_t *)realloc( audio_list, sizeof(sample_list_data_t) * audio_list_size );
                if( !tmp )
                    goto fail_parse_stream;
                audio_list = tmp;
            }
            parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_AUDIO, stream_number );
            audio_sample_info_t audio_sample_info;
            if( parser->get_audio_info( parser_info, stream_number, &audio_sample_info ) )
                break;
            /* correct check. */
            if( compare_ts > audio_sample_info.pts + info->wrap_around_check_v )
                ++wrap_around_count;
            compare_ts = audio_sample_info.pts;
            /* setup. */
#define CALCLATE_CORRECTION_TIMESTAMP( _timestamp )     \
( _timestamp + wrap_around_count * MPEG_TIMESTAMP_WRAPAROUND_VALUE )
            audio_list[i].file_position        = audio_sample_info.file_position;
            audio_list[i].sample_size          = audio_sample_info.sample_size;
            audio_list[i].raw_data_size        = audio_sample_info.raw_data_size;
            audio_list[i].raw_data_read_offset = audio_sample_info.raw_data_read_offset;
            audio_list[i].au_size              = audio_sample_info.au_size;
            audio_list[i].program_id           = audio_sample_info.program_id;
            audio_list[i].gop_number           = 0;
            audio_list[i].timestamp.pts        = CALCLATE_CORRECTION_TIMESTAMP( audio_sample_info.pts );
            audio_list[i].timestamp.dts        = CALCLATE_CORRECTION_TIMESTAMP( audio_sample_info.dts );
            audio_list[i].picture_coding_type  = 0;
            audio_list[i].temporal_reference   = 0;
            audio_list[i].picture_structure    = 0;
            audio_list[i].progressive_frame    = 0;
            audio_list[i].sampling_frequency   = audio_sample_info.sampling_frequency;
            audio_list[i].bitrate              = audio_sample_info.bitrate;
            audio_list[i].channel              = audio_sample_info.channel;
            audio_list[i].layer                = audio_sample_info.layer;
            audio_list[i].bit_depth            = audio_sample_info.bit_depth;
#undef CALCLATE_CORRECTION_TIMESTAMP
            /* progress. */
            parse_progress( param, audio_sample_info.file_position );
        }
        if( i > 0 )
        {
            /* setup audio sample list. */
            audio_stream->audio     = audio_list;
            audio_stream->audio_num = i;
        }
        else
            free( audio_list );
        audio_list = NULL;
    }
    return (thread_func_ret)(0);
fail_parse_stream:
    if( gop_list )
        free( gop_list );
    if( video_list )
        free( video_list );
    if( audio_list )
        free( audio_list );
    return (thread_func_ret)(-1);
}

MAPI_EXPORT int mpeg_api_create_sample_list( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    /* check stream num. */
    int8_t               video_stream_num = parser->get_stream_num( parser_info, SAMPLE_TYPE_VIDEO, 0 );
    int8_t               audio_stream_num = parser->get_stream_num( parser_info, SAMPLE_TYPE_AUDIO, 0 );
    video_stream_data_t *video_stream     = NULL;
    audio_stream_data_t *audio_stream     = NULL;
    if( video_stream_num )
        video_stream = (video_stream_data_t *)calloc( video_stream_num, sizeof(video_stream_data_t) );
    if( audio_stream_num )
        audio_stream = (audio_stream_data_t *)calloc( audio_stream_num, sizeof(audio_stream_data_t) );
    if( (video_stream_num && !video_stream)
     || (audio_stream_num && !audio_stream) )
        goto fail_create_list;
    /* create lists. */
    uint16_t thread_num = video_stream_num + audio_stream_num;
    parse_param_t *param    = (parse_param_t *)malloc( sizeof(parse_param_t) * thread_num );
    int64_t       *progress = (int64_t       *)calloc( thread_num, sizeof(int64_t) * thread_num );
    if( !param || !progress )
    {
        if( param )
            free( param );
        if( progress )
            free( progress );
        goto fail_create_list;
    }
    else
    {
        void *parse_thread[thread_num];
        memset( parse_thread, 0, sizeof(void *) * thread_num );
        uint16_t thread_index = 0;
        /* video. */
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            param[thread_index].api_info      = info;
            param[thread_index].sample_type   = SAMPLE_TYPE_VIDEO;
            param[thread_index].stream_number = i;
            param[thread_index].list_data     = &(video_stream[i]);
            param[thread_index].thread_index  = thread_index;
            param[thread_index].thread_num    = thread_num;
            param[thread_index].progress      = progress;
            parse_thread[thread_index] = thread_create( parse_stream, &param[thread_index] );
            ++thread_index;
        }
        /* audio. */
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            param[thread_index].api_info      = info;
            param[thread_index].sample_type   = SAMPLE_TYPE_AUDIO;
            param[thread_index].stream_number = i;
            param[thread_index].list_data     = &(audio_stream[i]);
            param[thread_index].thread_index  = thread_index;
            param[thread_index].thread_num    = thread_num;
            param[thread_index].progress      = progress;
            parse_thread[thread_index] = thread_create( parse_stream, &param[thread_index] );
            ++thread_index;
        }
        /* wait parse end. */
        if( thread_index )
        {
            for( uint16_t i = 0; i < thread_index; ++i )
            {
                //mapi_log( LOG_LV_PROGRESS, "[log] wait %s Stream[%3u]...\n"
                //                         , param[i].stream_name, param[i].stream_number );
                thread_wait_end( parse_thread[i], NULL );
            }
        }
        free( param );
        free( progress );
        parse_progress( NULL, info->file_size );
    }
    /* check. */
    if( video_stream )
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( !video_stream[i].video )
                goto fail_create_list;
    if( audio_stream )
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( !audio_stream[i].audio )
                goto fail_create_list;
    /* setup. */
    info->sample_list.video_stream     = video_stream;
    info->sample_list.audio_stream     = audio_stream;
    info->sample_list.video_stream_num = video_stream_num;
    info->sample_list.audio_stream_num = audio_stream_num;
    return 0;
fail_create_list:
    if( video_stream )
    {
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( video_stream[i].video_gop )
                free( video_stream[i].video_gop );
            if( video_stream[i].video )
                free( video_stream[i].video );
        }
        free( video_stream );
    }
    if( audio_stream )
    {
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( audio_stream[i].audio )
                free( audio_stream[i].audio );
        free( audio_stream );
    }
    memset( &(info->sample_list), 0, sizeof(sample_list_t) );
    return -1;
}

MAPI_EXPORT int64_t mpeg_api_get_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_sample_position( info->parser_info, sample_type, stream_number );
}

MAPI_EXPORT int mpeg_api_set_sample_position
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    int64_t                     position
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->set_sample_position( info->parser_info, sample_type, stream_number, position );
}

MAPI_EXPORT int mpeg_api_get_all_stream_data
(
    void                       *ih,
    get_sample_data_mode        get_mode,
    output_stream_type          output_stream,
    int                         update_psi,
    get_stream_data_cb_t       *cb
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    /* set start position. */
    if( get_mode == GET_SAMPLE_DATA_RAW )
    {
        uint8_t video_stream_num = parser->get_stream_num( parser_info, SAMPLE_TYPE_VIDEO, 0 );
        uint8_t audio_stream_num = parser->get_stream_num( parser_info, SAMPLE_TYPE_AUDIO, 0 );
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            video_sample_info_t video_sample_info;
            if( parser->get_video_info( parser_info, i, &video_sample_info ) )
                continue;
            parser->set_sample_position( parser_info, SAMPLE_TYPE_VIDEO, i, video_sample_info.file_position );
        }
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            audio_sample_info_t audio_sample_info;
            if( parser->get_audio_info( parser_info, i, &audio_sample_info ) )
                continue;
            parser->set_sample_position( parser_info, SAMPLE_TYPE_AUDIO, i, audio_sample_info.file_position );
        }
    }
    /* get data. */
    while( 1 )
        if( parser->get_specific_stream_data( parser_info, get_mode, output_stream, update_psi, cb ) )
            break;
    return 0;
}

MAPI_EXPORT int mpeg_api_get_stream_all
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    get_sample_data_mode        get_mode,
    get_stream_data_cb_t       *cb
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    int32_t        read_offset = 0;
    /* check offset. */
    if( get_mode == GET_SAMPLE_DATA_RAW )
    {
        int64_t file_position = 0;
        if( sample_type == SAMPLE_TYPE_VIDEO )
        {
            video_sample_info_t video_sample_info;
            if( parser->get_video_info( parser_info, stream_number, &video_sample_info ) )
                return -1;
            file_position = video_sample_info.file_position;
            read_offset   = video_sample_info.raw_data_read_offset;
        }
        if( sample_type == SAMPLE_TYPE_AUDIO )
        {
            audio_sample_info_t audio_sample_info;
            if( parser->get_audio_info( parser_info, stream_number, &audio_sample_info ) )
                return -1;
            file_position = audio_sample_info.file_position;
            read_offset   = audio_sample_info.raw_data_read_offset;
        }
        parser->set_sample_position( parser_info, sample_type, stream_number, file_position );
    }
    /* get data. */
    while( 1 )
        if( (read_offset = parser->get_stream_data( parser_info, sample_type, stream_number, read_offset, get_mode, cb )) )
            break;
    return 0;
}

MAPI_EXPORT int mpeg_api_get_stream_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint8_t                   **dst_buffer,
    uint32_t                   *dst_read_size,
    get_sample_data_mode        get_mode
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    parser->seek_next_sample_position( parser_info, sample_type, stream_number );
    /* get sample data. */
    int64_t  file_position = -1;
    uint32_t sample_size   = 0;
    int32_t  read_offset   = 0;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        /* get video. */
        video_sample_info_t video_sample_info;
        if( parser->get_video_info( parser_info, stream_number, &video_sample_info ) )
            return -1;
        file_position   = video_sample_info.file_position;
        sample_size     = video_sample_info.sample_size;
        if( get_mode == GET_SAMPLE_DATA_RAW )
        {
            sample_size = video_sample_info.raw_data_size;
            read_offset = video_sample_info.raw_data_read_offset;
        }
    }
    if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        /* get audio. */
        audio_sample_info_t audio_sample_info;
        if( parser->get_audio_info( parser_info, stream_number, &audio_sample_info ) )
            return -1;
        file_position   = audio_sample_info.file_position;
        sample_size     = audio_sample_info.sample_size;
        if( get_mode == GET_SAMPLE_DATA_RAW )
        {
            sample_size = audio_sample_info.raw_data_size;
            read_offset = audio_sample_info.raw_data_read_offset;
        }
    }
    if( !sample_size )
        return -1;
    int64_t reset_position = parser->get_sample_position( parser_info, sample_type, stream_number );
    int     result         = parser->get_sample_data( parser_info, sample_type, stream_number
                                                    , file_position, sample_size, read_offset
                                                    , dst_buffer, dst_read_size, get_mode );
    parser->set_sample_position( parser_info, sample_type, stream_number, reset_position );
    return result;
}

MAPI_EXPORT uint8_t mpeg_api_get_stream_num( void *ih, mpeg_sample_type sample_type, uint16_t service_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return 0;
    return info->parser->get_stream_num( info->parser_info, sample_type, service_id );
}

MAPI_EXPORT const char *mpeg_api_get_stream_information
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    get_information_key_type    key
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return NULL;
    return info->parser->get_stream_information( info->parser_info, sample_type, stream_number, key );
}

MAPI_EXPORT const char *mpeg_api_get_sample_file_extension
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return NULL;
    if( sample_type != SAMPLE_TYPE_VIDEO && sample_type != SAMPLE_TYPE_AUDIO )
        return NULL;
    static const char *raw_ext[2][7] =
        {
            { NULL, ".m1v", ".m2v", ".mpv", ".avc", ".hevc", ".vc1" },
            { NULL, ".mpa", ".aac", ".pcm", ".ac3", ".eac3", ".dts" }
        };
    int idx = 0;
    mpeg_stream_type stream_type = info->parser->get_sample_stream_type( info->parser_info, sample_type, stream_number );
    switch( stream_type )
    {
        /* Video Stream */
        case STREAM_VIDEO_MPEG1 :
            idx = 1;
            break;
        case STREAM_VIDEO_MPEG2 :
        case STREAM_VIDEO_MPEG2_A :
        case STREAM_VIDEO_MPEG2_B :
        case STREAM_VIDEO_MPEG2_C :
        case STREAM_VIDEO_MPEG2_D :
            idx = 2;
            break;
        case STREAM_VIDEO_AVC :
            idx = 4;
            break;
        case STREAM_VIDEO_HEVC :
            idx = 5;
            break;
        case STREAM_VIDEO_VC1 :
            idx = 6;
            break;
        /* Audio Stream */
        case STREAM_AUDIO_MP1 :
        case STREAM_AUDIO_MP2 :
            idx = 1;
            break;
        case STREAM_AUDIO_AAC :
            idx = 2;
            break;
      //case STREAM_VIDEO_PRIVATE :
        case STREAM_AUDIO_LPCM :
            idx = 3;
            break;
        case STREAM_AUDIO_AC3 :
            idx = 4;
            break;
        case STREAM_AUDIO_EAC3 :
        case STREAM_AUDIO_MLP :
        case STREAM_AUDIO_DDPLUS :
        case STREAM_AUDIO_DDPLUS_SUB :
            idx = 5;
            break;
        case STREAM_AUDIO_DTS :
        case STREAM_AUDIO_DTS_HD :
        case STREAM_AUDIO_DTS_HD_XLL :
        case STREAM_AUDIO_DTS_HD_SUB :
            idx = 6;
            break;
        /* Priavete Stream */
        case STREAM_PES_PRIVATE_DATA:
            idx = 0;        // FIXME
            break;
        default :
            break;
    }
    return raw_ext[sample_type][idx];
}

MAPI_EXPORT mpeg_stream_type mpeg_api_get_sample_stream_type
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_sample_stream_type( info->parser_info, sample_type, stream_number );
}

MAPI_EXPORT uint32_t mpeg_api_get_sample_num
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return 0;
    uint32_t sample_num = 0;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < info->sample_list.video_stream_num )
        sample_num = info->sample_list.video_stream[stream_number].video_num;
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < info->sample_list.audio_stream_num )
        sample_num = info->sample_list.audio_stream[stream_number].audio_num;
    return sample_num;
}

MAPI_EXPORT int mpeg_api_get_sample_info
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint32_t                    sample_number,
    stream_info_t              *stream_info
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info || !stream_info )
        return -1;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < info->sample_list.video_stream_num )
    {
        sample_list_data_t *list     = info->sample_list.video_stream[stream_number].video;
        int64_t             list_num = info->sample_list.video_stream[stream_number].video_num;
        if( !list || sample_number >= list_num )
            return -1;
        gop_list_data_t *gop = &(info->sample_list.video_stream[stream_number].video_gop[list[sample_number].gop_number]);
        stream_info->file_position        = list[sample_number].file_position;
        stream_info->sample_size          = list[sample_number].sample_size;
        stream_info->raw_data_size        = list[sample_number].raw_data_size;
        stream_info->au_size              = list[sample_number].au_size;
        stream_info->video_pts            = list[sample_number].timestamp.pts;
        stream_info->video_dts            = list[sample_number].timestamp.dts;
        stream_info->video_program_id     = list[sample_number].program_id;
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
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < info->sample_list.audio_stream_num )
    {
        sample_list_data_t *list     = info->sample_list.audio_stream[stream_number].audio;
        int64_t             list_num = info->sample_list.audio_stream[stream_number].audio_num;
        if( !list || sample_number >= list_num )
            return -1;
        stream_info->file_position      = list[sample_number].file_position;
        stream_info->sample_size        = list[sample_number].sample_size;
        stream_info->raw_data_size      = list[sample_number].raw_data_size;
        stream_info->au_size            = list[sample_number].au_size;
        stream_info->audio_pts          = list[sample_number].timestamp.pts;
        stream_info->audio_dts          = list[sample_number].timestamp.dts;
        stream_info->audio_program_id   = list[sample_number].program_id;
        stream_info->sampling_frequency = list[sample_number].sampling_frequency;
        stream_info->bitrate            = list[sample_number].bitrate;
        stream_info->channel            = list[sample_number].channel;
        stream_info->layer              = list[sample_number].layer;
        stream_info->bit_depth          = list[sample_number].bit_depth;
    }
    else
        return -1;
    return 0;
}

MAPI_EXPORT int mpeg_api_get_sample_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint32_t                    sample_number,
    uint8_t                   **dst_buffer,
    uint32_t                   *dst_read_size,
    get_sample_data_mode        get_mode
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info || !dst_buffer || !dst_read_size )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    sample_list_data_t *list;
    int64_t list_num;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        list     = info->sample_list.video_stream[stream_number].video;
        list_num = info->sample_list.video_stream[stream_number].video_num;
        if( !list || sample_number >= list_num )
            return -1;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        list     = info->sample_list.audio_stream[stream_number].audio;
        list_num = info->sample_list.audio_stream[stream_number].audio_num;
        if( !list || sample_number >= list_num )
            return -1;
    }
    else
        return -1;
    /* get sample data. */
    int64_t  file_position = list[sample_number].file_position;
    uint32_t sample_size   = list[sample_number].sample_size;
    int32_t  read_offset   = 0;
    if( get_mode == GET_SAMPLE_DATA_RAW )
    {
        sample_size        = list[sample_number].raw_data_size;
        read_offset        = list[sample_number].raw_data_read_offset;
    }
    if( !sample_size )
        return -1;
    return parser->get_sample_data( parser_info, sample_type, stream_number
                                  , file_position, sample_size, read_offset
                                  , dst_buffer, dst_read_size, get_mode );
}

MAPI_EXPORT int mpeg_api_free_sample_buffer( void *ih, uint8_t **buffer )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info || !buffer )
        return -1;
    info->parser->free_sample_buffer( buffer );
    return 0;
}

MAPI_EXPORT int mpeg_api_get_pcr( void *ih, pcr_info_t *pcr_info, uint16_t service_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_pcr( info->parser_info, pcr_info, service_id );
}

MAPI_EXPORT int mpeg_api_get_video_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_VIDEO, stream_number );
    /* get video. */
    video_sample_info_t video_sample_info;
    if( parser->get_video_info( parser_info, stream_number, &video_sample_info ) )
        return -1;
    stream_info->file_position        = video_sample_info.file_position;
    stream_info->sample_size          = video_sample_info.sample_size;
    stream_info->raw_data_size        = video_sample_info.raw_data_size;
    stream_info->au_size              = video_sample_info.au_size;
    stream_info->video_pts            = video_sample_info.pts;
    stream_info->video_dts            = video_sample_info.dts;
    stream_info->video_program_id     = video_sample_info.program_id;
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

MAPI_EXPORT int mpeg_api_get_audio_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    parser->seek_next_sample_position( parser_info, SAMPLE_TYPE_AUDIO, stream_number );
    /* get audio. */
    audio_sample_info_t audio_sample_info;
    if( parser->get_audio_info( parser_info, stream_number, &audio_sample_info ) )
        return -1;
    stream_info->file_position      = audio_sample_info.file_position;
    stream_info->sample_size        = audio_sample_info.sample_size;
    stream_info->raw_data_size      = audio_sample_info.raw_data_size;
    stream_info->au_size            = audio_sample_info.au_size;
    stream_info->audio_pts          = audio_sample_info.pts;
    stream_info->audio_dts          = audio_sample_info.dts;
    stream_info->audio_program_id   = audio_sample_info.program_id;
    stream_info->sampling_frequency = audio_sample_info.sampling_frequency;
    stream_info->bitrate            = audio_sample_info.bitrate;
    stream_info->channel            = audio_sample_info.channel;
    stream_info->layer              = audio_sample_info.layer;
    stream_info->bit_depth          = audio_sample_info.bit_depth;
    return 0;
}

MAPI_EXPORT int mpeg_api_parse( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->parse( info->parser_info );
}

MAPI_EXPORT int mpeg_api_get_stream_parse_info
(
    void                       *ih,
    stream_info_t              *stream_info,
    int64_t                    *video_1st_pts,
    int64_t                    *video_key_pts,
    uint16_t                    service_id
)
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    mpeg_parser_t *parser      = info->parser;
    void          *parser_info = info->parser_info;
    /* parse. */
    if( parser->parse( parser_info ) )
        return -1;
    /* check video and audio PTS. */
    enum {
        BOTH_VA_NONE  = 0x11,
        VIDEO_NONE    = 0x10,
        AUDIO_NONE    = 0x01,
        BOTH_VA_EXIST = 0x00
    };
    int check_stream_exist = BOTH_VA_EXIST;
    uint8_t stream_number = 0;      // FIXME
    /* get video. */
    video_sample_info_t video_sample_info = { .pts = MPEG_TIMESTAMP_INVALID_VALUE };
    check_stream_exist |= parser->get_video_info( parser_info, stream_number, &video_sample_info ) ? VIDEO_NONE : 0;
    if( !(check_stream_exist & VIDEO_NONE) )
    {
        *video_1st_pts = video_sample_info.pts;
        *video_key_pts = (video_sample_info.picture_coding_type == MPEG_VIDEO_I_FRAME) ? video_sample_info.pts : -1;
        while( video_sample_info.temporal_reference || *video_key_pts < 0 )
        {
            if( parser->get_video_info( parser_info, stream_number, &video_sample_info ) )
                break;
            if( *video_1st_pts > video_sample_info.pts && *video_1st_pts < video_sample_info.pts + info->wrap_around_check_v )
                *video_1st_pts = video_sample_info.pts;
            if( *video_key_pts < 0 && video_sample_info.picture_coding_type == MPEG_VIDEO_I_FRAME )
                *video_key_pts = video_sample_info.pts;
        }
    }
    else
        *video_1st_pts = *video_key_pts = MPEG_TIMESTAMP_INVALID_VALUE;
    /* get audio. */
    audio_sample_info_t audio_sample_info = { .pts = MPEG_TIMESTAMP_INVALID_VALUE };
    check_stream_exist |= parser->get_audio_info( parser_info, stream_number, &audio_sample_info ) ? AUDIO_NONE : 0;
    /* get pcr. */
    pcr_info_t pcr_info;
    parser->get_pcr( info->parser_info, &pcr_info, service_id );
    /* setup. */
    stream_info->pcr                = pcr_info.pcr < pcr_info.start_pcr ? pcr_info.start_pcr : pcr_info.pcr;
    stream_info->video_pts          = video_sample_info.pts;
    stream_info->audio_pts          = audio_sample_info.pts;
    stream_info->sampling_frequency = audio_sample_info.sampling_frequency;
    stream_info->bitrate            = audio_sample_info.bitrate;
    stream_info->channel            = audio_sample_info.channel;
    stream_info->layer              = audio_sample_info.layer;
    stream_info->bit_depth          = audio_sample_info.bit_depth;
    return (check_stream_exist == BOTH_VA_NONE);
}

MAPI_EXPORT int mpeg_api_set_pmt_target( void *ih, pmt_target_type pmt_target )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    info->parser->set_program_target( info->parser_info, pmt_target );
    return 0;
}

MAPI_EXPORT int mpeg_api_set_pmt_program_id( void *ih, uint16_t pmt_program_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->set_program_id( info->parser_info, PID_TYPE_PMT, pmt_program_id );
}

MAPI_EXPORT int mpeg_api_set_service_id( void *ih, uint16_t service_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->set_service_id( info->parser_info, service_id );
}

MAPI_EXPORT int mpeg_api_get_service_id_num( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_service_id_num( info->parser_info );
}

MAPI_EXPORT int mpeg_api_set_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->set_service_id_info( info->parser_info, sid_info, sid_info_num );
}

MAPI_EXPORT int mpeg_api_get_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_service_id_info( info->parser_info, sid_info, sid_info_num );
}

MAPI_EXPORT uint16_t mpeg_api_get_program_id( void *ih, mpeg_sample_type sample_type, uint8_t stream_no, uint16_t service_id )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info || !info->parser_info )
        return -1;
    return info->parser->get_program_id( info->parser_info, sample_type, stream_no, service_id );
}

MAPI_EXPORT void *mpeg_api_initialize_info( const char *mpeg, int64_t buffer_size )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)malloc( sizeof(mpeg_api_info_t) );
    if( !info )
        return NULL;
    mpeg_parser_t *parser      = NULL;
    void          *parser_info = NULL;
    static mpeg_parser_t *parsers[MPEG_PARSER_NUM + 1] =
        {
            &mpegts_parser,
            &mpeges_parser,
            NULL
        };
    for( int i = 0; parsers[i]; ++i )
    {
        parser_info = parsers[i]->initialize( mpeg, buffer_size );
        if( parser_info )
        {
            parser = parsers[i];
            break;
        }
    }
    if( !parser_info )
        goto fail_initialize;
    /* check file size. */
    int64_t file_size = get_file_size( (char *)mpeg );
    /* setup. */
    memset( info, 0, sizeof(mpeg_api_info_t) );
    info->parser              = parser;
    info->parser_info         = parser_info;
    info->wrap_around_check_v = TIMESTAMP_WRAP_AROUND_CHECK_VALUE;
    info->file_size           = file_size;
    return info;
fail_initialize:
    if( parser_info )
        free( parser_info );
    if( info )
        free( info );
    return NULL;
}

MAPI_EXPORT void mpeg_api_release_info( void *ih )
{
    mpeg_api_info_t *info = (mpeg_api_info_t *)ih;
    if( !info )
        return;
    if( info->parser_info )
        info->parser->release( info->parser_info );
    if( info->sample_list.video_stream )
    {
        for( uint8_t i = 0; i < info->sample_list.video_stream_num; ++i )
        {
            video_stream_data_t *video_stream = &(info->sample_list.video_stream[i]);
            if( video_stream->video_gop )
                free( video_stream->video_gop );
            if( video_stream->video )
                free( video_stream->video );
        }
        free( info->sample_list.video_stream );
    }
    if( info->sample_list.audio_stream )
    {
        for( uint8_t i = 0; i < info->sample_list.audio_stream_num; ++i )
        {
            audio_stream_data_t *audio_stream = &(info->sample_list.audio_stream[i]);
            if( audio_stream->audio )
                free( audio_stream->audio );
        }
        free( info->sample_list.audio_stream );
    }
    free( info );
}
