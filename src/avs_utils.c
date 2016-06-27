/*****************************************************************************
 * avs_utils.c
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

#include "avs_utils.h"

extern int avs_string_erase_invalid_strings( char *str )
{
    for( char *c = str; *c != '\0'; ++c )
    {
        /* check line feed or tab. */
        if( *c == '\n' || *c == '\t' )
        {
            *c = ' ';
            continue;
        }
        /* check commnet. */
        if( *c != '/' )
            continue;
        if( !strncmp( c, "//", 2 ) )
        {
            for( ; *c != '\0' && *c != '\n'; ++c )
                *c = ' ';
            if( *c == '\0' )
                break;
        }
        else if( !strncmp( c, "/*", 2 ) )
        {
            for( ; *c != '\0' && strncmp( c,  "*/", 2 ); ++c )
                *c = ' ';
            if( *c == '\0' )
                return -1;      /* script is illegal file. */
            strncpy( c, "  ", 2 );
        }
    }
    return 0;
}

extern char *avs_string_get_fuction_parameters( char **str_p, const char *search_word )
{
    char *str = strstr( *str_p, search_word );
    if( !str )
        return NULL;
    /* check parentheses items. '(s,e)' */
    char *c = strchr( str, '(' );
    if( !c )
        return NULL;
    char *parentheses_start = c;
    /* search '(' parentheses after. */
    int parentheses_count = 1;
    ++c;
    while( parentheses_count && *c != '\0' )
    {
        /* cache strings. */
        if( *c == ')' )
            --parentheses_count;
        else if( *c == '(' )
            ++parentheses_count;
        ++c;
    }
    if( parentheses_count )
        return NULL;            /* script is illegal file. */
    /* return parentheses position and next search position. */
    *str_p = ( *c != '\0' ) ? c + 1 : c;
    *c = '\0';
    return parentheses_start;
}

typedef enum {
    IS_PARENTHESES = 0x100,
    IS_OPERATOR    = 0x010,
    IS_NUMBER      = 0x001,
    IS_UNKNOWN     = 0x000
} calc_string_type;

static calc_string_type get_elements_type( char *str, char *str_before )
{
    calc_string_type type = IS_UNKNOWN;
    /* check elements. */
    if( *str == '+' || *str == '-' )
    {
        type = IS_OPERATOR;
        if( str_before && *str_before == '(' )
            type = IS_NUMBER;
    }
    else if( *str == '*' || *str == '/' || *str == '%' )
        type = IS_OPERATOR;
    else if( *str == '(' || *str == ')' )
        type = IS_PARENTHESES;
    else if( ('0' <= *str && *str <= '9') || *str == '.' )
        type = IS_NUMBER;
    return type;
}

static int get_calculate_string_elements_info( char *str, int *elements_num, int *max_size )
{
    if( !str || !elements_num || !max_size )
        return -1;
    int elements_count  = 0;
    int number_count    = 0;
    int number_max_size = 0;
    char *str_before = NULL;
    while( *str != '\0' )
    {
        /* check elements type. */
        calc_string_type elements_type = get_elements_type( str, str_before );
        if( elements_type == IS_UNKNOWN )
        {
            ++str;              /* unknown = space. */
            continue;
        }
        if( elements_type == IS_NUMBER )
        {
            if( number_count == 0 )
                ++elements_count;
            ++number_count;
        }
        else
        {
            ++elements_count;
            if( number_max_size < number_count )
                number_max_size = number_count;
            number_count = 0;
        }
        str_before = str;
        ++str;
    }
    *elements_num = elements_count;
    *max_size     = number_max_size;
    return 0;
}

static char **make_elements_list( char *str, int elements_num, int max_size )
{
    if( !str )
        return NULL;
    char **elements_list = (char **)malloc( sizeof(char *) * elements_num );
    if( !elements_list )
        return NULL;
    char *tmp = (char *)malloc( sizeof(char) * (max_size + 1) * elements_num );
    if( !tmp )
    {
        free( elements_list );
        return NULL;
    }
    memset( tmp, 0, (max_size + 1) * elements_num );
    for( int i = 0; i < elements_num; i++ )
        elements_list[i] = tmp + (max_size + 1) * i;
    /* make. */
    int elements_count = 0;
    int number_count   = 0;
    char *str_before = NULL;
    while( *str != '\0' )
    {
        /* check elements type. */
        calc_string_type elements_type = get_elements_type( str, str_before );
        if( elements_type == IS_UNKNOWN )
        {
            ++str;              /* unknown = space. */
            continue;
        }
        elements_list[elements_count][number_count+0] = *str;
        elements_list[elements_count][number_count+1] = '\0';
        str_before = str;
        ++str;
        if( elements_type == IS_NUMBER )
        {
            ++number_count;
            while( get_elements_type( str, str_before ) == IS_UNKNOWN )
                ++str;          /* unknown = space. */
            if( get_elements_type( str, str_before ) != IS_NUMBER )
            {
                ++elements_count;
                number_count = 0;
            }
        }
        else
        {
            ++elements_count;
            number_count = 0;
        }
    }
    return elements_list;
}

static int make_prn_list( char **elements_list, char **prn_list, int elements_num )
{
    char **elements_buffer = prn_list;
    char *elements_stack[elements_num];
    memset( elements_buffer, 0, elements_num );
    memset( elements_stack, 0, elements_num );
    int buffer_index = 0;
    int stack_index = 0;
    char *old_operator = NULL;
    for( int i = 0; i < elements_num; ++i )
    {
        char *elements = elements_list[i];
        if( !strcmp( elements, "(" ) )
        {
            elements_stack[stack_index++] = elements;
            old_operator = NULL;
        }
        else if( !strcmp( elements, ")" ) )
        {
            while( strcmp( elements_stack[--stack_index], "(" ) )
                elements_buffer[buffer_index++] = elements_stack[stack_index];
            if( stack_index )
                old_operator = elements_stack[stack_index - 1];
            else
                old_operator = NULL;
        }
        else if( !strcmp( elements, "*" )
              || !strcmp( elements, "/" )
              || !strcmp( elements, "%" ) )
        {
            elements_stack[stack_index++] = elements;
            old_operator = elements;
        }
        else if( !strcmp( elements, "+" )
              || !strcmp( elements, "-" ) )
        {
            if( old_operator && strcmp( old_operator, "+" ) && strcmp( old_operator, "-" ) )
            {
                elements_buffer[buffer_index++] = old_operator;
                --stack_index;
            }
            elements_stack[stack_index++] = elements;
            old_operator = elements;
        }
        else
            elements_buffer[buffer_index++] = elements;
    }
    mapi_log( LOG_LV4, "[debug] prn_index: %d, %d\n", buffer_index, stack_index );
    return buffer_index;
}

static int32_t calculate_prn_string_to_number( char **elements_list, int elements_num )
{
    int stack_index = 0;
    double stack[elements_num];
    stack[0] = 0;
    for( int i = 0; i < elements_num; ++i )
    {
        char *elements = elements_list[i];
        if( !strcmp( elements, "%" ) )
        {
            int32_t a = (int32_t)stack[stack_index - 2] + 0.5;
            int32_t b = (int32_t)stack[stack_index - 1] + 0.5;
            stack[stack_index - 2] = a % b;
            --stack_index;
        }
        else if( !strcmp( elements, "*" ) )
        {
            stack[stack_index - 2] *= stack[stack_index - 1];
            --stack_index;
        }
        else if( !strcmp( elements, "/" ) )
        {
            stack[stack_index - 2] /= stack[stack_index - 1];
            --stack_index;
        }
        else if( !strcmp( elements, "+" ) )
        {
            stack[stack_index - 2] += stack[stack_index - 1];
            --stack_index;
        }
        else if( !strcmp( elements, "-" ) )
        {
            stack[stack_index - 2] -= stack[stack_index - 1];
            --stack_index;
        }
        else
            stack[stack_index++] = atof( elements );
    }
    return (int32_t)stack[0];
}

#define LIST_DATA_DEBUG_LOG( list, num )        \
{                                               \
    mapi_log( LOG_LV4, "[debug]" );             \
    for( int i = 0; i < num; ++i )              \
        mapi_log( LOG_LV4, " %s", list[i] );    \
    mapi_log( LOG_LV4, "\n" );                  \
}
extern int avs_string_convert_calculate_string_to_result_number( avs_trim_info_t *info )
{
    size_t str_len = strlen( info->string );
    if( str_len < 5 )
        return -1;
    /* prepare. */
    char buf[str_len + 1];
    strcpy( buf, info->string );
    char *c = strchr( buf, ',' );
    if( !c )
        return -1;
    /* set buffer, 'start' and 'end' strings. */
    *c = '\0';
    char param_buf[2][str_len + 1];
    param_buf[1][0] = '(';
    strcpy( &(param_buf[1][1]), c + 1 );
    strcat( buf , ")" );
    strcpy( param_buf[0], buf );
    static struct {
        int32_t     num;
        char       *str;
    } convert_info[2] =
        {
            { -1, NULL },
            { -1, NULL }
        };
    convert_info[0].str = param_buf[0];
    convert_info[1].str = param_buf[1];
    /* convert. */
    for( int i = 0; i < 2; i++ )
    {
        mapi_log( LOG_LV4, "[debug] %s\n", convert_info[i].str );
        /* check number string. */
        int elements_num, max_size;
        if( get_calculate_string_elements_info( convert_info[i].str, &elements_num, &max_size ) )
            return -1;
        mapi_log( LOG_LV4, "[debug] [%d] elements:%d, max:%d\n", i, elements_num, max_size );
        /* set elements. */
        char **elements_list = make_elements_list( convert_info[i].str, elements_num, max_size );
        if( !elements_list )
            return -1;
        LIST_DATA_DEBUG_LOG( elements_list, elements_num );
        /* make 'PRN' list. */
        char *elements_prn_list[elements_num];
        int prn_index = make_prn_list( elements_list, elements_prn_list, elements_num );
        LIST_DATA_DEBUG_LOG( elements_prn_list, prn_index );
        /* calculate. */
        convert_info[i].num = calculate_prn_string_to_number( elements_prn_list, prn_index );
        mapi_log( LOG_LV4, "[debug] %d\n", convert_info[i].num );
        /* release. */
        free( elements_list[0] );
        free( elements_list );
    }
    /* check correct end. */
    if( convert_info[1].num < 0 )
        convert_info[1].num = convert_info[0].num - convert_info[1].num - 1;
    /* setup. */
    mapi_log( LOG_LV3, "[debug] str: %s\n"
                       "             %s\n", convert_info[0].str, convert_info[1].str );
    mapi_log( LOG_LV3, "[debug] num: %d, %d\n", convert_info[0].num, convert_info[1].num );
    if( convert_info[0].num < 0 )
        return -1;
    info->start = convert_info[0].num;
    info->end   = convert_info[1].num;
    return 0;
}
#undef LIST_DATA_DEBUG_LOG
