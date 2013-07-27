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
#include <ctype.h>

#include "config.h"

#include "mpeg_utils.h"
#include "thread_utils.h"
#include "file_utils.h"
#include "file_writer.h"

#define PROGRAM_VERSION                 "0.0.7"

#ifndef REVISION_NUMBER
#define REVISION_NUMBER                 "0"
#endif

typedef enum {
    USE_MAPI_SEQUENTIAL_READ = 0,       /* default */
    USE_MAPI_SAMPLE_LIST     = 1,
    USE_MAPI_DEMUX_ALL       = 2,
    USE_MAPI_DEMUX_ALL_ST    = 3,
    USE_MAPI_TYPE_MAX
} use_mapi_type;

typedef enum {
    OUTPUT_GET_INFO             = 0,    /* default */
    OUTPUT_GET_SAMPLE_RAW       = 1,
    OUTPUT_GET_SAMPLE_PES       = 2,
    OUTPUT_GET_SAMPLE_CONTAINER = 3,
    OUTPUT_MODE_MAX
} output_mode_type;

typedef enum {
    OUTPUT_DEMUX_SEQUENTIAL_READ  = 0x00,
    OUTPUT_DEMUX_MULTITHREAD_READ = 0x01
} output_demux_mode_type;

typedef struct {
    char                   *input;
    char                   *output;
    use_mapi_type           api_type;
    output_mode_type        output_mode;
    output_stream_type      output_stream;
    output_demux_mode_type  demux_mode;
    uint16_t                pmt_program_id;
    int64_t                 wrap_around_check_v;
    mpeg_reader_delay_type  delay_type;
    FILE                   *logfile;
    int64_t                 read_buffer_size;
    int64_t                 write_buffer_size;
    int64_t                 file_size;
} param_t;

static const struct {
    get_sample_data_mode get_mode;
    char ext[5];
} get_sample_list[3] =
    {
        { GET_SAMPLE_DATA_RAW       , ".raw" },
        { GET_SAMPLE_DATA_PES_PACKET, ".pes" },
        { GET_SAMPLE_DATA_CONTAINER , ".ts"  },
    };

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
        "       --mode <string>         Specify together multiple settings.\n"
        "                                   - p : Parse (--api-type 0 --output-mode 0)\n"
        "                                   - d : Demux (--api-type 3 --output-mode 1)\n"
        "                                   - v : video (--output-stream v)\n"
        "                                   - a : audio (--output-stream a)\n"
        "                                   - m : Multi-thread (--demux-mode 1)\n"
        "                               [ex] --mode pva = Parse v/a streams.\n"
        "                                    --mode da  = Demux audio only.\n"
        "       --api-type              Specify api type. [0-2]\n"
        "                                   - 0 : Use sequential access.\n"
        "                                   - 1 : Use sample-list access.\n"
        "                                   - 2 : Use sequential access. (Omit parse)\n"
        "                                   - 3 : Use sequential access.\n"
        "                                         (Omit parse, Fast single thread)\n"
        "       --output-mode <integer> Specify output mode. [0-3]\n"
        "                                   - 0 : Display information\n"
        "                                   - 1 : Demux raw data\n"
        "                                   - 2 : Demux PES data\n"
        "                                   - 3 : Demux container data\n"
        "       --output-stream <string>\n"
        "                               Specify output stream type.\n"
        "                                   - v  : video only\n"
        "                                   - a  : audio only\n"
        "                                   - va : video/audio\n"
        "       --demux-mode <integer>  Specify output demux mode. [0-1]\n"
        "                                   - 0 : Sequential read\n"
        "                                   - 1 : Multi-thread read\n"
        "       --delay-type <integer>  Specify audio delay type. [0-3]\n"
        "                                   - 0 : None\n"
        "                                   - 1 : GOP Key frame\n"
        "                                   - 2 : GOP 1st frame\n"
        "                                   - 3 : 1st Video frame (=default)\n"
        "       --debug <integer>       Specify output log level. [1-4]\n"
        "       --log <string>          Specify output file name of log.\n"
        "       --log-silent            Log: Suppress the output to stderr.\n"
        "       --log-output-all        Log: Output all log to both file and stderr.\n"
        "       --read-buffer-size <integer>\n"
        "                               Specify internal buffer size for data reading.\n"
        "       --write-buffer-size <integer>\n"
        "                               Specify internal buffer size for data writing.\n"
        "    -v --version               Display the version information.\n"
        "\n"
    );
}

static struct {
    log_level   log_lv;
    FILE       *msg_out;
    log_mode    mode;
} debug_ctrl = { 0 };

extern void dprintf( log_level level, const char *format, ... )
{
    /* check level. */
    if( debug_ctrl.log_lv < level )
        return;
    /* check mode and output. */
    FILE *msg_out[3] = { debug_ctrl.msg_out, NULL, NULL };
    if( level == LOG_LV_OUTPUT )
        msg_out[0] = stdout;
    else if( level == LOG_LV_PROGRESS )
        msg_out[0] = stderr;
    else
        switch( debug_ctrl.mode )
        {
            case LOG_MODE_SILENT :
                /* log only. */
                if( msg_out[0] == stderr )
                    return;
                break;
            case LOG_MODE_NORMAL :
                /* log and stderr(lv0). */
                if( msg_out[0] != stderr && level == LOG_LV0 )
                    msg_out[1] = stderr;
                break;
            case LOG_MODE_OUTPUT_ALL :
                /* log and stderr. */
                if( msg_out[0] != stderr )
                    msg_out[1] = stderr;
                break;
            default:
                return;
        }
    /* output. */
    va_list argptr;
    va_start( argptr, format );
    for( int i = 0; msg_out[i]; ++i )
        vfprintf( msg_out[i], format, argptr );
    va_end( argptr );
}

static void debug_setup_log_lv( log_level level, FILE *output )
{
    if( level != LOG_LV_KEEP )
        debug_ctrl.log_lv = level;
    if( output )
        debug_ctrl.msg_out = output;
    mpeg_api_setup_log_lv( level, output );
}

static void debug_setup_mode( log_mode mode )
{
    debug_ctrl.mode = mode;
}

static void debug_initialize( void )
{
    debug_setup_log_lv( LOG_LV0, stderr );
    debug_setup_mode( LOG_MODE_NORMAL );
}

static int init_parameter( param_t *p )
{
    if( !p )
        return -1;
    memset( p, 0, sizeof(param_t) );
    p->output_stream       = OUTPUT_STREAM_BOTH_VA;
    p->wrap_around_check_v = TIMESTAMP_WRAP_AROUND_CHECK_VALUE;
    p->delay_type          = MPEG_READER_DEALY_FAST_VIDEO_STREAM;
    p->logfile             = stderr;
    return 0;
}

static void cleanup_parameter( param_t *p )
{
    if( p->logfile && p->logfile != stderr )
        fclose( p->logfile );
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
        else if( !strcasecmp( argv[i], "--api-type" ) )
        {
            int type = atoi( argv[++i] );
            if( 0 <= type && type < USE_MAPI_TYPE_MAX )
                p->api_type = type;
            else if( type < 0 )
                p->api_type = 0;
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
        else if( !strcasecmp( argv[i], "--delay-type" ) )
        {
            mpeg_reader_delay_type delay_type = atoi( argv[++i] );
            if( MPEG_READER_DEALY_NONE <= delay_type && delay_type < MPEG_READER_DEALY_INVALID )
                p->delay_type = delay_type;
        }
        else if( !strcasecmp( argv[i], "--debug" ) )
        {
            log_level lv = atoi( argv[++i] );
            if( lv > LOG_LV_ALL )
                lv = LOG_LV_ALL;
            else if( lv < LOG_LV0 )
                lv = LOG_LV0;
            debug_setup_log_lv( lv, NULL );
        }
        else if( !strcasecmp( argv[i], "--log" ) )
        {
            FILE *log = fopen( argv[++i], "at" );
            if( log )
            {
                if( p->logfile != stderr )
                    fclose( p->logfile );
                p->logfile = log;
                debug_setup_log_lv( LOG_LV_KEEP, p->logfile );
            }
        }
        else if( !strcasecmp( argv[i], "--log-silent" ) )
            debug_setup_mode( LOG_MODE_SILENT );
        else if( !strcasecmp( argv[i], "--log-output-all" ) )
            debug_setup_mode( LOG_MODE_OUTPUT_ALL );
        else if( !strcasecmp( argv[i], "--read-buffer-size" ) )
        {
            int64_t size = atoi( argv[++i] );
            if( READ_BUFFER_SIZE_MIN <= size && size <= READ_BUFFER_SIZE_MAX )
                p->read_buffer_size = size;
        }
        else if( !strcasecmp( argv[i], "--write-buffer-size" ) )
        {
            int64_t size = atoi( argv[++i] );
            if( WRITE_BUFFER_SIZE_MIN <= size && size <= WRITE_BUFFER_SIZE_MAX )
                p->write_buffer_size = size;
        }
        else if( !strcasecmp( argv[i], "--mode" ) )
        {
            char *mode = argv[++i];
            output_stream_type output_stream = OUTPUT_STREAM_NONE;
            for( int j = 0; mode[j] != '\0'; ++j )
                switch( tolower(mode[j]) )
                {
                    case 'p' :
                        p->api_type    = USE_MAPI_SEQUENTIAL_READ;
                        p->output_mode = OUTPUT_GET_INFO;
                        break;
                    case 'd' :
                        p->api_type    = USE_MAPI_DEMUX_ALL_ST;
                        p->output_mode = OUTPUT_GET_SAMPLE_RAW;
                        break;
                    case 'v' :
                        output_stream |= OUTPUT_STREAM_VIDEO;
                        p->delay_type  = MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER;
                        break;
                    case 'a' :
                        output_stream |= OUTPUT_STREAM_AUDIO;
                        break;
                    case 'm' :
                        p->api_type   = USE_MAPI_DEMUX_ALL;
                        p->demux_mode = OUTPUT_DEMUX_MULTITHREAD_READ;
                        break;
                    default :
                        break;
                }
            if( output_stream != OUTPUT_STREAM_NONE )
                p->output_stream = output_stream;
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

static inline uint16_t get_output_stream_nums( output_stream_type output, uint8_t v_num, uint8_t a_num )
{
    return ((output & OUTPUT_STREAM_VIDEO) ? v_num : 0)
         + ((output & OUTPUT_STREAM_AUDIO) ? a_num : 0);
}

/*  */
static inline int64_t dumper_ftell( void *fw_ctx )
{
    return file_writer.ftell( fw_ctx );
}

static inline int dumper_fwrite( void *fw_ctx, uint8_t *src_buffer, int64_t src_size, int64_t *dst_size )
{
    return file_writer.fwrite( fw_ctx, src_buffer, src_size, dst_size );
}

static inline int dumper_fseek( void *fw_ctx, int64_t seek_offset, int origin )
{
    return file_writer.fseek( fw_ctx, seek_offset, origin );
}

static int dumper_open( void **fw_ctx, char *file_name, int64_t buffer_size )
{
    void *ctx = NULL;
    if( file_writer.init( &ctx ) )
    {
        dprintf( LOG_LV_PROGRESS, "[log] error, failed to allocate context.\n" );
        return -1;
    }
    if( file_writer.open( ctx, file_name, buffer_size ) )
    {
        dprintf( LOG_LV_PROGRESS, "[log] error, failed to open: %s\n", file_name );
        goto fail;
    }
    *fw_ctx = ctx;
    return 0;
fail:
    file_writer.release( &ctx );
    return -1;
}

static void dumper_close( void **fw_ctx )
{
    if( !fw_ctx || !(*fw_ctx) )
        return;
    file_writer.close( *fw_ctx );
    file_writer.release( fw_ctx );
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

static void dump_stream_info( void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    static const char frame[4] = { '?', 'I', 'P', 'B' };
    for( uint8_t i = 0; i < video_stream_num; ++i )
    {
        dprintf( LOG_LV_OUTPUT, "[log] Video Stream[%3u]\n", i );
        int64_t gop_number = -1;
        for( uint32_t j = 0; ; ++j )
        {
            if( mpeg_api_get_video_frame( info, i, stream_info ) )
                break;
            int64_t pts = stream_info->video_pts;
            int64_t dts = stream_info->video_dts;
            if( stream_info->gop_number < 0 )
            {
                dprintf( LOG_LV_OUTPUT, " [no GOP Picture data]" );
                j = -1;
            }
            else
            {
                if( gop_number < stream_info->gop_number )
                {
                    gop_number = stream_info->gop_number;
                    dprintf( LOG_LV_OUTPUT, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info->progressive_sequence, stream_info->closed_gop );
                }
                dprintf( LOG_LV_OUTPUT, " [%8u]", j );
            }
            dprintf( LOG_LV_OUTPUT, "  pict_struct:%d  order:%2d  [%c]", stream_info->picture_structure, stream_info->temporal_reference, frame[stream_info->picture_coding_type] );
            dprintf( LOG_LV_OUTPUT, "  progr_frame:%d  rff:%d  tff:%d", stream_info->progressive_frame, stream_info->repeat_first_field, stream_info->top_field_first );
            dprintf( LOG_LV_OUTPUT, "  POS: %14"PRId64"", stream_info->file_position );
            dprintf( LOG_LV_OUTPUT, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
            dprintf( LOG_LV_OUTPUT, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
            if( dts != pts )
                dprintf( LOG_LV_OUTPUT, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
            dprintf( LOG_LV_OUTPUT, "\n" );
        }
    }
    for( uint8_t i = 0; i < audio_stream_num; ++i )
    {
        dprintf( LOG_LV_OUTPUT, "[log] Audio Stream[%3u]\n", i );
        for( uint32_t j = 0; ; ++j )
        {
            if( mpeg_api_get_audio_frame( info, i, stream_info ) )
                break;
            int64_t pts = stream_info->audio_pts;
            int64_t dts = stream_info->audio_dts;
            char mapping_info[8];
            get_speaker_mapping_info( stream_info->channel, mapping_info );
            dprintf( LOG_LV_OUTPUT, " [%8u]", j );
            dprintf( LOG_LV_OUTPUT, "  %6uHz  %4uKbps  %s channel  layer %1u  %2u bits", stream_info->sampling_frequency, stream_info->bitrate / 1000, mapping_info, stream_info->layer, stream_info->bit_depth );
            dprintf( LOG_LV_OUTPUT, "  POS: %14"PRId64"", stream_info->file_position );
            dprintf( LOG_LV_OUTPUT, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
            dprintf( LOG_LV_OUTPUT, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
            if( dts != pts )
                dprintf( LOG_LV_OUTPUT, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
            dprintf( LOG_LV_OUTPUT, "\n" );
        }
    }
}

static void dump_sample_info( void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    static const char frame[4] = { '?', 'I', 'P', 'B' };
    if( mpeg_api_create_sample_list( info ) )
        return;
    for( uint8_t i = 0; i < video_stream_num; ++i )
    {
        dprintf( LOG_LV_OUTPUT, "[log] Video Stream[%3u]\n", i );
        uint32_t no_gop_picture_num = 0;
        int64_t  gop_number         = -1;
        for( uint32_t j = 0; ; ++j )
        {
            if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, j, stream_info ) )
                break;
            int64_t pts = stream_info->video_pts;
            int64_t dts = stream_info->video_dts;
            if( stream_info->gop_number < 0 )
            {
                dprintf( LOG_LV_OUTPUT, " [no GOP Picture data]" );
                ++no_gop_picture_num;
            }
            else
            {
                if( gop_number < stream_info->gop_number )
                {
                    gop_number = stream_info->gop_number;
                    dprintf( LOG_LV_OUTPUT, " [GOP:%6"PRId64"]  progr_sequence:%d  closed_gop:%d\n", gop_number, stream_info->progressive_sequence, stream_info->closed_gop );
                }
                dprintf( LOG_LV_OUTPUT, " [%8u]", j - no_gop_picture_num );
            }
            dprintf( LOG_LV_OUTPUT, "  pict_struct:%d  order:%2d  [%c]", stream_info->picture_structure, stream_info->temporal_reference, frame[stream_info->picture_coding_type] );
            dprintf( LOG_LV_OUTPUT, "  progr_frame:%d  rff:%d  tff:%d", stream_info->progressive_frame, stream_info->repeat_first_field, stream_info->top_field_first );
            dprintf( LOG_LV_OUTPUT, "  POS: %14"PRId64"", stream_info->file_position );
            dprintf( LOG_LV_OUTPUT, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
            dprintf( LOG_LV_OUTPUT, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
            if( dts != pts )
                dprintf( LOG_LV_OUTPUT, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
            dprintf( LOG_LV_OUTPUT, "\n" );
        }
    }
    for( uint8_t i = 0; i < audio_stream_num; ++i )
    {
        dprintf( LOG_LV_OUTPUT, "[log] Audio Stream[%3u]\n", i );
        for( uint32_t j = 0; ; ++j )
        {
            if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_AUDIO, i, j, stream_info ) )
                break;
            int64_t pts = stream_info->audio_pts;
            int64_t dts = stream_info->audio_dts;
            char mapping_info[8];
            get_speaker_mapping_info( stream_info->channel, mapping_info );
            dprintf( LOG_LV_OUTPUT, " [%8u]", j );
            dprintf( LOG_LV_OUTPUT, "  %6uHz  %4uKbps  %s channel  layer %1u  %2u bits", stream_info->sampling_frequency, stream_info->bitrate / 1000, mapping_info, stream_info->layer, stream_info->bit_depth );
            dprintf( LOG_LV_OUTPUT, "  POS: %14"PRId64"", stream_info->file_position );
            dprintf( LOG_LV_OUTPUT, "  size: %10u  raw_size: %10u", stream_info->sample_size, stream_info->raw_data_size );
            dprintf( LOG_LV_OUTPUT, "  PTS: %10"PRId64" [%8"PRId64"ms]", pts, pts / 90 );
            if( dts != pts )
                dprintf( LOG_LV_OUTPUT, "  DTS: %10"PRId64" [%8"PRId64"ms]", dts, dts / 90 );
            dprintf( LOG_LV_OUTPUT, "\n" );
        }
    }
}

static void open_file( param_t *p, void *info, mpeg_sample_type sample_type, uint8_t total_stream_num, uint8_t stream_number, void **file, char *add_string, int add_str_len )
{
    int                  get_index = p->output_mode - OUTPUT_GET_SAMPLE_RAW;
    get_sample_data_mode get_mode  = get_sample_list[get_index].get_mode;
    static const struct {
        char   *add_name;
        char   *add_ext;
    } stream_type[2] =
        {
            { "v", ".video" },
            { "a", ".audio" }
        };
    int type_index = (sample_type == SAMPLE_TYPE_VIDEO) ? 0 : 1;
    size_t dump_name_size = strlen( p->output ) + 16 + add_str_len;
    char dump_name[dump_name_size];
    strcpy( dump_name, p->output );
    strcat( dump_name, get_sample_list[get_index].ext );
    if( total_stream_num > 1 )
    {
        char stream_name[6];
        sprintf( stream_name, ".%s%u", stream_type[type_index].add_name, stream_number );
        strcat( dump_name, stream_name );
    }
    if( add_string )
        strcat( dump_name, add_string );
    if( get_mode == GET_SAMPLE_DATA_RAW )
    {
        const char *raw_ext = mpeg_api_get_sample_file_extension( info, sample_type, stream_number );
        if( raw_ext )
            strcat( dump_name, raw_ext );
        else
            strcat( dump_name, stream_type[type_index].add_ext );
    }
    else
        strcat( dump_name, stream_type[type_index].add_ext );
    dumper_open( file, dump_name, p->write_buffer_size );
}

static void open_file_for_list_api( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num, void **video, void **audio )
{
    memset( video, 0, sizeof(void *) * (video_stream_num + 1) );
    memset( audio, 0, sizeof(void *) * (audio_stream_num + 1) );
    /* open files. */
    int64_t video_pts = -1;
    for( uint8_t i = 0; i < video_stream_num; ++i )
    {
        /* check sample num. */
        uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
        dprintf( LOG_LV_PROGRESS, "[log] Video Stream[%3u]  sample_num:%8u\n", i, sample_num );
        if( sample_num && (p->output_stream & OUTPUT_STREAM_VIDEO) )
            /* open output file. */
            open_file( p, info, SAMPLE_TYPE_VIDEO, video_stream_num, i, &(video[i]), NULL, 0 );
        if( video_pts < 0 && p->delay_type != MPEG_READER_DEALY_NONE )
        {
            /* get video stream info. */
            if( !mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, 0, stream_info ) )
            {
                int64_t video_1st_pts = stream_info->video_pts;
                int64_t video_key_pts = (stream_info->picture_coding_type == MPEG_VIDEO_I_FRAME) ? stream_info->video_pts : -1;
                uint32_t j = 0;
                while( stream_info->temporal_reference || video_key_pts < 0 )
                {
                    ++j;
                    if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, j, stream_info ) )
                        break;
                    if( video_1st_pts > stream_info->video_pts && video_1st_pts < stream_info->video_pts + p->wrap_around_check_v )
                        video_1st_pts = stream_info->video_pts;
                    if( video_key_pts < 0 && stream_info->picture_coding_type == MPEG_VIDEO_I_FRAME )
                        video_key_pts = stream_info->video_pts;
                }
                switch( p->delay_type )
                {
                    case MPEG_READER_DEALY_VIDEO_GOP_KEYFRAME :
                        video_pts = video_key_pts;
                        break;
                    case MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER :
                        video_pts = stream_info->video_pts;
                        break;
                    case MPEG_READER_DEALY_FAST_VIDEO_STREAM :
                    case MPEG_READER_DEALY_FAST_STREAM :
                        video_pts = video_1st_pts;
                        break;
                    default :
                        break;
                }
                dprintf( LOG_LV_PROGRESS, "[log] video_pts: %"PRId64" [%"PRId64"ms]\n", video_pts, video_pts / 90 );
            }
        }
    }
    for( uint8_t i = 0; i < audio_stream_num; ++i )
    {
        /* check sample num. */
        uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
        dprintf( LOG_LV_PROGRESS, "[log] Audio Stream[%3u]  sample_num:%8u\n", i, sample_num );
        if( sample_num && (p->output_stream & OUTPUT_STREAM_AUDIO) )
        {
            /* calculate audio delay value. */
            int64_t audio_delay = 0;
            if( video_pts >= 0 )
            {
                if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_AUDIO, i, 0, stream_info ) )
                    continue;
                int64_t audio_pts = stream_info->audio_pts;
                audio_delay = audio_pts - video_pts;
                if( llabs(audio_delay) > p->wrap_around_check_v )
                    audio_delay += MPEG_TIMESTAMP_WRAPAROUND_VALUE * ((audio_delay) > 0 ? -1 : 1);
                dprintf( LOG_LV_PROGRESS, "[log] audio_pts: %"PRId64" [%"PRId64"ms]\n", audio_pts, audio_pts / 90 );
                dprintf( LOG_LV_PROGRESS, "[log] audio_delay: %"PRId64"ms\n", audio_delay / 90 );
            }
            /* open output file. */
            char delay_string[256];
            int delay_str_len = 0;
            if( audio_delay )
            {
                sprintf( delay_string, ". DELAY %"PRId64"ms", audio_delay / 90 );
                delay_str_len = strlen( delay_string );
            }
            open_file( p, info, SAMPLE_TYPE_AUDIO, audio_stream_num, i, &(audio[i]), audio_delay ? delay_string : NULL, delay_str_len );
        }
    }
}

static void open_file_for_stream_api( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num, void **video, void **audio )
{
    memset( video, 0, sizeof(void *) * (video_stream_num + 1) );
    memset( audio, 0, sizeof(void *) * (audio_stream_num + 1) );
    /* open files. */
    int64_t video_pts = -1;
    for( uint8_t i = 0; i < video_stream_num; ++i )
    {
        if( p->output_stream & OUTPUT_STREAM_VIDEO )
            /* open output file. */
            open_file( p, info, SAMPLE_TYPE_VIDEO, video_stream_num, i, &(video[i]), NULL, 0 );
        if( (p->output_stream & OUTPUT_STREAM_AUDIO) && audio_stream_num
         && video_pts < 0 && p->delay_type != MPEG_READER_DEALY_NONE )
        {
            /* get video stream info. */
            if( mpeg_api_get_video_frame( info, i, stream_info ) )
                continue;
            int64_t gop_number     = stream_info->gop_number;
            int64_t start_position = stream_info->file_position;
            int64_t video_1st_pts  = stream_info->video_pts;
            int64_t video_key_pts  = (stream_info->picture_coding_type == MPEG_VIDEO_I_FRAME) ? stream_info->video_pts : -1;
            while( stream_info->temporal_reference || video_key_pts < 0 )
            {
                if( mpeg_api_get_video_frame( info, i, stream_info ) )
                    break;
                if( video_1st_pts > stream_info->video_pts && video_1st_pts < stream_info->video_pts + p->wrap_around_check_v )
                    video_1st_pts = stream_info->video_pts;
                if( video_key_pts < 0 && stream_info->picture_coding_type == MPEG_VIDEO_I_FRAME )
                    video_key_pts = stream_info->video_pts;
                if( gop_number < stream_info->gop_number )
                {
                    gop_number = stream_info->gop_number;
                    start_position = stream_info->file_position;
                }
            }
            switch( p->delay_type )
            {
                case MPEG_READER_DEALY_VIDEO_GOP_KEYFRAME :
                    video_pts = video_key_pts;
                    break;
                case MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER :
                    video_pts = stream_info->video_pts;
                    break;
                case MPEG_READER_DEALY_FAST_VIDEO_STREAM :
                case MPEG_READER_DEALY_FAST_STREAM :
                    video_pts = video_1st_pts;
                    break;
                default :
                    break;
            }
            mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, start_position );
            dprintf( LOG_LV_PROGRESS, "[log] video_pts: %"PRId64" [%"PRId64"ms]\n", video_pts, video_pts / 90 );
        }
    }
    for( uint8_t i = 0; i < audio_stream_num; ++i )
    {
        if( p->output_stream & OUTPUT_STREAM_AUDIO )
        {
            /* calculate audio delay value. */
            int64_t audio_delay = 0;
            if( video_pts >= 0 )
            {
                if( mpeg_api_get_audio_frame( info, i, stream_info ) )
                    continue;
                mpeg_api_set_sample_position( info, SAMPLE_TYPE_AUDIO, i, stream_info->file_position );
                int64_t audio_pts = stream_info->audio_pts;
                audio_delay = audio_pts - video_pts;
                if( llabs(audio_delay) > p->wrap_around_check_v )
                    audio_delay += MPEG_TIMESTAMP_WRAPAROUND_VALUE * ((audio_delay) > 0 ? -1 : 1);
                dprintf( LOG_LV_PROGRESS, "[log] audio_pts: %"PRId64" [%"PRId64"ms]\n", audio_pts, audio_pts / 90 );
                dprintf( LOG_LV_PROGRESS, "[log] audio_delay: %"PRId64"ms\n", audio_delay / 90 );
            }
            /* open output file. */
            char delay_string[256];
            int delay_str_len = 0;
            if( audio_delay )
            {
                sprintf( delay_string, ". DELAY %"PRId64"ms", audio_delay / 90 );
                delay_str_len = strlen( delay_string );
            }
            open_file( p, info, SAMPLE_TYPE_AUDIO, audio_stream_num, i, &(audio[i]), audio_delay ? delay_string : NULL, delay_str_len );
        }
    }
}

typedef struct {
    void                   *api_info;
    mpeg_sample_type        get_type;
    get_sample_data_mode    get_mode;
    void                   *fw_ctx;
    char                   *stream_name;
    uint8_t                 stream_number;
    uint32_t                sample_num;
    uint32_t                start_number;
    int64_t                 file_size;
} demux_param_t;

static thread_func_ret demux_sample( void *args )
{
    demux_param_t *param = (demux_param_t *)args;
    if( !param )
        return (thread_func_ret)(-1);
    void                 *info          = param->api_info;
    mpeg_sample_type      get_type      = param->get_type;
    get_sample_data_mode  mode          = param->get_mode;
    void                 *fw_ctx        = param->fw_ctx;
    char                 *stream_name   = param->stream_name;
    uint8_t               stream_number = param->stream_number;
    uint32_t              num           = param->sample_num;
    uint32_t              start         = param->start_number;
    int64_t               file_size     = param->file_size;
    /* demux */
    uint64_t total_size = 0;
    dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [demux] start - sample_num:%u  start_num:%u\n", stream_name, stream_number, num, start );
    for( uint32_t i = start; i < num; ++i )
    {
        uint8_t  *buffer    = NULL;
        uint32_t  data_size = 0;
        if( mpeg_api_get_sample_data( info, get_type, stream_number, i, &buffer, &data_size, mode ) )
            break;
        else if( buffer && data_size )
        {
            dumper_fwrite( fw_ctx, buffer, data_size, NULL );
            mpeg_api_free_sample_buffer( info, &buffer );
            total_size += data_size;
        }
        int64_t progress = mpeg_api_get_sample_position( info, get_type, stream_number );
        dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [%8u]  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                , stream_name, stream_number, i, total_size, (progress * 100.0 / file_size) );
    }
    dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
    dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [demux] end - output: %"PRIu64" byte\n", stream_name, stream_number, total_size );
    return (thread_func_ret)(0);
}

static void demux_sample_data( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    int                  get_index = p->output_mode - OUTPUT_GET_SAMPLE_RAW;
    get_sample_data_mode get_mode  = get_sample_list[get_index].get_mode;
    if( mpeg_api_create_sample_list( info ) )
        return;
    /* ready file. */
    void *video[video_stream_num + 1], *audio[audio_stream_num + 1];
    open_file_for_list_api( p, info, stream_info, video_stream_num, audio_stream_num, video, audio );
    /* output. */
    dprintf( LOG_LV_PROGRESS, "[log] Demux - START\n" );
    uint16_t total_stream_num = get_output_stream_nums( p->output_stream, video_stream_num, audio_stream_num );
    if( p->demux_mode == OUTPUT_DEMUX_MULTITHREAD_READ && total_stream_num > 1 )
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Multi thread\n" );
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
                    uint32_t start_number = 0;
                    while( 1 )
                    {
                        if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, start_number, stream_info ) )
                            break;
                        if( stream_info->gop_number >= 0 )
                            break;
                        ++start_number;
                    }
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_VIDEO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = video[i];
                    param[thread_index].stream_name   = "Video";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
                    param[thread_index].start_number  = start_number;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_sample, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* audio. */
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                if( audio[i] )
                {
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_AUDIO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = audio[i];
                    param[thread_index].stream_name   = "Audio";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
                    param[thread_index].start_number  = 0;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_sample, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* wait demux end. */
            if( thread_index )
                for( uint16_t i = 0; i < thread_index; ++i )
                    thread_wait_end( demux_thread[i], NULL );
            free( param );
        }
        /* close output file. */
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( video[i] )
                dumper_close( &(video[i]) );
        for( uint8_t i = 0; i < audio_stream_num; ++i )
            if( audio[i] )
                dumper_close( &(audio[i]) );
    }
    else
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Sequential read\n" );
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( video[i] )
            {
                uint64_t total_size = 0;
                uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_VIDEO, i );
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] start - sample_num:%u\n", i, sample_num );
                for( uint32_t j = 0; j < sample_num; ++j )
                {
                    while( 1 )
                    {
                        if( mpeg_api_get_sample_info( info, SAMPLE_TYPE_VIDEO, i, j, stream_info ) )
                            break;
                        if( stream_info->gop_number >= 0 )
                            break;
                        ++j;
                    }
                    uint8_t  *buffer    = NULL;
                    uint32_t  data_size = 0;
                    if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_VIDEO, i, j, &buffer, &data_size, get_mode ) )
                        break;
                    if( buffer && data_size )
                    {
                        dumper_fwrite( video[i], buffer, data_size, NULL );
                        mpeg_api_free_sample_buffer( info, &buffer );
                        total_size += data_size;
                    }
                    int64_t progress = mpeg_api_get_sample_position( info, SAMPLE_TYPE_VIDEO, i );
                    dprintf( LOG_LV_PROGRESS, " [%8u]  size: %10u  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                            , j, data_size, total_size, (progress * 100.0 / p->file_size) );
                }
                dumper_close( &(video[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            if( audio[i] )
            {
                uint64_t total_size = 0;
                uint32_t sample_num = mpeg_api_get_sample_num( info, SAMPLE_TYPE_AUDIO, i );
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] start - sample_num:%u\n", i, sample_num );
                for( uint32_t j = 0; j < sample_num; ++j )
                {
                    uint8_t  *buffer    = NULL;
                    uint32_t  data_size = 0;
                    if( mpeg_api_get_sample_data( info, SAMPLE_TYPE_AUDIO, i, j, &buffer, &data_size, get_mode ) )
                        break;
                    if( buffer && data_size )
                    {
                        dumper_fwrite( audio[i], buffer, data_size, NULL );
                        mpeg_api_free_sample_buffer( info, &buffer );
                        total_size += data_size;
                    }
                    int64_t progress = mpeg_api_get_sample_position( info, SAMPLE_TYPE_AUDIO, i );
                    dprintf( LOG_LV_PROGRESS, " [%8u]  size: %10u  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                            , j, data_size, total_size, (progress * 100.0 / p->file_size) );
                }
                dumper_close( &(audio[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
    }
    dprintf( LOG_LV_PROGRESS, "[log] Demux - END\n" );
}

static thread_func_ret demux_stream( void *args )
{
    demux_param_t *param = (demux_param_t *)args;
    if( !param )
        return (thread_func_ret)(-1);
    void                 *info          = param->api_info;
    mpeg_sample_type      get_type      = param->get_type;
    get_sample_data_mode  mode          = param->get_mode;
    void                 *fw_ctx        = param->fw_ctx;
    char                 *stream_name   = param->stream_name;
    uint8_t               stream_number = param->stream_number;
    int64_t               file_size     = param->file_size;
    /* demux */
    uint64_t total_size = 0;
    dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [demux] start\n", stream_name, stream_number );
    for( uint32_t i = 0; ; ++i )
    {
        uint8_t  *buffer    = NULL;
        uint32_t  data_size = 0;
        if( mpeg_api_get_stream_data( info, get_type, stream_number, &buffer, &data_size, mode ) )
            break;
        else if( buffer && data_size )
        {
            dumper_fwrite( fw_ctx, buffer, data_size, NULL );
            mpeg_api_free_sample_buffer( info, &buffer );
            total_size += data_size;
        }
        int64_t progress = mpeg_api_get_sample_position( info, get_type, stream_number );
        dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [%8u]  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                , stream_name, stream_number, i, total_size, (progress * 100.0 / file_size) );
    }
    dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
    dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [demux] end - output: %"PRIu64" byte\n", stream_name, stream_number, total_size );
    return (thread_func_ret)(0);
}

static void demux_stream_data( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    int                  get_index = p->output_mode - OUTPUT_GET_SAMPLE_RAW;
    get_sample_data_mode get_mode  = get_sample_list[get_index].get_mode;
    /* ready file. */
    void *video[video_stream_num + 1], *audio[audio_stream_num + 1];
    open_file_for_stream_api( p, info, stream_info, video_stream_num, audio_stream_num, video, audio );
    /* output. */
    dprintf( LOG_LV_PROGRESS, "[log] Demux - START\n" );
    uint16_t total_stream_num = get_output_stream_nums( p->output_stream, video_stream_num, audio_stream_num );
    if( p->demux_mode == OUTPUT_DEMUX_MULTITHREAD_READ && total_stream_num > 1 )
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Multi thread\n" );
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
                    while( 1 )
                    {
                        if( mpeg_api_get_video_frame( info, i, stream_info ) )
                            break;
                        if( stream_info->gop_number >= 0 )
                        {
                            mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, stream_info->file_position );
                            dprintf( LOG_LV_PROGRESS, "[log] Video POS: %"PRId64"\n", stream_info->file_position );
                            break;
                        }
                    }
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_VIDEO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = video[i];
                    param[thread_index].stream_name   = "Video";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = 0;
                    param[thread_index].start_number  = 0;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_stream, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* audio. */
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                if( audio[i] )
                {
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_AUDIO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = audio[i];
                    param[thread_index].stream_name   = "Audio";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = 0;
                    param[thread_index].start_number  = 0;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_stream, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* wait demux end. */
            if( thread_index )
                for( uint16_t i = 0; i < thread_index; ++i )
                    thread_wait_end( demux_thread[i], NULL );
            free( param );
        }
        /* close output file. */
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( video[i] )
                dumper_close( &(video[i]) );
        for( uint8_t i = 0; i < audio_stream_num; ++i )
            if( audio[i] )
                dumper_close( &(audio[i]) );
    }
    else
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Sequential read\n" );
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( video[i] )
            {
                uint64_t total_size = 0;
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] start\n", i );
                while( 1 )
                {
                    if( mpeg_api_get_video_frame( info, i, stream_info ) )
                        break;
                    if( stream_info->gop_number >= 0 )
                    {
                        mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, stream_info->file_position );
                        dprintf( LOG_LV_PROGRESS, "[log] Video POS: %"PRId64"\n", stream_info->file_position );
                        break;
                    }
                }
                for( uint32_t j = 0; ; ++j )
                {
                    uint8_t  *buffer    = NULL;
                    uint32_t  data_size = 0;
                    if( mpeg_api_get_stream_data( info, SAMPLE_TYPE_VIDEO, i, &buffer, &data_size, get_mode ) )
                        break;
                    if( buffer && data_size )
                    {
                        dumper_fwrite( video[i], buffer, data_size, NULL );
                        mpeg_api_free_sample_buffer( info, &buffer );
                        total_size += data_size;
                    }
                    int64_t progress = mpeg_api_get_sample_position( info, SAMPLE_TYPE_VIDEO, i );
                    dprintf( LOG_LV_PROGRESS, " [%8u]  size: %10u  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                            , j, data_size, total_size, (progress * 100.0 / p->file_size) );
                }
                dumper_close( &(video[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            if( audio[i] )
            {
                uint64_t total_size = 0;
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] start\n", i );
                for( uint32_t j = 0; ; ++j )
                {
                    uint8_t  *buffer    = NULL;
                    uint32_t  data_size = 0;
                    if( mpeg_api_get_stream_data( info, SAMPLE_TYPE_AUDIO, i, &buffer, &data_size, get_mode ) )
                        break;
                    if( buffer && data_size )
                    {
                        dumper_fwrite( audio[i], buffer, data_size, NULL );
                        mpeg_api_free_sample_buffer( info, &buffer );
                        total_size += data_size;
                    }
                    int64_t progress = mpeg_api_get_sample_position( info, SAMPLE_TYPE_AUDIO, i );
                    dprintf( LOG_LV_PROGRESS, " [%8u]  size: %10u  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                            , j, data_size, total_size, (progress * 100.0 / p->file_size) );
                }
                dumper_close( &(audio[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
    }
    dprintf( LOG_LV_PROGRESS, "[log] Demux - END\n" );
}

typedef struct {
    void           *fw_ctx;
    char           *stream_name;
    uint8_t         stream_number;
    uint32_t        count;
    uint64_t        total_size;
    int64_t         file_size;
    int             percent;
} demux_cb_param_t;

static void demux_cb_func( void *cb_params, void *cb_ret )
{
    demux_cb_param_t         *param = (demux_cb_param_t         *)cb_params;
    get_stream_data_cb_ret_t *ret   = (get_stream_data_cb_ret_t *)cb_ret;
    /* get return values. */
    uint8_t  *buffer    = ret->buffer;
    uint32_t  read_size = ret->read_size;
    int64_t   progress  = ret->progress;
    /* output. */
    dumper_fwrite( param->fw_ctx, buffer, read_size, NULL );
    param->total_size += read_size;
    int percent = progress * 10000 / param->file_size;
    if( (percent - param->percent) > 0 )
    {
        dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [%8u]  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                , param->stream_name, param->stream_number, param->count, param->total_size, percent / 100.0 );
        param->percent = percent;
    }
    ++ param->count;
}

static thread_func_ret demux_all( void *args )
{
    demux_param_t *param = (demux_param_t *)args;
    if( !param )
        return (thread_func_ret)(-1);
    void                 *info          = param->api_info;
    mpeg_sample_type      get_type      = param->get_type;
    get_sample_data_mode  mode          = param->get_mode;
    void                 *fw_ctx        = param->fw_ctx;
    char                 *stream_name   = param->stream_name;
    uint8_t               stream_number = param->stream_number;
    int64_t               file_size     = param->file_size;
    /* demux */
    dprintf( LOG_LV_PROGRESS, "                                                                              \r"
                              " %s Stream[%3u] [demux] start\n", stream_name, stream_number );
    demux_cb_param_t     cb_params = { fw_ctx, stream_name, stream_number, 0, 0, file_size };
    get_stream_data_cb_t cb        = { demux_cb_func, &cb_params };
    mpeg_api_get_stream_all( info, get_type, stream_number, mode, &cb );
    /* finish. */
    uint64_t total_size = cb_params.total_size;
    dprintf( LOG_LV_PROGRESS, "                                                                              \r"
                              " %s Stream[%3u] [demux] end - output: %"PRIu64" byte\n", stream_name, stream_number, total_size );
    return (thread_func_ret)(0);
}

static void demux_stream_all( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    int                  get_index = p->output_mode - OUTPUT_GET_SAMPLE_RAW;
    get_sample_data_mode get_mode  = get_sample_list[get_index].get_mode;
    /* ready file. */
    void *video[video_stream_num + 1], *audio[audio_stream_num + 1];
    open_file_for_stream_api( p, info, stream_info, video_stream_num, audio_stream_num, video, audio );
    /* output. */
    dprintf( LOG_LV_PROGRESS, "[log] Demux - START\n" );
    uint16_t total_stream_num = get_output_stream_nums( p->output_stream, video_stream_num, audio_stream_num );
    if( p->demux_mode == OUTPUT_DEMUX_MULTITHREAD_READ && total_stream_num > 1 )
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Multi thread\n" );
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
                    while( 1 )
                    {
                        if( mpeg_api_get_video_frame( info, i, stream_info ) )
                            break;
                        if( stream_info->gop_number >= 0 )
                        {
                            mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, stream_info->file_position );
                            dprintf( LOG_LV_PROGRESS, "[log] Video POS: %"PRId64"\n", stream_info->file_position );
                            break;
                        }
                    }
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_VIDEO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = video[i];
                    param[thread_index].stream_name   = "Video";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = 0;
                    param[thread_index].start_number  = 0;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_all, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* audio. */
            for( uint8_t i = 0; i < audio_stream_num; ++i )
            {
                if( audio[i] )
                {
                    param[thread_index].api_info      = info;
                    param[thread_index].get_type      = SAMPLE_TYPE_AUDIO;
                    param[thread_index].get_mode      = get_mode;
                    param[thread_index].fw_ctx        = audio[i];
                    param[thread_index].stream_name   = "Audio";
                    param[thread_index].stream_number = i;
                    param[thread_index].sample_num    = 0;
                    param[thread_index].start_number  = 0;
                    param[thread_index].file_size     = p->file_size;
                    demux_thread[thread_index] = thread_create( demux_all, &param[thread_index] );
                    ++thread_index;
                }
            }
            /* wait demux end. */
            if( thread_index )
                for( uint16_t i = 0; i < thread_index; ++i )
                    thread_wait_end( demux_thread[i], NULL );
            free( param );
        }
        /* close output file. */
        for( uint8_t i = 0; i < video_stream_num; ++i )
            if( video[i] )
                dumper_close( &(video[i]) );
        for( uint8_t i = 0; i < audio_stream_num; ++i )
            if( audio[i] )
                dumper_close( &(audio[i]) );
    }
    else
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Sequential read\n" );
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( video[i] )
            {
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] start\n", i );
                while( 1 )
                {
                    if( mpeg_api_get_video_frame( info, i, stream_info ) )
                        break;
                    if( stream_info->gop_number >= 0 )
                    {
                        mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, stream_info->file_position );
                        dprintf( LOG_LV_PROGRESS, "[log] Video POS: %"PRId64"\n", stream_info->file_position );
                        break;
                    }
                }
                demux_cb_param_t     cb_params = { video[i], "Video", i, 0, 0, p->file_size };
                get_stream_data_cb_t cb        = { demux_cb_func, &cb_params };
                mpeg_api_get_stream_all( info, SAMPLE_TYPE_VIDEO, i, get_mode, &cb );
                uint64_t total_size = cb_params.total_size;
                dumper_close( &(video[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            if( audio[i] )
            {
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] start\n", i );
                demux_cb_param_t     cb_params = { audio[i], "Audio", i, 0, 0, p->file_size };
                get_stream_data_cb_t cb        = { demux_cb_func, &cb_params };
                mpeg_api_get_stream_all( info, SAMPLE_TYPE_AUDIO, i, get_mode, &cb );
                uint64_t total_size = cb_params.total_size;
                dumper_close( &(audio[i]) );
                dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
                dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] end - output: %"PRIu64" byte\n", i, total_size );
            }
        }
    }
    dprintf( LOG_LV_PROGRESS, "[log] Demux - END\n" );
}

typedef struct {
    demux_cb_param_t   *v_cb_param;
    demux_cb_param_t   *a_cb_param;
    uint32_t            count;
    int64_t             file_size;
} demux_all_cb_param_t;

static void demux_all_cb_func( void *cb_params, void *cb_ret )
{
    demux_all_cb_param_t     *param = (demux_all_cb_param_t     *)cb_params;
    get_stream_data_cb_ret_t *ret   = (get_stream_data_cb_ret_t *)cb_ret;
    /* get return values. */
    mpeg_sample_type  sample_type   = ret->sample_type;
    uint8_t           stream_number = ret->stream_number;
    uint8_t          *buffer        = ret->buffer;
    uint32_t          read_size     = ret->read_size;
    uint32_t          read_offset   = ret->read_offset;
    int64_t           progress      = ret->progress;
    /* check the target stream. */
    demux_cb_param_t *cb_p        = NULL;
    char             *stream_name = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        cb_p        = &(param->v_cb_param[stream_number]);
        stream_name = "Video";
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        cb_p        = &(param->a_cb_param[stream_number]);
        stream_name = "Audio";
    }
    else
    {
        ++ param->count;
        return;
    }
    /* output. */
    int64_t total_size = cb_p->total_size;
    int32_t valid_size = read_size - read_offset;
    if( total_size + valid_size > 0 )
    {
        if( cb_p->fw_ctx )
        {
            if( total_size < 0 )
                dumper_fwrite( cb_p->fw_ctx, &(buffer[-total_size]), total_size + valid_size, NULL );
            else
                dumper_fwrite( cb_p->fw_ctx, buffer, valid_size, NULL );
            total_size += valid_size;
        }
        else
            total_size = 0;
    }
    else
    {
        if( valid_size < 0 )
            dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [%8u]  skip: %d byte                               \n"
                                    , stream_name, stream_number, cb_p->count, -valid_size );
        total_size = 0;
    }
    int percent = progress * 10000 / param->file_size;
    if( (percent - cb_p->percent) > 0 )
    {
        dprintf( LOG_LV_PROGRESS, " %s Stream[%3u] [%8u]  total: %14"PRIu64" byte ...[%5.2f%%]\r"
                                , stream_name, stream_number, cb_p->count, total_size, percent / 100.0 );
        cb_p->percent = percent;
    }
    cb_p->total_size += valid_size;
    ++ cb_p->count;
    ++ param->count;
}

static void demux_stream_all_in_st( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num )
{
    int                  get_index = p->output_mode - OUTPUT_GET_SAMPLE_RAW;
    get_sample_data_mode get_mode  = get_sample_list[get_index].get_mode;
    /* ready file. */
    void *video[video_stream_num + 1], *audio[audio_stream_num + 1];
    open_file_for_stream_api( p, info, stream_info, video_stream_num, audio_stream_num, video, audio );
    /* output. */
    dprintf( LOG_LV_PROGRESS, "[log] Demux - START\n" );
    uint16_t total_stream_num = get_output_stream_nums( p->output_stream, video_stream_num, audio_stream_num );
    if( total_stream_num )
    {
        dprintf( LOG_LV_PROGRESS, "[log] Demux - Sequential read [Fast-ST]\n" );
        /* set position. */
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( !video[i] )
                continue;
            while( 1 )
            {
                if( mpeg_api_get_video_frame( info, i, stream_info ) )
                    break;
                if( stream_info->gop_number >= 0 )
                {
                    mpeg_api_set_sample_position( info, SAMPLE_TYPE_VIDEO, i, stream_info->file_position );
                    dprintf( LOG_LV_PROGRESS, "[log] Video POS: %"PRId64"\n", stream_info->file_position );
                    break;
                }
            }
        }
#if 0
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            if( !audio[i] )
                continue;
            /* None. */
        }
#endif
        /* demux. */
        static const char *output_stream_name[4] = { "(none)", "Video", "Audio", "V/A" };
        int                stream_name_index     = ((p->output_stream & OUTPUT_STREAM_VIDEO) ? 1 : 0)
                                                 + ((p->output_stream & OUTPUT_STREAM_AUDIO) ? 2 : 0);
        dprintf( LOG_LV_PROGRESS, " %s Stream [demux] start\n"
                                , output_stream_name[stream_name_index] );
        demux_cb_param_t v_cb_params[video_stream_num + 1];
        demux_cb_param_t a_cb_params[audio_stream_num + 1];
        memset( v_cb_params, 0, sizeof(demux_cb_param_t) * (video_stream_num + 1) );
        memset( a_cb_params, 0, sizeof(demux_cb_param_t) * (audio_stream_num + 1) );
        for( uint8_t i = 0; i < video_stream_num; ++i )
            v_cb_params[i].fw_ctx = video[i];
        for( uint8_t i = 0; i < audio_stream_num; ++i )
            a_cb_params[i].fw_ctx = audio[i];
        demux_all_cb_param_t cb_params = { v_cb_params, a_cb_params, 0, p->file_size };
        get_stream_data_cb_t cb        = { demux_all_cb_func, (void *)&cb_params };
        mpeg_api_get_all_stream_data( info, get_mode, p->output_stream, &cb );
        dprintf( LOG_LV_PROGRESS, "                                                                              \r" );
        for( uint8_t i = 0; i < video_stream_num; ++i )
        {
            if( !video[i] )
                continue;
            dumper_close( &(video[i]) );
            dprintf( LOG_LV_PROGRESS, " Video Stream[%3u] [demux] done - output: %"PRIu64" byte\n"
                                    , i, v_cb_params[i].total_size );
        }
        for( uint8_t i = 0; i < audio_stream_num; ++i )
        {
            if( !audio[i] )
                continue;
            dumper_close( &(audio[i]) );
            dprintf( LOG_LV_PROGRESS, " Audio Stream[%3u] [demux] done - output: %"PRIu64" byte\n"
                                    , i, a_cb_params[i].total_size );
        }
        dprintf( LOG_LV_PROGRESS, " %s Stream [demux] end\n", output_stream_name[stream_name_index] );
    }
    dprintf( LOG_LV_PROGRESS, "[log] Demux - END\n" );
}

static void parse_mpeg( param_t *p )
{
    if( !p || !p->input )
        return;
    /* parse. */
    void *info = mpeg_api_initialize_info( p->input, p->read_buffer_size );
    if( !info )
        return;
    stream_info_t *stream_info = malloc( sizeof(*stream_info) );
    if( !stream_info )
        goto end_parse;
    if( p->pmt_program_id )
        if( 0 > mpeg_api_set_pmt_program_id( info, p->pmt_program_id ) )
            goto end_parse;
    int parse_result = mpeg_api_parse( info );
    if( !parse_result )
    {
        /* check file size. */
        p->file_size = get_file_size( p->input );
        /* check PCR. */
        int64_t pcr = mpeg_api_get_pcr( info );
        if( pcr >= 0 )
            dprintf( LOG_LV_OUTPUT, "[log] pcr: %10"PRId64"  [%8"PRId64"ms]\n", pcr, pcr / 90 );
        /* check Video and Audio information. */
        uint8_t video_stream_num = mpeg_api_get_stream_num( info, SAMPLE_TYPE_VIDEO );
        uint8_t audio_stream_num = mpeg_api_get_stream_num( info, SAMPLE_TYPE_AUDIO );
        dprintf( LOG_LV_OUTPUT, "[log] stream_num:  Video:%4u  Audio:%4u\n", video_stream_num, audio_stream_num );
        if( !video_stream_num && !audio_stream_num )
            goto end_parse;
        /* output. */
        static const struct {
            void (*dump )( void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num );
            void (*demux)( param_t *p, void *info, stream_info_t *stream_info, uint8_t video_stream_num, uint8_t audio_stream_num );
        } output_func[USE_MAPI_TYPE_MAX] =
            {
                { dump_stream_info, demux_stream_data      },
                { dump_sample_info, demux_sample_data      },
                { dump_stream_info, demux_stream_all       },
                { dump_stream_info, demux_stream_all_in_st }
            };
        switch( p->output_mode )
        {
            case OUTPUT_GET_INFO :
                output_func[p->api_type].dump( info, stream_info, video_stream_num, audio_stream_num );
                break;
            case OUTPUT_GET_SAMPLE_RAW :
            case OUTPUT_GET_SAMPLE_PES :
            case OUTPUT_GET_SAMPLE_CONTAINER :
                output_func[p->api_type].demux( p, info, stream_info, video_stream_num, audio_stream_num );
                break;
            default :
                dprintf( LOG_LV0, "[log] Specified output mode is invalid value...\n" );
                break;
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

static int check_commandline( int argc, char *argv[] )
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
    int i = 1;
    while( i < argc )
    {
        param_t param;
        if( init_parameter( &param ) )
            return -1;
        debug_initialize();
        i = parse_commandline( argc, argv, i, &param );
        if( i < 0 )
            break;
        if( !correct_parameter( &param ) )
            parse_mpeg( &param );
        cleanup_parameter( &param );
    }
    return 0;
}
