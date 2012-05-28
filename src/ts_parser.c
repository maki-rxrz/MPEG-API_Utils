/*****************************************************************************
 * ts_parser.c
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
#include <stdarg.h>

#include "mpeg_utils.h"

#define PROGRAM_VERSION                 "0.0.1"

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

typedef struct {
    char               *input;
    char               *output;
    output_mode_type    output_mode;
    uint16_t            pmt_program_id;
} param_t;

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
        "       --output-mode <integer> Specify output mode. [0-2]\n"
        "       --debug <integer>       Specify output log level. [1-4]\n"
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
        else if( !strcasecmp( argv[i], "--debug" ) )
        {
            debug_level = atoi( argv[++i] );
            if( debug_level > LOG_LV_ALL )
                debug_level = LOG_LV_ALL;
            else if( debug_level < LOG_LV0 )
                debug_level = LOG_LV0;
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

static void parse_mpeg( param_t *p )
{
    if( !p || !p->input )
        return;
    /* parse. */
    void *info = mpeg_api_initialize_info( p->input );
    if( !info )
        return;
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
        static const char frame[4] = { '?', 'I', 'P', 'B' };
        if( p->output_mode == OUTPUT_SEQUENTIAL_READ_GET_INFO )
        {
            dprintf( LOG_LV0, "[log] Video\n" );
            int64_t gop_number = -1;
            for( uint32_t i = 0; ; ++i )
            {
                stream_info_t stream_info;
                if( mpeg_api_get_video_frame( info, &stream_info ) )
                    break;
                int64_t pts = stream_info.video_pts;
                int64_t dts = stream_info.video_dts;
                if( stream_info.gop_number < 0 )
                {
                    dprintf( LOG_LV0, " [no GOP Picture data]" );
                    dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                    if( dts == -1 )
                        dprintf( LOG_LV0, "\n" );
                    else
                        dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]\n", dts, dts / 90 );
                    i = -1;
                    continue;
                }
                if( gop_number < stream_info.gop_number )
                {
                    gop_number = stream_info.gop_number;
                    dprintf( LOG_LV0, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info.progressive_sequence, stream_info.closed_gop );
                }
                dprintf( LOG_LV0, " [%8d]  pict_struct:%d  order:%2d  [%c]", i, stream_info.picture_structure, stream_info.temporal_reference, frame[stream_info.picture_coding_type] );
                dprintf( LOG_LV0, "  progr_frame:%d  rff:%d  tff:%d", stream_info.progressive_frame, stream_info.repeat_first_field, stream_info.top_field_first );
                dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info.file_position );
                dprintf( LOG_LV0, "  size: %10d", stream_info.sample_size );
                dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                if( dts == -1 )
                    dprintf( LOG_LV0, "\n" );
                else
                    dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]\n", dts, dts / 90 );
            }
            dprintf( LOG_LV0, "[log] Audio\n" );
            for( uint32_t i = 0; ; ++i )
            {
                stream_info_t stream_info;
                if( mpeg_api_get_audio_frame( info, &stream_info ) )
                    break;
                int64_t pts = stream_info.audio_pts;
                int64_t dts = stream_info.audio_dts;
                dprintf( LOG_LV0, " [%8d]  POS: %10"PRId64"", i, stream_info.file_position );
                dprintf( LOG_LV0, "  size: %10d", stream_info.sample_size );
                dprintf( LOG_LV0, "  PTS: %"PRId64" [%"PRId64"ms]", pts, pts / 90 );
                if( dts == -1 )
                    dprintf( LOG_LV0, "\n" );
                else
                    dprintf( LOG_LV0, "  DTS: %"PRId64" [%"PRId64"ms]\n", dts, dts / 90 );
            }
        }
        else if( p->output_mode == OUTPUT_CREATE_LIST_GET_INFO )
        {
            if( mpeg_api_create_sample_list( info ) )
                goto end_parse;
            dprintf( LOG_LV0, "[log] Video\n" );
            int64_t gop_number = -1;
            for( uint32_t i = 0; ; ++i )
            {
                stream_info_t stream_info;
                if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, &stream_info ) )
                    break;
                int64_t pts = stream_info.video_pts;
                int64_t dts = stream_info.video_dts;
                if( gop_number < stream_info.gop_number )
                {
                    gop_number = stream_info.gop_number;
                    dprintf( LOG_LV0, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info.progressive_sequence, stream_info.closed_gop );
                }
                dprintf( LOG_LV0, " [%8d]  pict_struct:%d  order:%2d  [%c]", i, stream_info.picture_structure, stream_info.temporal_reference, frame[stream_info.picture_coding_type] );
                dprintf( LOG_LV0, "  progr_frame:%d  rff:%d  tff:%d", stream_info.progressive_frame, stream_info.repeat_first_field, stream_info.top_field_first );
                dprintf( LOG_LV0, "  POS: %10"PRId64"", stream_info.file_position );
                dprintf( LOG_LV0, "  size: %10d", stream_info.sample_size );
                dprintf( LOG_LV0, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
                if( dts != -1 )
                    dprintf( LOG_LV0, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
                dprintf( LOG_LV0, "\n" );
            }
            dprintf( LOG_LV0, "[log] Audio\n" );
            for( uint32_t i = 0; ; ++i )
            {
                stream_info_t stream_info;
                if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_AUDIO, i, &stream_info ) )
                    break;
                int64_t pts = stream_info.audio_pts;
                int64_t dts = stream_info.audio_dts;
                dprintf( LOG_LV0, " [%8d]  POS: %10"PRId64"", i, stream_info.file_position );
                dprintf( LOG_LV0, "  size: %10d", stream_info.sample_size );
                dprintf( LOG_LV0, "  PTS: %"PRId64" [%"PRId64"ms]", pts, pts / 90 );
                if( dts != -1 )
                    dprintf( LOG_LV0, "  DTS: %"PRId64" [%"PRId64"ms]", dts, dts / 90 );
                dprintf( LOG_LV0, "\n" );
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
            /* check sample num. */
            uint32_t sample_video_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO );
            uint32_t sample_audio_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO );
            dprintf( LOG_LV0, "[log] sample num  Video:%8d  Audio:%8d\n", sample_video_num, sample_audio_num );
            /* get data. */
            size_t dump_name_size = strlen( p->output ) + 11;
            char dump_name[dump_name_size];
            FILE *video = NULL, *audio = NULL;
            if( sample_video_num )
            {
                strcpy( dump_name, p->output );
                strcat( dump_name, get_sample_list[get_index].ext );
                strcat( dump_name, ".video" );
                video = fopen( dump_name, "wb" );
            }
            if( sample_audio_num )
            {
                char dump_name[dump_name_size];
                strcpy( dump_name, p->output );
                strcat( dump_name, get_sample_list[get_index].ext );
                strcat( dump_name, ".audio" );
                audio = fopen( dump_name, "wb" );
            }
            if( !video && !audio )
                goto end_parse;
            /* outptut. */
            dprintf( LOG_LV0, "[log] Video\n" );
            for( uint32_t i = 0; i < sample_video_num; ++i )
            {
                uint8_t *buffer = NULL;
                uint32_t data_size = 0;
                if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_VIDEO, i, &buffer, &data_size, get_sample_list[get_index].get_mode ) )
                    break;
                if( buffer && data_size )
                {
                    fwrite( buffer, 1, data_size, video );
                    free( buffer );
                }
                dprintf( LOG_LV0, " [%8d]  size: %10d\n", i, data_size );
            }
            dprintf( LOG_LV0, "[log] Audio\n" );
            for( uint32_t i = 0; i < sample_audio_num; ++i )
            {
                uint8_t *buffer = NULL;
                uint32_t data_size = 0;
                if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_AUDIO, i, &buffer, &data_size, get_sample_list[get_index].get_mode ) )
                    break;
                if( buffer && data_size )
                {
                    fwrite( buffer, 1, data_size, audio );
                    free( buffer );
                }
                dprintf( LOG_LV0, " [%8d]  size: %10d\n", i, data_size );
            }
            if( video )
                fclose( video );
            if( audio )
                fclose( audio );
        }
    }
    else if( parse_result > 0 )
        dprintf( LOG_LV0, "[log] MPEG-TS not have both video and audio stream.\n" );
    else
        dprintf( LOG_LV0, "[log] MPEG-TS no read.\n" );
end_parse:
    mpeg_api_release_info( info );
}

int main( int argc, char *argv[] )
{
    if( argc < 2 )
    {
        print_help();
        return -1;
    }
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
