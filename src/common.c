/*****************************************************************************
 * common.c
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

#include "file_utils.h"

/*============================================================================
 *  OS depncdent functions
 *==========================================================================*/

#ifdef _WIN32
#include "windeps.h"
#endif

/*============================================================================
 *  Utility functions
 *==========================================================================*/

extern int64_t get_file_size( char *file_name )
{
    int64_t file_size;

    FILE *fp = mapi_fopen( file_name, "rb" );
    if( !fp )
        return -1;

    fseeko( fp, 0, SEEK_END );
    file_size = ftello( fp );

    fclose( fp );

    return file_size;
}
