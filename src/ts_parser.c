/*****************************************************************************
 * ts_parser.c
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
 *   A-4. When you release a modified version to public, you
 *       must publish it with your name.
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

#include "config.h"

#include "mpeg_utils.h"
#include "thread_utils.h"

#define PROGRAM_VERSION                 "0.0.2"

#ifndef REVISION_NUMBER
#define REVISION_NUMBER                 "0"
#endif

typedef enum {
    OUTPUT_SEQUENTIAL_READ_GET_INFO         = 0,    /* default */
    OUTPUT_CREATE_LIST_GET_INFO             = 1,
    OUTPUT_CREATE_LIST_GET_SAMPLE_RAW       = 2,
    OUTPUT_CREATE_LIST_GET_SAMPLE_PES       = 3,
    OUTPUT_CREATE_LIST_GET_SAMPLE_CONTAINER = 4,
    OUTPUT_MODE_MAX
} output_mode_type;

typedef enum {
    OUTPUT_STREAM_NONE    = 0x00,
    OUTPUT_STREAM_VIDEO   = 0x01,
    OUTPUT_STREAM_AUDIO   = 0x02,
    OUTPUT_STREAM_BOTH_VA = 0x03
} output_stream_type;

typedef enum {
    OUTPUT_DEMUX_SEQUENTIAL_READ  = 0x00,
    OUTPUT_DEMUX_MULTITHREAD_READ = 0x01
} output_demux_mode_type;

typedef struct {
    char                   *input;
    char                   *output;
    output_mode_type        output_mode;
    output_stream_type      output_stream;
    output_demux_mode_type  demux_mode;
    uint16_t                pmt_program_id;
    int64_t                 wrap_around_check_v;
} param_t;

#define TIMESTAMP_WRAP_AROUND_CHECK_VALUE       (0x0FFFFFFFFLL)

static void print_version( void )
{
    fprintf( stdout,
        "MPEG-2 TS/ES Parser version " PROGRAM_VERSION "." REVISION_NUMBER "\n"
    );
}

static void print_help( void )
{
    fprintf( stdout,
        "\n"
        "MPEG-2 TS/ES Parser version " PROGRAM_VERSION "." REVISION_NUMBER "\n"
        "\n"
        "usage:  ts_parser [options] <input>\n"
        "\n"
        "options:\n"
        "    -o --output <string>       Specify output file name.\n"
        "       --pmt-pid <integer>     Specify Program Map Table ID.\n"
        "       --output-mode <integer> Specify output mode. [0-4]\n"
        "       --output-stream <string>"
        "                               Specify output stream type.\n"
        "                                   - v  : video only\n"
        "                                   - a  : audio only\n"
        "                                   - va : video/audio\n"
        "       --demux-mode <integer>  Specify output demux mode. [0-1]\n"
        "       --debug <integer>       Specify output log level. [1-4]\n"
        "    -v --version               Display the version information."
        "\n"
    );
}

static log_level debug_level = LOG_LV0;
extern void dprintf( log_level level, const char *format, ... )
{
    if( debug_level < level )
        return;
    va_list argptr;
    va_start( argptr, format );
    vfprintf( stderr, format, argptr );
    va_end( argptr );
}

static int init_parameter( param_t *p )
{
    if( !p )
        return -1;
    memset( p, 0, sizeof(param_t) );
    p->output_stream       = OUTPUT_STREAM_BOTH_VA;
    p->wrap_around_check_v = TIMESTAMP_WRAP_AROUND_CHECK_VALUE;
    return 0;
}

static void cleanup_parameter( param_t *p )
{
    if( p->output )
        free( p->output );
    if( p->input )
        free( p->input );
}

static int parse_commandline( int argc, char **argv, int index, param_t *p )
{
    if( !argv )
        return -1;
    int i = index;
    while( i < argc && *argv[i] == '-' )
    {
        if( !strcasecmp( argv[i], "--output" ) || !strcasecmp( argv[i], "-o" ) )
        {
            if( p->output )
                free( p->output );
            p->output = strdup( argv[++i] );
        }
        else if( !strcasecmp( argv[i], "--pmt-pid" ) )
        {
            ++i;
            int base = (strncmp( argv[i], "0x", 2 )) ? 10 : 16;
            p->pmt_program_id = strtol( argv[i], NULL, base );
        }
        else if( !strcasecmp( argv[i], "--output-mode" ) )
        {
            int mode = atoi( argv[++i] );
            if( 0 <= mode && mode < OUTPUT_MODE_MAX )
                p->output_mode = mode;
            else if( mode < 0 )
                p->output_mode = 0;
        }
        else if( !strcasecmp( argv[i], "--output-stream" ) )
        {
            char *c = argv[++i];
            if( !strcasecmp( c, "va" ) || !strcasecmp( c, "av" ) )
                p->output_stream = OUTPUT_STREAM_BOTH_VA;
            else if( !strncasecmp( c, "v", 1 ) )
                p->output_stream = OUTPUT_STREAM_VIDEO;
            else if( !strncasecmp( c, "a", 1 ) )
                p->output_stream = OUTPUT_STREAM_AUDIO;
        }
        else if( !strcasecmp( argv[i], "--demux-mode" ) )
            p->demux_mode = atoi( argv[++i] );
        else if( !strcasecmp( argv[i], "--debug" ) )
        {
            debug_level = atoi( argv[++i] );
            if( debug_level > LOG_LV_ALL )
                debug_level = LOG_LV_ALL;
            else if( debug_level < LOG_LV0 )
                debug_level = LOG_LV0;
            mpeg_api_setup_log_lv( debug_level, stderr );
        }
        ++i;
    }
    if( i < argc )
    {
        p->input = strdup( argv[i] );
        ++i;
    }
    return i;
}

static int correct_parameter( param_t *p )
{
    if( !p || !p->input )
        return -1;
    /* output. */
    if( !p->output )
    {
        p->output = strdup( p->input );
        if( !p->output )
            return -1;
    }
    return 0;
}

typedef struct {
    void                   *api_info;
    mpeg_sample_type        get_type;
    get_sample_data_mode    get_mode;
    FILE                   *file;
    char                   *stream_name;
    uint8_t                 stream_no;
    uint32_t                sample_num;
} demux_param_t;

static void *demux( void *args )
{
    demux_param_t *param = (demux_param_t *)args;
    if( !param )
        return (void *)(-1);
    void *info                = param->api_info;
    mpeg_sample_type get_type = param->get_type;
    get_sample_data_mode mode = param->get_mode;
    FILE *file                = param->file;
    char *stream_name         = param->stream_name;
    uint8_t stream_no         = param->stream_no;
    uint32_t num              = param->sample_num;
    /* demux */
    dprintf( LOG_LV0, " %s Stream[%3u] [demux] start - sample_num:%u\n", stream_name, stream_no, num );
    for( uint32_t i = 0; i < num; ++i )
    {
        uint8_t *buffer = NULL;
        uint32_t data_size = 0;
        if( mpeg_api_get_sample_data( info, get_type, stream_no, i, &buffer, &data_size, mode ) )
            break;
        else if( buffer && data_size )
        {
            fwrite( buffer, 1, data_size, file );
            mpeg_api_free_sample_buffer( info, &buffer );
        }
        dprintf( LOG_LV0, " %s Stream[%3u] [%8u]  size: %10u\n", stream_name, stream_no, i, data_size );
    }
    dprintf( LOG_LV0, " %s Stream[%3u] [demux] end\n", stream_name, stream_no );
    return (void *)(0);
}

static void get_speaker_mapping_info( uint16_t speaker_mapping, char *mapping_info )
{
    uint8_t f_channels = !!(speaker_mapping & MPEG_AUDIO_SPEAKER_FRONT_CENTER) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_FRONT_LEFT   ) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_FRONT_RIGHT);
    uint8_t r_channels = !!(speaker_mapping & MPEG_AUDIO_SPEAKER_REAR_SRROUND) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_REAR_LEFT    ) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_REAR_RIGHT )
                       + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_REAR_LEFT2  ) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_REAR_RIGHT2  )
                       + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_OUTSIDE_LEFT) + !!(speaker_mapping & MPEG_AUDIO_SPEAKER_OUTSIDE_RIGHT);
    uint8_t lfe        = !!(speaker_mapping & MPEG_AUDIO_SPEAKER_LFE_CHANNEL);
    sprintf( mapping_info, "%u/%u", f_channels, r_channels );
    if( lfe )
        strcat( mapping_info, "+lfe" );
}

static void parse_mpeg( param_t *p )
{
    if( !p || !p->input )
        return;
    /* parse. */
    void *info = mpeg_api_initialize_info( p->input );
    if( !info )
        return;
    stream_info_t *stream_info = malloc( sizeof(stream_info) );
    if( !stream_info )
        goto end_parse;
    if( p->pmt_program_id )
        if( 0 > mpeg_api_set_pmt_program_id( info, p->pmt_program_id ) )
            goto end_parse;
    int parse_result = mpeg_api_parse( info );
    if( !parse_result )
    {
        /* check PCR. */
        int64_t pcr = mpeg_api_get_pcr( info );
        dprintf( LOG_LV0, "[log] pcr: %10"PRId64"  [%8"PRId64"ms]\n", pcr, pcr / 90 );
        /* check Video and Audio information. */
        uint8_t video_stream_num = mpeg_api_get_stream_num( info, SAMPLE_TYPE_VIDEO );
        uint8_t audio_stream_num = mpeg_api_get_stream_num( info, SAMPLE_TYPE_AUDIO );
        dprintf( LOG_LV0, "[log] stream_num:  Video:%4u  Audio:%4u\n", video_stream_num, audio_stream_num );
        if( !video_stream_num && !audio_stream_num )
            goto end_parse;
        static const char frame[4] = { '?', 'I', 'P', 'B' };
        if( p->output_mode == OUTPUT_SEQUENTIAL_READ_GET_INFO )
        {
            for( uint8_t i = 0; i < video_stream_num; ++i )
            {
                dprintf( LOG_LV0, "[log] Video Stream[%3u]\n", i );
                int64_t gop_number = -1;
                for( uint32_t j = 0; ; ++j )
                {
                    if( mpeg_api_get_video_frame( info, i, stream_info ) )
                        break;
                    int64_t pts = stream_info->video_pts;
                    int64_t dts = stream_info->video_dts;
                    if( stream_info->gop_number < 0 )
                    {
                        dprintf( LOG_LV0, " [no GOP Picture data]" );
                        j = -1;
                    }
                    else
                    {
                        if( gop_number < stream_info->gop_number )
                        {
                            gop_number = stream_info->gop_number;
                            dprintf( LOG_LV0, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info->progressive_sequence, stream_info->closed_gop );
                        }
                        dprintf( LOG_LV0, " [%8u]", j );
                    }
                    dprintf( LOG_LV0, "  pict_struct:%d  order:%2d  [%c]", stream_info->picture_structure, stream_info->temporal_reference, frame[stream_info->picture_coding_type] );
                    dprintf( LOG_LV0, "  progr_frame:%d  rff:%d  tff:%d", stream_info->progressive_frame, stream_info->repeat_first_field, stream_info->top_field_first );
                    dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info->file_position );
                    dprintf( LOG_LV0, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
                    dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                    if( dts != pts )
                        dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
                    dprintf( LOG_LV0, "\n" );
                }
            }
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                dprintf( LOG_LV0, "[log] Audio Stream[%3u]\n", i );
                for( uint32_t j = 0; ; ++j )
                {
                    if( mpeg_api_get_audio_frame( info, i, stream_info ) )
                        break;
                    int64_t pts = stream_info->audio_pts;
                    int64_t dts = stream_info->audio_dts;
                    char mapping_info[8];
                    get_speaker_mapping_info( stream_info->channel, mapping_info );
                    dprintf( LOG_LV0, " [%8u]", j );
                    dprintf( LOG_LV0, "  %6uHz  %4uKbps  %s channel  layer %1u  %2u bits", stream_info->sampling_frequency, stream_info->bitrate / 1000, mapping_info, stream_info->layer, stream_info->bit_depth );
                    dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info->file_position );
                    dprintf( LOG_LV0, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
                    dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                    if( dts != pts )
                        dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
                    dprintf( LOG_LV0, "\n" );
                }
            }
        }
        else if( p->output_mode == OUTPUT_CREATE_LIST_GET_INFO )
        {
            if( mpeg_api_create_sample_list( info ) )
                goto end_parse;
            for( uint8_t i = 0; i < video_stream_num; ++i )
            {
                dprintf( LOG_LV0, "[log] Video Stream[%3u]\n", i );
                int64_t gop_number = -1;
                for( uint32_t j = 0; ; ++j )
                {
                    if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, j, stream_info ) )
                        break;
                    int64_t pts = stream_info->video_pts;
                    int64_t dts = stream_info->video_dts;
                    if( gop_number < stream_info->gop_number )
                    {
                        gop_number = stream_info->gop_number;
                        dprintf( LOG_LV0, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info->progressive_sequence, stream_info->closed_gop );
                    }
                    dprintf( LOG_LV0, " [%8u]", j );
                    dprintf( LOG_LV0, "  pict_struct:%d  order:%2d  [%c]", stream_info->picture_structure, stream_info->temporal_reference, frame[stream_info->picture_coding_type] );
                    dprintf( LOG_LV0, "  progr_frame:%d  rff:%d  tff:%d", stream_info->progressive_frame, stream_info->repeat_first_field, stream_info->top_field_first );
                    dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info->file_position );
                    dprintf( LOG_LV0, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
                    dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                    if( dts != pts )
                        dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
                    dprintf( LOG_LV0, "\n" );
                }
            }
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                dprintf( LOG_LV0, "[log] Audio Stream[%3u]\n", i );
                for( uint32_t j = 0; ; ++j )
                {
                    if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_AUDIO, i, j, stream_info ) )
                        break;
                    int64_t pts = stream_info->audio_pts;
                    int64_t dts = stream_info->audio_dts;
                    char mapping_info[8];
                    get_speaker_mapping_info( stream_info->channel, mapping_info );
                    dprintf( LOG_LV0, " [%8u]", j );
                    dprintf( LOG_LV0, "  %6uHz  %4uKbps  %s channel  layer %1u  %2u bits", stream_info->sampling_frequency, stream_info->bitrate / 1000, mapping_info, stream_info->layer, stream_info->bit_depth );
                    dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info->file_position );
                    dprintf( LOG_LV0, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
                    dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                    if( dts != pts )
                        dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
                    dprintf( LOG_LV0, "\n" );
                }
            }
        }
        else if( p->output_mode == OUTPUT_CREATE_LIST_GET_SAMPLE_RAW
              || p->output_mode == OUTPUT_CREATE_LIST_GET_SAMPLE_PES
              || p->output_mode == OUTPUT_CREATE_LIST_GET_SAMPLE_CONTAINER )
        {
            int get_index = p->output_mode - OUTPUT_CREATE_LIST_GET_SAMPLE_RAW;
            static const struct {
                get_sample_data_mode get_mode;
                char ext[5];
            } get_sample_list[3] =
                {
                    { GET_SAMPLE_DATA_RAW       , ".raw" },
                    { GET_SAMPLE_DATA_PES_PACKET, ".pes" },
                    { GET_SAMPLE_DATA_CONTAINER , ".ts"  },
                };
            if( mpeg_api_create_sample_list( info ) )
                goto end_parse;
            /* ready file. */
            FILE *video[video_stream_num], *audio[audio_stream_num];
            if( video_stream_num )
                memset( video, 0, sizeof(FILE *) * video_stream_num );
            if( audio_stream_num )
                memset( audio, 0, sizeof(FILE *) * audio_stream_num );
            int64_t video_first_pts = -1;
            for( uint8_t i = 0; i < video_stream_num; ++i )
            {
                /* check sample num. */
                uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
                dprintf( LOG_LV0, "[log] Video Stream[%3u]  sample_num:%8u\n", i, sample_num );
                if( sample_num && (p->output_stream & OUTPUT_STREAM_VIDEO) )
                {
                    /* open output file. */
                    size_t dump_name_size = strlen( p->output ) + 16;
                    char dump_name[dump_name_size];
                    strcpy( dump_name, p->output );
                    strcat( dump_name, get_sample_list[get_index].ext );
                    if( video_stream_num > 1 )
                    {
                        char stream_name[5];
                        sprintf( stream_name, ".v%u", i );
                        strcat( dump_name, stream_name );
                    }
                    if( get_sample_list[get_index].get_mode == GET_SAMPLE_DATA_RAW )
                    {
                        const char* raw_ext = mpeg_api_get_sample_file_extension( info, SAMPLE_TYPE_VIDEO, i );
                        if( raw_ext )
                            strcat( dump_name, raw_ext );
                        else
                            strcat( dump_name, ".video" );
                    }
                    else
                        strcat( dump_name, ".video" );
                    video[i] = fopen( dump_name, "wb" );
                }
                if( video_first_pts < 0 && video[i] )
                {
                    /* get first video stream info. */
                    for( uint32_t j = 0; j < sample_num; ++j )
                    {
                        if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, j, stream_info ) )
                        {
                            fclose( video[i] );
                            video[i] = NULL;
                            break;
                        }
                        if( stream_info->temporal_reference == -1 )
                            break;
                        if( !(stream_info->temporal_reference) )
                        {
                            video_first_pts = stream_info->video_pts;
                            break;
                        }
                    }
                }
            }
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                /* check sample num. */
                uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
                dprintf( LOG_LV0, "[log] Audio Stream[%3u]  sample_num:%8u\n", i, sample_num );
                if( sample_num && (p->output_stream & OUTPUT_STREAM_AUDIO) )
                {
                    /* calculate audio delay time. */
                    int64_t audio_delay = 0;
                    if( video_first_pts >= 0 )
                    {
                        if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_AUDIO, i, 0, stream_info ) )
                            continue;
                        audio_delay = stream_info->audio_pts - video_first_pts;
                        if( llabs(audio_delay) > p->wrap_around_check_v )
                            audio_delay += MPEG_TIMESTAMP_WRAPAROUND_VALUE * ((audio_delay) > 0 ? -1 : 1);
                    }
                    /* open output file. */
                    char delay_string[256];
                    int delay_str_len = 0;
                    if( audio_delay )
                    {
                        sprintf( delay_string, ". DELAY %"PRId64"ms", audio_delay / 90 );
                        delay_str_len = strlen( delay_string );
                    }
                    size_t dump_name_size = strlen( p->output ) + 16 + delay_str_len;
                    char dump_name[dump_name_size];
                    strcpy( dump_name, p->output );
                    strcat( dump_name, get_sample_list[get_index].ext );
                    if( audio_stream_num > 1 )
                    {
                        char stream_name[6];
                        sprintf( stream_name, ".a%u", i );
                        strcat( dump_name, stream_name );
                    }
                    if( audio_delay )
                        strcat( dump_name, delay_string );
                    if( get_sample_list[get_index].get_mode == GET_SAMPLE_DATA_RAW )
                    {
                        const char* raw_ext = mpeg_api_get_sample_file_extension( info, SAMPLE_TYPE_AUDIO, i );
                        if( raw_ext )
                            strcat( dump_name, raw_ext );
                        else
                            strcat( dump_name, ".audio" );
                    }
                    else
                        strcat( dump_name, ".audio" );
                    audio[i] = fopen( dump_name, "wb" );
                }
            }
            /* outptut. */
            dprintf( LOG_LV0, "[log] Demux - START\n" );
            uint16_t total_stream_num = video_stream_num + audio_stream_num;
            if( p->demux_mode == OUTPUT_DEMUX_MULTITHREAD_READ && total_stream_num > 1 )
            {
                dprintf( LOG_LV0, "[log] Demux - Multi thread\n" );
                demux_param_t *param = (demux_param_t *)malloc( sizeof(demux_param_t) * total_stream_num );
                if( param )
                {
                    void *demux_thread[total_stream_num];
                    memset( demux_thread, 0, sizeof(void *) * total_stream_num );
                    uint16_t thread_index = 0;
                    /* video. */
                    for( uint8_t i = 0; i < video_stream_num; ++i )
                    {
                        if( video[i] )
                        {
                            param[thread_index].api_info    = info;
                            param[thread_index].get_type    = SAMPLE_TYPE_VIDEO;
                            param[thread_index].get_mode    = get_sample_list[get_index].get_mode;
                            param[thread_index].file        = video[i];
                            param[thread_index].stream_name = "Video";
                            param[thread_index].stream_no   = i;
                            param[thread_index].sample_num  = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
                            demux_thread[thread_index] = thread_create( demux, &param[thread_index] );
                            ++thread_index;
                        }
                    }
                    /* audio. */
                    for( uint8_t i = 0; i < audio_stream_num; ++i )
                    {
                        if( audio[i] )
                        {
                            param[thread_index].api_info    = info;
                            param[thread_index].get_type    = SAMPLE_TYPE_AUDIO;
                            param[thread_index].get_mode    = get_sample_list[get_index].get_mode;
                            param[thread_index].file        = audio[i];
                            param[thread_index].stream_name = "Audio";
                            param[thread_index].stream_no   = i;
                            param[thread_index].sample_num  = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
                            demux_thread[thread_index] = thread_create( demux, &param[thread_index] );
                            ++thread_index;
                        }
                    }
                    /* wait demux end. */
                    if( thread_index )
                    {
                        for( uint16_t i = 0; i < thread_index; ++i )
                        {
                            dprintf( LOG_LV0, "[log] wait %s Stream[%3u]...\n", param[i].stream_name, param[i].stream_no );
                            thread_wait_end( demux_thread[i], NULL );
                        }
                    }
                    free( param );
                }
                /* close output file. */
                for( uint8_t i = 0; i < video_stream_num; ++i )
                    if( video[i] )
                        fclose( video[i] );
                for( uint8_t i = 0; i < audio_stream_num; ++i )
                    if( audio[i] )
                        fclose( audio[i] );
            }
            else
            {
                dprintf( LOG_LV0, "[log] Demux - Sequential read\n" );
                for( uint8_t i = 0; i < video_stream_num; ++i )
                {
                    if( video[i] )
                    {
                        uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
                        dprintf( LOG_LV0, " Video Stream[%3u] [demux] start - sample_num:%u\n", i, sample_num );
                        for( uint32_t j = 0; j < sample_num; ++j )
                        {
                            uint8_t *buffer = NULL;
                            uint32_t data_size = 0;
                            if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_VIDEO, i, j, &buffer, &data_size, get_sample_list[get_index].get_mode ) )
                                break;
                            if( buffer && data_size )
                            {
                                fwrite( buffer, 1, data_size, video[i] );
                                mpeg_api_free_sample_buffer( info, &buffer );
                            }
                            dprintf( LOG_LV0, " [%8u]  size: %10u\n", j, data_size );
                        }
                        fclose( video[i] );
                    }
                }
                for( uint8_t i = 0; i < audio_stream_num; ++i )
                {
                    if( audio[i] )
                    {
                        uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
                        dprintf( LOG_LV0, " Audio Stream[%3u] [demux] start - sample_num:%u\n", i, sample_num );
                        for( uint32_t j = 0; j < sample_num; ++j )
                        {
                            uint8_t *buffer = NULL;
                            uint32_t data_size = 0;
                            if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_AUDIO, i, j, &buffer, &data_size, get_sample_list[get_index].get_mode ) )
                                break;
                            if( buffer && data_size )
                            {
                                fwrite( buffer, 1, data_size, audio[i] );
                                mpeg_api_free_sample_buffer( info, &buffer );
                            }
                            dprintf( LOG_LV0, " [%8u]  size: %10u\n", j, data_size );
                        }
                        fclose( audio[i] );
                    }
                }
            }
            dprintf( LOG_LV0, "[log] Demux - END\n" );
        }
    }
    else if( parse_result > 0 )
        dprintf( LOG_LV0, "[log] MPEG not have both video and audio stream.\n" );
    else
        dprintf( LOG_LV0, "[log] MPEG no read.\n" );
end_parse:
    if( stream_info )
        free( stream_info );
    mpeg_api_release_info( info );
}

int check_commandline( int argc, char *argv[] )
{
    for( int i = 0; i < argc; ++i )
        if( !strcasecmp( argv[i], "--version" ) || !strcasecmp( argv[i], "-v" ) )
        {
            print_version();
            return 1;
        }
    if( argc < 2 )
    {
        print_help();
        return 1;
    }
    return 0;
}

int main( int argc, char *argv[] )
{
    if( check_commandline( argc, argv ) )
        return 0;
    mpeg_api_setup_log_lv( debug_level, stderr );
    int i = 1;
    while( i < argc )
    {
        param_t param;
        if( init_parameter( &param ) )
            return -1;
        i = parse_commandline( argc, argv, i, &param );
        if( i < 0 )
            break;
        if( !correct_parameter( &param ) )
            parse_mpeg( &param );
        cleanup_parameter( &param );
    }
    return 0;
}
