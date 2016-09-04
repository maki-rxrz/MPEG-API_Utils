/*****************************************************************************
 * windeps.h
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

//#include "common.h"

//#include <stdlib.h>
#include <windows.h>

/*============================================================================
 *  OS depncdent functions
 *==========================================================================*/

static int convert_utf8_to_utf16( const char *utf8_str, wchar_t **utf16_str )
{
    int num = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str, -1, NULL, 0 );
    *utf16_str = (wchar_t *)calloc( num, sizeof(wchar_t) );
    if( !(*utf16_str) )
        return -1;
    MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str, -1, *utf16_str, num );
    return 0;
}

FILE *mapi_fopen( const char *file_name, const char *mode )
{
    FILE *fp = NULL;
    wchar_t *w_file_name = NULL;
    wchar_t *w_mode      = NULL;
    /* prepare the wide character. */
    if( convert_utf8_to_utf16( file_name, &w_file_name ) )
        return NULL;
    if( convert_utf8_to_utf16( mode, &w_mode ) )
    {
        free( w_file_name );
        return NULL;
    }
    /* open. */
    fp = _wfopen( w_file_name, w_mode );
    if( !fp )
        fp = fopen( file_name, mode );
    free( w_file_name );
    free( w_mode );
    return fp;
}

int mapi_convert_args_to_utf8( int *argc_p, char ***argv_p )
{
    wchar_t **w_argv = CommandLineToArgvW( GetCommandLineW(), argc_p );
    if( !w_argv )
        return -1;
    /* prepare for convert. */
    int argc   = *argc_p;
    int offset = (argc + 1) * sizeof(char *);
    int size   = offset;
    for( int i = 0; i < argc; i++ )
        size += WideCharToMultiByte( CP_UTF8, 0, w_argv[i], -1, NULL, 0, NULL, NULL );
    /* allocate buffer. */
    char **argv_utf8 = (char **)malloc( size );
    if( !argv_utf8 )
        goto fail;
    /* convert UTF-8. */
    for( int i = 0; i < argc; i++ )
    {
        argv_utf8[i] = (char *)argv_utf8 + offset;
        offset += WideCharToMultiByte( CP_UTF8, 0, w_argv[i], -1, argv_utf8[i], size - offset, NULL, NULL );
    }
    argv_utf8[argc] = NULL;
    /* setup and cleanup. */
    *argv_p = argv_utf8;
    LocalFree( w_argv );
    return 1;
fail:
    LocalFree( w_argv );
    return -1;
}

#ifdef MAPI_UTILS_CODE_ENABLED

#define STRING_BUFFER_SIZE      (4096)

int mapi_vfprintf( FILE *stream, const char *format, va_list arg )
{
    if( stream != stdout && stream != stderr )
        return vfprintf( stream, format, arg );
    /* check if redirection. */
    HANDLE console = GetStdHandle( (stream == stdout) ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
    DWORD  mode;
    if( !GetConsoleMode( console, &mode ) )
        return vfprintf( stream, format, arg );
    /* prepare output strings. */
    char    utf8_str[STRING_BUFFER_SIZE];
    wchar_t utf16_str[STRING_BUFFER_SIZE];
    va_list arg2;
    va_copy( arg2, arg );
    int length = vsnprintf( utf8_str, sizeof(utf8_str), format, arg2 );
    va_end( arg2 );
    int length_utf16 = MultiByteToWideChar( CP_UTF8, 0, utf8_str, length, utf16_str, sizeof(utf16_str) / sizeof(wchar_t) );
    /* output. */
    DWORD written;
    WriteConsoleW( console, utf16_str, length_utf16, &written, NULL );
    return length;
}

#endif
