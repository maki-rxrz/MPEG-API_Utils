/*****************************************************************************
 * text_utils.c
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

#include "utils_def.h"
#include "avs_utils.h"
#include "text_utils.h"

typedef int (*load_list_func)( common_param_t *p, FILE *list, const char *search_word );
static int load_avs_txt( common_param_t *p, FILE *list, const char *search_word );
static int load_vcf_txt( common_param_t *p, FILE *list, const char *search_word );
static int load_del_txt( common_param_t *p, FILE *list, const char *search_word );
static int load_keyframe_txt( common_param_t *p, FILE *list, const char *search_word );

static const struct {
    char               *ext;
    cut_list_type       list_type;
    mpeg_reader_type    reader;
    load_list_func      load_func;
    char               *search_word;
} list_array[CUT_LIST_TYPE_MAX] =
    {
        {  ".txt"      , CUT_LIST_DEL_TEXT , MPEG_READER_M2VVFAPI, load_del_txt     , NULL                         },
        {  ".avs"      , CUT_LIST_AVS_TRIM , MPEG_READER_DGDECODE, load_avs_txt     , "Trim"                       },
        {  ".vcf"      , CUT_LIST_VCF_RANGE, MPEG_READER_DGDECODE, load_vcf_txt     , "VirtualDub.subset.AddRange" },
        {  ".keyframe" , CUT_LIST_KEY_AUTO , MPEG_READER_TMPGENC , load_keyframe_txt, NULL                         },
        {  ".keyframe1", CUT_LIST_KEY_CUT_O, MPEG_READER_TMPGENC , load_keyframe_txt, NULL                         },
        {  ".keyframe2", CUT_LIST_KEY_CUT_E, MPEG_READER_TMPGENC , load_keyframe_txt, NULL                         },
        {  ".keyframe3", CUT_LIST_KEY_TRIM , MPEG_READER_TMPGENC , load_keyframe_txt, NULL                         }
    };

static int load_avs_txt( common_param_t *p, FILE *list, const char *search_word )
{
    char *line = (char *)malloc( p->line_max );
    if( !line )
        return -1;
    int alloc_count = 1;
    /* initialize. */
    p->list_data_count = 0;
    /* setup. */
    while( fgets( line, p->line_max, list ) )
    {
        /* check if exist search word. */
        if( !strstr( line, search_word ) )
            continue;
        /* check multi lines. */
        fpos_t next_pos;
        while( 1 )
        {
            fgetpos( list, &next_pos );
            char cache_line[p->line_max];
            if( !fgets( cache_line, p->line_max, list ) )
                break;
            size_t cache_len = strlen( cache_line );
            /* check connected. */
            if( strspn( cache_line, " \t\n"   ) == cache_len
             || strspn( cache_line, " \t\n\\" ) == cache_len )
                continue;                       /* skip blank line. */
            char *c = strchr( cache_line, '\\' );
            if( !c )
                break;                          /* non connect line. */
            size_t check1 = strspn( cache_line, " \t\n" );
            size_t check2 = (size_t)(c - cache_line);
            if( check1 != check2 )
                break;                          /* non connect line. */
            /* connect lines. */
            size_t line_size = sizeof(line);
            size_t string_len = strlen(line) + strlen(c + 1) + 1;
            if( line_size < string_len )
            {
                ++alloc_count;
                char *tmp = (char *)realloc( line, p->line_max * alloc_count );
                if( !tmp )
                    goto fail_load;
                line = tmp;
            }
            strcat( line, c + 1 );
        }
        /* erase invalid strings. */
        if( avs_string_erase_invalid_strings( line ) )
            goto fail_load;
        mapi_log( LOG_LV4, "[debug] line: %s\n", line );
        /* parse. */
        char *line_p = strstr( line, search_word );
        while( line_p )
        {
            /* generate parameter strings. */
            char *param_string = avs_string_get_fuction_parameters( &line_p, search_word );
            if( !param_string )
                break;
            mapi_log( LOG_LV4, "[debug] param_str: %s\n", param_string );
            mapi_log( LOG_LV4, "[debug] next_str : %s\n", line_p );
            /* convert and calculate. */
            avs_trim_info_t info;
            info.string = param_string;
            if( !avs_string_convert_calculate_string_to_result_number( &info ) )
                PUSH_LIST_DATA( p, info.start, info.end );
            /* seek next data. */
            line_p = strstr( line_p, search_word );
        }
        /* seek next line. */
        fsetpos( list, &next_pos );
    }
    free( line );
    return p->list_data_count ? 0 : -1;
fail_load:
    free( line );
    p->list_data_count = 0;
    return -1;
}

static int load_vcf_txt( common_param_t *p, FILE *list, const char *search_word )
{
    char line[p->line_max];
    /* initialize. */
    size_t sword_len = strlen( search_word );
    char search_format[sword_len + 8];
    strcpy( search_format, search_word );
    strcat( search_format, "(%d,%d)" );
    p->list_data_count = 0;
    /* setup. */
    while( fgets( line, p->line_max, list ) )
    {
        int32_t start, end;
        if( sscanf( line, search_format, &start, &end ) == 2 )
            PUSH_LIST_DATA( p, start, start + end - 1 );
    }
    return p->list_data_count ? 0 : -1;
}

static int load_del_txt( common_param_t *p, FILE *list, const char *search_word )
{
    char line[p->line_max];
    int32_t start, end;
    /* check range. */
    char *result;
    while( (result = fgets( line, p->line_max, list )) )
    {
        if( *line == '#' )
            continue;
        if( sscanf( line, "%d * %d\n", &start, &end ) == 2 )
            break;
    }
    if( !result )
        return -1;
    /* check delete, and setup. */
    p->list_data_count = 0;
    while( fgets( line, p->line_max, list ) )
    {
        if( *line == '#' )
            continue;
        int32_t num1, num2;
        if( sscanf( line, "%d * %d\n", &num1, &num2 ) == 2 )
            continue;
        else if( sscanf( line, "%d - %d\n", &num1, &num2 ) == 2 )
        {
            PUSH_LIST_DATA( p, start, num1 - 1 );
            start = num2 + 1;
        }
        else if( sscanf( line, "%d\n", &num1 ) == 1 )
        {
            PUSH_LIST_DATA( p, start, num1 - 1 );
            start = num1 + 1;
        }
    }
    PUSH_LIST_DATA( p, start, end );
    return 0;
}

static int load_keyframe_txt( common_param_t *p, FILE *list, const char *search_word )
{
    char line[p->line_max];
    /* check 'Trim' style. */
    if( p->list_type == CUT_LIST_KEY_TRIM )
    {
        /* initialize. */
        p->list_data_count = 0;
        /* setup. */
        int i = 0;
        int32_t start;
        while( fgets( line, p->line_max, list ) )
        {
            int32_t num1 = atoi( line );
            if( num1 <= 0 )
                break;
            if( i )
                PUSH_LIST_DATA( p, start, num1 );
            start = num1;
            i ^= 1;
        }
        return p->list_data_count ? 0 : -1;
    }
    /* check if first frame number is '0'. */
    if( !fgets( line, p->line_max, list ) || atoi( line ) )
    {
        mapi_log( LOG_LV0, "[log] error, *.keyframe is NG foramt...\n" );
        return -1;
    }
    /* check 'AUTO'. */
    cut_list_type list_type = p->list_type + p->list_key_type;
    if( list_type == CUT_LIST_KEY_AUTO )
    {
        /* check total frames for select odd/even. */
        int32_t check[2] = { 0 };
        int32_t odd_total = 0, even_total = 0;
        int i = 1;
        while( fgets( line, p->line_max, list ) )
        {
            check[i] = atoi( line );
            if( i )
                odd_total  += check[1] - 1 - check[0] + 1;
            else
                even_total += (check[0] - 1) - (check[1] + 1);
            i ^= 1;
        }
        if( odd_total > even_total )
            list_type = CUT_LIST_KEY_CUT_E;
        else
            list_type = CUT_LIST_KEY_CUT_O;
        mapi_log( LOG_LV1, "[list] [keyframe] odd:%d, even:%d, select:%d\n", odd_total, even_total, list_type - CUT_LIST_KEY_AUTO );
    }
    /* initialize. */
    fseeko( list, 0, SEEK_SET );
    p->list_data_count = 0;
    /* skip '0'. */
    fgets( line, p->line_max, list );
    /* setup. */
    int i = (list_type == CUT_LIST_KEY_CUT_E);
    int32_t start = 0;
    while( fgets( line, p->line_max, list ) )
    {
        int32_t num1 = atoi( line );
        if( num1 <= 0 )
            break;
        if( i )
            PUSH_LIST_DATA( p, start, num1 - 1 );
        start = num1 + 1;
        i ^= 1;
    }
    return p->list_data_count ? 0 : -1;
}

static cut_list_data_t *malloc_list_data( common_param_t *p, FILE *list, const char *search_word )
{
    /* search words. */
    fseeko( list, 0, SEEK_SET );
    char line[p->line_max];
    int search_word_count = 0;
    if( search_word )
    {
        /* check search word nums. */
        size_t sword_len = strlen( search_word );
        while( fgets( line, p->line_max, list ) )
        {
            char *c = strchr( line, search_word[0] );
            if( !c )
                continue;
            char *line_p = line;
            while( *line_p != '\0' )
                if( !strncmp( line_p, search_word, sword_len ) )
                {
                    ++search_word_count;
                    line_p += sword_len;
                }
                else
                    ++line_p;
        }
    }
    else
        /* check line count nums. */
        do
            ++search_word_count;
        while( fgets( line, p->line_max, list ) );
    fseeko( list, 0, SEEK_SET );
    if( search_word_count <= 0 )
        return NULL;
    /* malloc and initialize. */
    size_t size = sizeof(cut_list_data_t) * search_word_count;
    cut_list_data_t *list_data = (cut_list_data_t *)malloc( size );
    if( list_data )
        memset( list_data, 0, size );
    return list_data;
}

extern int text_load_cut_list( common_param_t *p, FILE *list )
{
    if( !p || !list )
        return -1;
    int result = -1;
    for( int i = 0; i < CUT_LIST_TYPE_MAX; ++i )
        if( p->list_type == list_array[i].list_type )
        {
            const char *search_word = p->list_search_word ? p->list_search_word : list_array[i].search_word;
            p->list_data = malloc_list_data( p, list, search_word );
            if( p->list_data )
                result = list_array[i].load_func( p, list, search_word );
            break;
        }
    return result;
}

extern void text_get_cut_list_type( common_param_t *p, const char *ext )
{
    if( !p || !ext )
        return;
    for( int j = 0; j < CUT_LIST_TYPE_MAX; ++j )
        if( !strcasecmp( ext, list_array[j].ext ) )
        {
            p->list_type = list_array[j].list_type;
            p->reader    = list_array[j].reader;
            break;
        }
}
