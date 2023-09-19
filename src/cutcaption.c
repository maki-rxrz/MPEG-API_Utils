/*****************************************************************************
 * cutcaption.c
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
#include <stdarg.h>

#include "config.h"

#include "utils_def.h"
#include "text_utils.h"
#include "d2v_parser.h"
#include "mpeg_utils.h"

#define PROGRAM_VERSION                 "1.2.4"

#ifndef REVISION_NUMBER
#define REVISION_NUMBER                 "0"
#endif

#define INPUT_EXT_MAX                   (4)
#define CAPTION_TYPE_MAX                (2)

#define UTF8_BOM                        "\xEF\xBB\xBF"
#define UTF8_BOM_SIZE                   (3)

#define DEFAULT_FPS_NUM                 (30000)
#define DEFAULT_FPS_DEN                 (1001)
#define DEFAULT_LINE_MAX                (512)

#define TIMESTAMP_WRAP_AROUND_CHECK_VALUE       (450000)        /* 5sec x 90kHz */

typedef enum {
    OUTPUT_NONE = 0x00,
    OUTPUT_ASS  = 0x01,
    OUTPUT_SRT  = 0x10,
    OUTPUT_DUAL = 0x11              /* default */
} output_mode_type;

typedef enum {
    ASPECT_RATIO_DEFAULT = 0,
    ASPECT_RATIO_SQUARE  = 1,
    ASPECT_RATIO_WIDE    = 2,
    ASPECT_RATIO_CINEMA  = 3
} aspect_ratio_type;

typedef struct {
    uint32_t            num;
    uint32_t            den;
} frame_rate_t;

typedef enum {
    EXECUTE_CUT_CAPTION    = 0,
    EXECUTE_ANALYZE_MPEGTS = 1
} execute_mode_type;

typedef struct {
    /* common */
    UTILS_COMMON_PARAMTER
    /* private */
    char               *input;
    char               *output;
    int                 output_no_overwrite;
    output_mode_type    output_mode;
    uint16_t            service_id;
    uint16_t            pmt_program_id;
    pmt_target_type     pmt_target;
    frame_rate_t        frame_rate;
    int64_t             wrap_around_check_v;
    aspect_ratio_type   aspect_ratio;
    int32_t             shift_pos_x;
    int32_t             shift_pos_y;
    FILE               *logfile;
    execute_mode_type   execute_mode;
    int                 ext_correction;
} param_t;

typedef void (*cut_caption_func)( param_t *p, FILE *input, FILE *output );
static void cut_ass( param_t *p, FILE *input, FILE *output );
static void cut_srt( param_t *p, FILE *input, FILE *output );

typedef void (*correct_func)( param_t *p );
static void correct_d2v_input( param_t *p );

static const struct {
    char               *ext;
    output_mode_type    mode;
    cut_caption_func    cut_func;
    correct_func        correct_func;
} input_array[INPUT_EXT_MAX] =
    {
        { ".ass", OUTPUT_ASS , cut_ass, NULL              },
        { ".srt", OUTPUT_SRT , cut_srt, NULL              },
        { ".ts" , OUTPUT_DUAL, NULL   , NULL              },
        { ".d2v", OUTPUT_DUAL, NULL   , correct_d2v_input }
    };

static const struct {
    char                   *type;
    mpeg_reader_type        reader;
    mpeg_reader_delay_type  delay_type;
} reader_array[READER_TYPE_MAX] =
    {
        {  "m2vvfp"  , MPEG_READER_M2VVFAPI, MPEG_READER_DEALY_VIDEO_GOP_KEYFRAME },
        {  "dgdecode", MPEG_READER_DGDECODE, MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER },
        {  "libav"   , MPEG_READER_LIBAV   , MPEG_READER_DEALY_FAST_VIDEO_STREAM  },
        {  "tmpgenc" , MPEG_READER_TMPGENC , MPEG_READER_DEALY_FAST_STREAM        }
    };

static void print_version( void )
{
    fprintf( stdout,
        "CutCaption version " PROGRAM_VERSION "." REVISION_NUMBER "\n"
    );
}

static void print_help( void )
{
    fprintf( stdout,
        "\n"
        "CutCaption version " PROGRAM_VERSION "." REVISION_NUMBER "\n"
        "\n"
        "usage:  cutcaption [options] <input>\n"
        "\n"
        "options:\n"
        "    -o --output <string>       Specify output file name.\n"
        "       --force-output          Overwrite the existing output file.\n"
        "    -l --list <string>         Specify cut list name.\n"
        "                                   - txt, avs, vcf, keyframe\n"
        "       --key-type <integer>    Set keyframe list type. [1-3]\n"
        "                                   - 1     Cutting odd frames range.\n"
        "                                   - 2     Cutting even frames range.\n"
        "                                   - 3     Cutting range out.\n"
        "       --search-word <string>  Specify search words in scripts.\n"
        "    -r --reader <string>       Specify MPEG-2 TS reader.\n"
        "                               Check the delay time between the PCR-PTS.\n"
        "                                   - m, m2vvfp     MPEG-2 VIDEO VFAPI Plug-In.\n"
        "                                   - d, dgdecode   DGIndex and DGDecode.\n"
        "                                   - l, libav      Libav reader.\n"
        "                                   - t, tmpgenc    TMPGEnc series.\n"
        "       --no-reader             Disable check of the read delay time.\n"
        "       --sid <integer>         Specify Service ID.\n"
        "       --pmt-pid <integer>     Specify Program Map Table ID.\n"
        "       --pmt-target <integer>  Specify detection target of PMT.\n"
        "               (default: 0)        - 0 : Select maximum of PMT detected\n"
        "                                   - 1 : Select minimum of PMT detected\n"
        "    -d --delay <integer>       Specify delay time.\n"
        "    -f --framerate <int/int>   Specify framerate.       (ex: 30000/1001)\n"
        "       --fps-num <integer>     Specify fps numerator.   (ex: 24000)\n"
        "       --fps-den <integer>     Specify fps denominator. (ex: 1001)\n"
        "    -m --line-max <integer>    The maximum size of one line in a list.\n"
        "       --debug <integer>       Specify output log level. [1-4]\n"
        "       --log <string>          Specify output file name of log.\n"
        "       --log-silent            Log: Suppress the output to stderr.\n"
        "       --log-output-all        Log: Output all log to both file and stderr.\n"
        "       --analyze               Display the analysis information.\n"
        "    -v --version               Display the version information."
        "\n"
        "  [ASS Subtitles only]\n"
        "    -a --aspect-ratio <integer> or <string>\n"
        "                               Specify output video aspect ratio.\n"
        "                                   - 1, 4:3        Normal.\n"
        "                                   - 2, 16:9       Wide.\n"
        "                                   - 3, 2.35:1     CinemaScope.\n"
        "       --shift-posx <integer>  Specify shifts value of the vertical position.\n"
        "       --shift-posy <integer>  Specify shifts value of the horizontal position.\n"
        "\n"
    );
}

static struct {
    log_level   log_lv;
    FILE       *msg_out;
    log_mode    mode;
} debug_ctrl = { 0 };

extern void mapi_log( log_level level, const char *format, ... )
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
        mapi_vfprintf( msg_out[i], format, argptr );
    va_end( argptr );
#ifdef DEBUG
    for( int i = 0; msg_out[i]; ++i )
        fflush( msg_out[i] );
#endif
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

static int file_exists( const char *file )
{
    if( !file )
        return 0;
    FILE *fp = mapi_fopen( file, "rb" );
    if( fp )
    {
        fclose( fp );
        return 1;
    }
    return 0;
}

static FILE *file_ext_open( const char *file, const char *ext, const char *mode )
{
    if( !file || !mode )
        return NULL;
    size_t len     = strlen( file );
    size_t ext_len = ext ? strlen( ext ) : 0;
    char full_name[len + ext_len + 32];
    strcpy( full_name, file );
    if( ext_len )
        strcat( full_name, ext );
    FILE *fp = mapi_fopen( full_name, mode );
    return fp;
}

static int init_parameter( param_t *p )
{
    if( !p )
        return -1;
    memset( p, 0, sizeof(param_t) );
    p->output_no_overwrite = 1;
    p->output_mode         = OUTPUT_DUAL;
    p->delay_time          = 0;
    p->frame_rate.num      = DEFAULT_FPS_NUM;
    p->frame_rate.den      = DEFAULT_FPS_DEN;
    p->line_max            = DEFAULT_LINE_MAX;
    p->wrap_around_check_v = TIMESTAMP_WRAP_AROUND_CHECK_VALUE;
    p->logfile             = stderr;
    return 0;
}

static void cleanup_parameter( param_t *p )
{
    if( p->logfile && p->logfile != stderr )
        fclose( p->logfile );
    if( p->list_data )
        free( p->list_data );
    if( p->list )
        free( p->list );
    if( p->list_search_word )
        free( p->list_search_word );
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
        else if( !strcasecmp( argv[i], "--force-output" ) )
            p->output_no_overwrite = 0;
        else if( !strcasecmp( argv[i], "--list" ) || !strcasecmp( argv[i], "-l" ) )
        {
            if( p->list )
                free( p->list );
            p->list = strdup( argv[++i] );
            if( p->list )
            {
                char *ext = strrchr( p->list, '.' );
                if( ext )
                    text_get_cut_list_type( (common_param_t *)p, ext );
            }
        }
        else if( !strcasecmp( argv[i], "--key-type" ) )
        {
            ++i;
            if( p->list_type == CUT_LIST_KEY_AUTO )
            {
                int order = atoi( argv[i] );
                if( 1 <= order || order <= 3 )
                    p->list_key_type = order;
            }
        }
        else if( !strcasecmp( argv[i], "--search-word" ) )
        {
            if( p->list_search_word )
                free( p->list_search_word );
            p->list_search_word = strdup( argv[++i] );
        }
        else if( !strcasecmp( argv[i], "--reader" ) || !strcasecmp( argv[i], "-r" ) )
        {
            char *c = argv[++i];
            for( int j = 0; j < READER_TYPE_MAX; ++j )
                if( !strcasecmp( c, reader_array[j].type ) || !strncasecmp( c, reader_array[j].type, 1 ) )
                {
                    p->reader = reader_array[j].reader;
                    break;
                }
        }
        else if( !strcasecmp( argv[i], "--no-reader" ) )
            p->reader = MPEG_READER_NONE;
        else if( !strcasecmp( argv[i], "--sid" ) )
        {
            ++i;
            int base = (strncmp( argv[i], "0x", 2 )) ? 10 : 16;
            p->service_id = strtol( argv[i], NULL, base );
        }
        else if( !strcasecmp( argv[i], "--pmt-pid" ) )
        {
            ++i;
            int base = (strncmp( argv[i], "0x", 2 )) ? 10 : 16;
            p->pmt_program_id = strtol( argv[i], NULL, base );
        }
        else if( !strcasecmp( argv[i], "--pmt-target" ) )
            p->pmt_target = atoi( argv[++i] );
        else if( !strcasecmp( argv[i], "--delay" ) || !strcasecmp( argv[i], "-d" ) )
            p->delay_time = atoi( argv[++i] );
        else if( !strcasecmp( argv[i], "--framerate" ) || !strcasecmp( argv[i], "-f" ) )
        {
            char *framerate = strdup( argv[++i] );
            if( framerate )
            {
                char *c = strchr( framerate, '/' );
                if( c )
                {
                    *c = '\0';
                    char *fps_num = framerate;
                    char *fps_den = c + 1;
                    int num = atoi( fps_num );
                    if( num > 0 )
                        p->frame_rate.num = num;
                    num= atoi( fps_den );
                    if( num > 0 )
                        p->frame_rate.den = num;
                }
                free( framerate );
            }
        }
        else if( !strcasecmp( argv[i], "--fps-num" ) )
        {
            int fps_num = atoi( argv[++i] );
            if( fps_num > 0 )
                p->frame_rate.num = fps_num;
        }
        else if( !strcasecmp( argv[i], "--fps-den" ) )
        {
            int fps = atoi( argv[++i] );
            if( fps > 0 )
                p->frame_rate.num = fps;
        }
        else if( !strcasecmp( argv[i], "--line-max" ) || !strcasecmp( argv[i], "-m" ) )
        {
            int max = atoi( argv[++i] );
            if( max > 0 )
                p->line_max = max;
        }
        else if( !strcasecmp( argv[i], "--aspect-ratio" ) || !strcasecmp( argv[i], "-a" ) )
        {
            ++i;
            if( !strcmp( argv[i], "4:3" ) || !strcmp( argv[i], "1" ) )
                p->aspect_ratio = ASPECT_RATIO_SQUARE;
            else if( !strcmp( argv[i], "16:9" ) || !strcmp( argv[i], "2" ) )
                p->aspect_ratio = ASPECT_RATIO_SQUARE;
            else if( !strcmp( argv[i], "2.35:1" ) || !strcmp( argv[i], "3" ) )
                p->aspect_ratio = ASPECT_RATIO_SQUARE;
        }
        else if( !strcasecmp( argv[i], "--shift-posx" ) )
            p->shift_pos_x = atoi( argv[++i] );
        else if( !strcasecmp( argv[i], "--shift-posy" ) )
            p->shift_pos_y = atoi( argv[++i] );
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
            FILE *log = mapi_fopen( argv[++i], "at" );
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
        else if( !strcasecmp( argv[i], "--analyze" ) )
            p->execute_mode = EXECUTE_ANALYZE_MPEGTS;
        else
        {
            /* check invalid parameters. */
            int invalid_nums = 1;
            if( (i + 2) < argc && *argv[i + 1] != '-' && !file_exists( argv[i + 1] ) )
            {
                /* check 'next-argv + input-ext'. */
                size_t argv_len = strlen( argv[i + 1] );
                int    ia_idx   = 0;
                for( ; ia_idx < INPUT_EXT_MAX; ++ia_idx )
                {
                    const char *ext     = input_array[ia_idx].ext;
                    size_t      ext_len = ext ? strlen( ext ) : 0;
                    char full_name[argv_len + ext_len];
                    strcpy( full_name, argv[i + 1] );
                    if( ext_len )
                        strcat( full_name, ext );
                    if( file_exists( full_name ) )
                        break;
                }
                if( ia_idx == INPUT_EXT_MAX )
                    ++invalid_nums;
            }
            if( invalid_nums == 2 )
            {
                mapi_log( LOG_LV0, "[log] invalid paramters: '%s %s'\n", argv[i], argv[i + 1] );
                ++i;
            }
            else
                mapi_log( LOG_LV0, "[log] invalid paramter: '%s'\n", argv[i] );
        }
        ++i;
    }
    if( i < argc )
    {
        p->input = strdup( argv[i] );
        if( p->input )
        {
            char *ext = strrchr( p->input, '.' );
            if( ext )
            {
                for( int i = 0; i < INPUT_EXT_MAX; ++i )
                    if( !strcasecmp( ext, input_array[i].ext ) )
                    {
                        if( input_array[i].correct_func )
                        {
                            input_array[i].correct_func( p );
                            ext = strrchr( p->input, '.' );
                            if( !ext )
                                ext = p->input + strlen( p->input );
                        }
                        p->output_mode    = input_array[i].mode;
                        p->ext_correction = 1;
                        *ext              = '\0';
                        break;
                    }
            }
        }
        ++i;
    }
    return i;
}

static int correct_parameter( param_t *p )
{
    if( !p || !p->input )
        return -1;
    /* input. */
    output_mode_type mode = OUTPUT_NONE;
    for( int i = 0; i < CAPTION_TYPE_MAX; ++i )
    {
        /* check if exist input file. */
        FILE *input = file_ext_open( p->input, input_array[i].ext, "rb" );
        if( input )
        {
            fclose( input );
            mode |= input_array[i].mode;
        }
    }
    if( mode == OUTPUT_NONE )
    {
        mapi_log( LOG_LV0, "[log] don't exist input file. input:%s\n", p->input );
        return -1;
    }
    p->output_mode &= mode;
    /* output. */
    if( !p->output )
    {
        /* correct by "input" + "_new". */
        size_t len = strlen( p->input );
        p->output = (char *)malloc( len + 5 );
        if( !p->output )
        {
            mapi_log( LOG_LV0, "[log] malloc error.\n" );
            return -1;
        }
        strcpy( p->output, p->input );
        strcat( p->output, "_new" );
    }
    else if( !strcasecmp( p->output, p->input ) )
    {
        /* correct by adding "_new". */
        size_t len = strlen( p->input );
        char *tmp = (char *)realloc( p->output, len + 5 );
        if( !tmp )
        {
            mapi_log( LOG_LV0, "[log] realloc error.\n" );
            return -1;
        }
        p->output = tmp;
        strcat( p->output, "_new" );
    }
    /* check if exist output file. */
    if( p->output_no_overwrite )
        for( int i = 0; i < CAPTION_TYPE_MAX; ++i )
        {
            if( !(p->output_mode & input_array[i].mode) )
                continue;
            FILE *output = file_ext_open( p->output, input_array[i].ext, "rt" );
            if( output )
            {
                fclose( output );
                p->output_mode &= ~input_array[i].mode;
            }
        }
    if( p->output_mode == OUTPUT_NONE )
    {
        mapi_log( LOG_LV0, "[log] already exist output file.\n" );
        return 1;
    }
    return 0;
}

static int load_cut_list( param_t *p )
{
    if( !p )
        return -1;
    FILE *list = mapi_fopen( p->list, "rt" );
    if( !list )
    {
        /* check user specified delay time. */
        if( p->delay_time && (p->list_data = (cut_list_data_t *)malloc( sizeof(cut_list_data_t) )) )
        {
            p->reader = MPEG_READER_NONE;
            PUSH_LIST_DATA( p, 0, INT32_MAX - 1 );
            mapi_log( LOG_LV1, "[log] apply delay. delay:%d\n", p->delay_time );
            mapi_log( LOG_LV2, "[list] [%d]  s:%6d  e:%6d\n", 0, p->list_data[0].start, p->list_data[0].end );
            return 0;
        }
        return -1;
    }
    mapi_log( LOG_LV0, "[log] list : %s\n", p->list );
    int result = text_load_cut_list( (common_param_t *)p, list );
    fclose( list );
    for( int i = 0; i < p->list_data_count; ++i )
        mapi_log( LOG_LV1, "[list] [%d]  s:%6d  e:%6d\n", i, p->list_data[i].start, p->list_data[i].end );
    return result;
}

typedef struct {
    uint32_t Hrs;
    uint32_t Mins;
    uint32_t Secs;
    uint32_t Msecs;
} caption_time_t;

static int64_t time_to_total( caption_time_t *t, int floor )
{
    if( floor )
        t->Msecs *= 10;
    int64_t total = ((t->Hrs * 60 + t->Mins) * 60 + t->Secs) * 1000 + t->Msecs;
    return total;
}

static void total_to_time( caption_time_t *t, int64_t total, int round )
{
    mapi_log( LOG_LV4, "[debug] total:%" PRId64 "\n", total );
    if( round )
        total += 5;
    t->Msecs = total % 1000;
    total /= 1000;
    t->Secs  = total % 60;
    total /= 60;
    t->Mins  = total % 60;
    total /= 60;
    t->Hrs   = total;
    if( round )
        t->Msecs /= 10;
}

static double time_to_frame( double time, frame_rate_t frame_rate )
{
    mapi_log( LOG_LV4, "[debug] t2f: %f\n", time );
    return time * frame_rate.num / frame_rate.den / 1000;
}

static double frame_to_time( double frame_num, frame_rate_t frame_rate )
{
    mapi_log( LOG_LV4, "[debug] f2t: %f\n", frame_num * frame_rate.den * 1000 / frame_rate.num );
    return frame_num * frame_rate.den * 1000 / frame_rate.num;
}

static int get_output_times( param_t *p, int64_t *start, int64_t *end )
{
    int64_t cut_frames = 0;
    int64_t cut_start  = 0;
    double s = time_to_frame( *start, p->frame_rate );
    double e = time_to_frame( *end  , p->frame_rate );
    int is_output = 0;
    for( int i = 0; i < p->list_data_count; ++i )
    {
        cut_frames += p->list_data[i].start - cut_start;
        if( s < p->list_data[i].start && e < p->list_data[i].start )
        {
            /* delete. */
            mapi_log( LOG_LV4, "[debug] delete. s:%f e:%f\n", s, e );
            return -1;
        }
        if( s <= p->list_data[i].end + 1 && e >= p->list_data[i].end + 1 )
        {
            /* correct end. */
            e = p->list_data[i].end + 1;
            is_output = 1;
            mapi_log( LOG_LV4, "[debug] correct end time. e:%f cut:%" PRId64 "\n", e, cut_frames );
            break;
        }
        if( s < p->list_data[i].start && e >= p->list_data[i].start && e <= p->list_data[i].end + 1 )
        {
            /* correct start. */
            s = p->list_data[i].start;
            is_output = 1;
            mapi_log( LOG_LV4, "[debug] correct start time. e:%f cut:%" PRId64 "\n", s, cut_frames );
            break;
        }
        if( s < p->list_data[i].start && e >= p->list_data[i].end + 1 )
        {
            /* correct start & end. */
            s = p->list_data[i].start;
            e = p->list_data[i].end + 1;
            is_output = 1;
            mapi_log( LOG_LV4, "[debug] correct times. s:%f e:%f cut:%" PRId64 "\n", s, e, cut_frames );
            break;
        }
        if( i < p->list_data_count - 1
         && s < p->list_data[i].start && e >= p->list_data[i+1].start && e <= p->list_data[i+1].end + 1 )
        {
            /* middle cut. */
            int64_t middle_cut = p->list_data[i + 1].start - p->list_data[i].end + 1;
            s += middle_cut;
            cut_frames += middle_cut;
            is_output = 1;
            mapi_log( LOG_LV4, "[debug] middle cut. s:%f e:%f cut:%" PRId64 "\n", s, e, cut_frames );
            break;
        }
        if( e <= p->list_data[i].end + 1 )
        {
            /* output. */
            mapi_log( LOG_LV4, "[debug] output. cut:%" PRId64 "\n", cut_frames );
            is_output = 1;
            break;
        }
        mapi_log( LOG_LV4, "[debug] --calc-- s:%f e:%f, cut:%" PRId64 ", ls:%d le:%d\n"
                         , s, e, cut_frames, p->list_data[i].start, p->list_data[i].end );
        cut_start = p->list_data[i].end + 1;
    }
    if( !is_output )
        return -1;
    double time_s = frame_to_time( s - cut_frames, p->frame_rate );
    double time_e = frame_to_time( e - cut_frames, p->frame_rate );
    /* check delay. */
    int64_t delay = p->delay_time + p->reader_delay;
    mapi_log( LOG_LV3, "[debug] s:%f e:%f d:%" PRId64 "\n", time_s, time_e, delay );
    time_e += delay;
    if( time_e < 0 )
        return -1;
    time_s += delay;
    if( time_s < 0 )
        time_s = 0;
    /* setup. */
    *start = (int64_t)(time_s + 0.5);
    *end   = (int64_t)(time_e + 0.5);
    mapi_log( LOG_LV2, "[debug] out s:%" PRId64 " e:%" PRId64 "\n", *start, *end );
    return 0;
}

static int ass_header_change_aspect_ratio( param_t *p, FILE *input, FILE *output, int32_t *shift_x, int32_t *shift_y )
{
    /* get ass script info section data. */
    aspect_ratio_type video_aspect_ratio = ASPECT_RATIO_DEFAULT;
    aspect_ratio_type aspect_ratio       = ASPECT_RATIO_DEFAULT;
    int32_t play_res_x = 0, play_res_y = 0;
    char line[p->line_max];
    while( fgets( line, p->line_max, input ) )
    {
        if( !strncmp( line, "[Events]", 8 ) )
            break;
        if( sscanf( line, "Video Aspect Ratio: %d", (int *)&video_aspect_ratio ) )
            continue;
        else if( sscanf( line, "PlayResX: %d", &play_res_x ) )
            continue;
        else if( sscanf( line, "PlayResY: %d", &play_res_y ) )
            continue;
    }
    mapi_log( LOG_LV3, "[debug] PlayResX:%d, PlayResY:%d, Aspect Ratio:%d\n", play_res_x, play_res_y, video_aspect_ratio );
    fseeko( input, 0, SEEK_SET );
    /* check ass script info. */
    if( !play_res_x || !play_res_y || video_aspect_ratio < ASPECT_RATIO_DEFAULT || ASPECT_RATIO_CINEMA < video_aspect_ratio )
        return -1;
    if( video_aspect_ratio == ASPECT_RATIO_DEFAULT )
    {
        /* judge aspect ratio. */
        if( play_res_x * 3 == play_res_y * 4 )
            aspect_ratio = ASPECT_RATIO_SQUARE;
        else if( play_res_x * 9 == play_res_y * 16 )
            aspect_ratio = ASPECT_RATIO_WIDE;
        else if( play_res_x * 1 == play_res_y * 2.35 )
            aspect_ratio = ASPECT_RATIO_CINEMA;
        else
            return -1;
    }
    if( aspect_ratio == p->aspect_ratio )
        return 1;
    /* calculate shift X or Y values. */
    if( aspect_ratio == ASPECT_RATIO_SQUARE )
    {
        if( p->aspect_ratio == ASPECT_RATIO_WIDE )
            *shift_y = -(play_res_y) / 4;
        else if( p->aspect_ratio == ASPECT_RATIO_CINEMA )
            *shift_y = -(play_res_y) * 7 / 16;
    }
    else if( aspect_ratio == ASPECT_RATIO_WIDE )
    {
        if( p->aspect_ratio == ASPECT_RATIO_SQUARE )
            *shift_x = -(play_res_x) / 4;
        else if( p->aspect_ratio == ASPECT_RATIO_CINEMA )
            *shift_y = -(play_res_y) / 4;
    }
    else if( aspect_ratio == ASPECT_RATIO_CINEMA )
    {
        if( p->aspect_ratio == ASPECT_RATIO_SQUARE )
            *shift_y = play_res_y * 7 / 9;
        else if( p->aspect_ratio == ASPECT_RATIO_WIDE )
            *shift_y = play_res_y / 3;
    }
    /* output header. */
    aspect_ratio = (video_aspect_ratio == ASPECT_RATIO_DEFAULT) ? video_aspect_ratio : p->aspect_ratio;
    while( fgets( line, p->line_max, input ) )
    {
        if( !strncmp( line, "[Events]", 8 ) )
        {
            fprintf( output, "%s", line );
            break;
        }
        if( !strncmp( line, "Video Aspect Ratio: ", 20 ) )
            sprintf( line, "Video Aspect Ratio: %d\n", aspect_ratio );
        else if( !strncmp( line, "PlayResX: ", 10 ) )
            sprintf( line, "PlayResX: %d\n", play_res_x + *shift_x );
        else if( !strncmp( line, "PlayResY: ", 10 ) )
            sprintf( line, "PlayResY: %d\n", play_res_y + *shift_y );
        fprintf( output, "%s", line );
    }
    *shift_x /= 2;
    *shift_y /= 2;
    return 0;
}

static void cut_ass( param_t *p, FILE *input, FILE *output )
{
    char line[p->line_max];
    int change_layout = 0;
    int32_t ar_shift_x = 0, ar_shift_y = 0;
    /* parse header. */
    if( p->aspect_ratio != ASPECT_RATIO_DEFAULT )
        change_layout = !(ass_header_change_aspect_ratio( p, input, output, &ar_shift_x, &ar_shift_y ));
    change_layout = (change_layout || p->shift_pos_x || p->shift_pos_y);
    mapi_log( LOG_LV3, "[debug] layout:%d, %d, %d\n", change_layout, ar_shift_x, ar_shift_y );
    /* parse data. */
    while( fgets( line, p->line_max, input ) )
    {
        /* pre process. */
        static const char *diaglogue_format = "Dialogue: %d,";
        int layer;
        if( sscanf( line, diaglogue_format, &layer ) != 1 )
        {
            fprintf( output, "%s", line );
            continue;
        }
        char line_head[p->line_max];
        sprintf( line_head, diaglogue_format, layer );
        /* check times line. */
        static const char *time_format = "%01d:%02d:%02d.%02d,%01d:%02d:%02d.%02d,";
        char *line_p = strchr( line, ',' );
        ++line_p;
        caption_time_t time_s, time_e;
        if( sscanf( line_p, time_format
                  , &time_s.Hrs, &time_s.Mins, &time_s.Secs, &time_s.Msecs
                  , &time_e.Hrs, &time_e.Mins, &time_e.Secs, &time_e.Msecs ) == 8 )
        {
            /* calculate times. */
            int64_t start = time_to_total( &time_s, 1 );
            int64_t end   = time_to_total( &time_e, 1 );
            if( get_output_times( p, &start, &end ) )
                continue;
            /* write head. */
            fprintf( output, "%s", line_head );
            /* write body. */
            total_to_time( &time_s, start, 1 );
            total_to_time( &time_e, end  , 1 );
            fprintf( output, time_format
                   , time_s.Hrs, time_s.Mins, time_s.Secs, time_s.Msecs
                   , time_e.Hrs, time_e.Mins, time_e.Secs, time_e.Msecs );
            /* prepare for post process. */
            /* 22: line size. "0:00:00.00,0:00:00.00," */
            line_p += 22;
            /* change caption layout. */
            if( change_layout )
            {
                char *ass_pos_p = strstr( line_p, "\\pos(" );
                if( ass_pos_p )
                {
                    static const char *ass_pos_format = "\\pos(%d,%d)";
                    int32_t pos_x, pos_y;
                    if( sscanf( ass_pos_p, ass_pos_format, &pos_x, &pos_y ) == 2 )
                    {
                        for( char *c = line_p; c != ass_pos_p; ++c )
                            fputc( (int)*c, output );
                        fprintf( output, ass_pos_format
                               , pos_x + ar_shift_x + p->shift_pos_x, pos_y + ar_shift_y + p->shift_pos_y );
                        for( ; *ass_pos_p != ')'; ++ass_pos_p );
                        ++ass_pos_p;
                        line_p = ass_pos_p;
                    }
                }
            }
        }
        fprintf( output, "%s", line_p );
        /* post process. */
        /* don't require ASS Subtitle. */
    }
}

static void cut_srt( param_t *p, FILE *input, FILE *output )
{
    char line[p->line_max];
    /* check 'UTF-8 BOM'. */
    if( !fgets( line, p->line_max, input ) )
        return;
    if( !strncmp( line, UTF8_BOM, UTF8_BOM_SIZE ) )
        fprintf( output, UTF8_BOM );
    fseeko( input, UTF8_BOM_SIZE, SEEK_SET );
    /* parse data. */
    int cut_count       = 0;
    int subtitle_number = 0;
    while( fgets( line, p->line_max, input ) )
    {
        /* pre process. */
        if( sscanf( line, "%d\n", &subtitle_number ) != 1 )
        {
            if( subtitle_number )
                fprintf( output, "%s", line );
            continue;
        }
        char line_head[p->line_max];
        sprintf( line_head, "%d\n", subtitle_number - cut_count );
        if( !fgets( line, p->line_max, input ) )
            break;
        /* check times line. */
        static const char *time_format = "%02d:%02d:%02d,%03d --> %02d:%02d:%02d,%03d";
        char *line_p = line;
        caption_time_t time_s, time_e;
        if( sscanf( line_p, time_format
                  , &time_s.Hrs, &time_s.Mins, &time_s.Secs, &time_s.Msecs
                  , &time_e.Hrs, &time_e.Mins, &time_e.Secs, &time_e.Msecs ) == 8 )
        {
            /* calculate times. */
            int64_t start = time_to_total( &time_s, 0 );
            int64_t end   = time_to_total( &time_e, 0 );
            if( get_output_times( p, &start, &end ) )
            {
                ++cut_count;
                while( fgets( line, p->line_max, input ) )
                    if( *line == '\n' )
                        break;
                subtitle_number = 0;
                continue;
            }
            /* write head. */
            fprintf( output, "%s", line_head );
            /* write body. */
            total_to_time( &time_s, start, 0 );
            total_to_time( &time_e, end  , 0 );
            fprintf( output, time_format
                   , time_s.Hrs, time_s.Mins, time_s.Secs, time_s.Msecs
                   , time_e.Hrs, time_e.Mins, time_e.Secs, time_e.Msecs );
            /* prepare for post process. */
            /* 29: line size. "00:00:00,000 --> 00:00:00,000" */
            line_p += 29;
        }
        fprintf( output, "%s", line_p );
        /* post process. */
        while( fgets( line, p->line_max, input ) )
        {
            fprintf( output, "%s", line );
            if( *line == '\n' )
                break;
        }
    }
}

static void correct_d2v_input( param_t *p )
{
    void *d2v_info = d2v_parser.parse( p->input );
    if( !d2v_info )
        goto end_correct;
    const char *filename = d2v_parser.get_filename( d2v_info, 0 );
    if( !filename )
        goto end_correct;
    /* check input path. */
    char *input;
    char *str  = NULL;
    char *str2 = (char *)(p->input);
    while( 1 )
    {
        static const char sep[] = "\\/";
        str2 = strpbrk( str2, sep );
        if( !str2 )
            break;
        str2 += 1;
        str = str2;
    }
    if( !str || filename[1] == ':' )
    {
        input = strdup( filename );
        if( !input )
            goto end_correct;
    }
    else
    {
        size_t path_size = str - (char *)(p->input);
        size_t len = path_size + strlen( filename ) + 1;
        input = (char *)malloc( len + 10 );
        if( !input )
            goto end_correct;
        strncpy( input, p->input, path_size );
        input[path_size] = '\0';
        strcat( input, filename );
    }
    mapi_log( LOG_LV2, "[check] correct input filename from d2v.\n"
                       "        %s\n"
                       "        %s\n", p->input, input );
    free( p->input );
    p->input = input;
end_correct:
    d2v_parser.release( d2v_info );
}

typedef struct {
    int64_t video_1st_start;
    int64_t video_key_start;
    int64_t video_odr_start;
    int64_t audio_start;
} delay_info_type;

static void parse_reader_offset( param_t *p, delay_info_type *delay_info )
{
    if( !p || p->reader == MPEG_READER_NONE )
        return;
    mpeg_reader_delay_type delay_type = MPEG_READER_DEALY_NONE;
    for( int i = 0; i < READER_TYPE_MAX; ++i )
        if( p->reader == reader_array[i].reader )
        {
            delay_type = reader_array[i].delay_type;
            break;
        }
    if( delay_type == MPEG_READER_DEALY_NONE )
        return;
    /* parse. */
    char mpegts[strlen( p->input ) + 4];
    strcpy( mpegts, p->input );
    if( p->execute_mode == EXECUTE_CUT_CAPTION || p->ext_correction )
        strcat( mpegts, ".ts" );
    void *info = mpeg_api_initialize_info( mpegts, 0 );
    if( !info )
        return;
    stream_info_t *stream_info = malloc( sizeof(*stream_info) );
    if( !stream_info )
        goto end_parse;
    if( p->service_id && 0 > mpeg_api_set_service_id( info, p->service_id ) )
        goto end_parse;
    if( p->pmt_program_id && 0 > mpeg_api_set_pmt_program_id( info, p->pmt_program_id ) )
        goto end_parse;
    if( p->pmt_target )
        mpeg_api_set_pmt_target( info, p->pmt_target );
    int64_t video_1st_pts, video_key_pts;
    int get_info_result = mpeg_api_get_stream_info( info, stream_info, &video_1st_pts, &video_key_pts );
    if( !get_info_result )
    {
        int64_t pcr           = stream_info->pcr;
        int64_t video_pts     = stream_info->video_pts;
        int64_t audio_pts     = stream_info->audio_pts;
        /* check wrap around. */
#define CHECK_OFFSET_VALUE( _timestamp )        \
( (pcr > _timestamp + p->wrap_around_check_v) ? MPEG_TIMESTAMP_WRAPAROUND_VALUE : 0 )
        int64_t video_1st_offset = CHECK_OFFSET_VALUE( video_1st_pts );
        int64_t video_key_offset = CHECK_OFFSET_VALUE( video_key_pts );
        int64_t video_odr_offset = CHECK_OFFSET_VALUE( video_pts     );
        int64_t audio_offset     = CHECK_OFFSET_VALUE( audio_pts     );
#undef CHECK_OFFSET_VALUE
        /* calculate delay. */
#define CALCLATE_DELAY_VALUE( _timestamp, _offset )     \
( (_timestamp >= 0) ? (pcr - (_timestamp + _offset)) / 90 : 0 )
        int64_t video_1st_start = CALCLATE_DELAY_VALUE( video_1st_pts, video_1st_offset );
        int64_t video_key_start = CALCLATE_DELAY_VALUE( video_key_pts, video_key_offset );
        int64_t video_odr_start = CALCLATE_DELAY_VALUE( video_pts    , video_odr_offset );
        int64_t audio_start     = CALCLATE_DELAY_VALUE( audio_pts    , audio_offset     );
#undef CALCLATE_DELAY_VALUE
        switch( delay_type )
        {
            case MPEG_READER_DEALY_VIDEO_GOP_KEYFRAME :
                p->reader_delay = video_key_start;
                break;
            case MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER :
                p->reader_delay = video_odr_start;
                break;
            case MPEG_READER_DEALY_FAST_VIDEO_STREAM :
                p->reader_delay = video_1st_start;
                break;
            case MPEG_READER_DEALY_FAST_STREAM :
                p->reader_delay = (video_1st_start > audio_start) ? video_1st_start : audio_start;
                break;
            case MPEG_READER_DEALY_NONE :
            default :
                break;
        }
        mapi_log( LOG_LV2, "[check] [read_delay] video_1st: %" PRId64 ", video_odr: %" PRId64 ", video_key: %" PRId64 "\n"
                           "                     audio: %" PRId64 "\n"
                         , video_1st_start, video_odr_start, video_key_start, audio_start );
        mapi_log( LOG_LV1, "[reader] delay: %" PRId64 "\n", p->reader_delay );
        /* setup required informations. */
        if( delay_info )
        {
            delay_info->video_1st_start = video_1st_start;
            delay_info->video_key_start = video_key_start;
            delay_info->video_odr_start = video_odr_start;
            delay_info->audio_start     = audio_start;
        }
    }
    else if( get_info_result > 0 )
        mapi_log( LOG_LV1, "[reader] MPEG-TS not have both video and audio stream.\n" );
end_parse:
    if( stream_info )
        free( stream_info );
    mpeg_api_release_info( info );
}

static void output_caption( param_t *p )
{
    /* check. */
    int check = correct_parameter( p );
    if( check )
    {
        if( check < 0 )
            mapi_log( LOG_LV0, "[log] error, parameters...\n" );
        return;
    }
    /* prepare. */
    if( load_cut_list( p ) )
    {
        mapi_log( LOG_LV0, "[log] error, list file.\n" );
        return;
    }
    parse_reader_offset( p, NULL );
    /* do. */
    for( int i = 0; i < CAPTION_TYPE_MAX; ++i )
    {
        if( (p->output_mode & input_array[i].mode) )
        {
            /* open files. */
            FILE *input = NULL, *output = NULL;
            if( !(input  = file_ext_open( p->input , input_array[i].ext, "rt" ))
             || !(output = file_ext_open( p->output, input_array[i].ext, "wt" )) )
            {
                if( input )
                    fclose( input );
                if( output )
                    fclose( output );
                continue;
            }
            /* output. */
            input_array[i].cut_func( p, input, output );
            /* close files. */
            fclose( output );
            fclose( input );
            mapi_log( LOG_LV0, "[log] input: %s%s\n"
                               "     output: %s%s\n"
                             , p->input, input_array[i].ext, p->output, input_array[i].ext );
        }
    }
}

static void analyze_mpegts( param_t *p )
{
    /* check. */
    if( !p || !p->input )
    {
        mapi_log( LOG_LV0, "[log] error, parameters...\n" );
        return;
    }
    /* parse. */
    delay_info_type delay_info = { 0 };
    parse_reader_offset( p, &delay_info );
    /* display. */
    mapi_log( LOG_LV_OUTPUT,
        "[analyze_mpegts]  %s.ts\n"
        "[reader's delay]\n"
        "  MPEG-2 VIDEO VFAPI Plug-In: %" PRId64 " msec\n"
        "  DGIndex and DGDecode      : %" PRId64 " msec\n"
        "  Libav reader              : %" PRId64 " msec\n"
        "  TMPGEnc series            : %" PRId64 " msec\n"
        "\n"
        , p->input
        , delay_info.video_key_start, delay_info.video_odr_start, delay_info.video_1st_start
        , (delay_info.video_1st_start > delay_info.audio_start) ? delay_info.video_1st_start : delay_info.audio_start );
}

static int check_commandline( int argc, char *argv[] )
{
    for( int i = 0; i < argc; ++i )
        if( !strcasecmp( argv[i], "--version" ) || !strcasecmp( argv[i], "-v" ) )
        {
            print_version();
            return 1;
        }
    if( argc < 3 )
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
    int conv_args = mapi_convert_args_to_utf8( &argc, &argv );
    if( conv_args < 0 )
        return -1;
    int i = 1;
    while( i < argc )
    {
        typedef void (*exec_func_type)( param_t *p );
        static const exec_func_type exec_func[2] = { output_caption, analyze_mpegts };
        param_t param;
        if( init_parameter( &param ) )
            return -1;
        debug_initialize();
        i = parse_commandline( argc, argv, i, &param );
        if( i < 0 )
            break;
        if( param.input )
            exec_func[param.execute_mode]( &param );
        cleanup_parameter( &param );
    }
    if( conv_args > 0 )
        free( argv );
    return 0;
}
