/*****************************************************************************
 * mpegts_parser.c
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

#include "mpeg_common.h"
#include "mpeg_stream.h"
#include "mpeg_parser.h"
#include "mpegts_def.h"

#define SYNC_BYTE                           '\x47'

#define TS_PACKET_SIZE                      (188)
#define TTS_PACKET_SIZE                     (192)
#define FEC_TS_PACKET_SIZE                  (204)

#define TS_PACKET_TYPE_NUM                  (3)
#define TS_PACKET_FIRST_CHECK_COUNT_NUM     (4)
#define TS_PACKET_SEARCH_CHECK_COUNT_NUM    (50000)
#define TS_PACKET_SEARCH_RETRY_COUNT_NUM    (5)

typedef struct {
    FILE                       *input;
    int32_t                     packet_size;
    int32_t                     sync_byte_position;
    int64_t                     read_position;
    int32_t                     ts_packet_length;
    uint32_t                    packet_check_count_num;
} mpegts_file_context_t;

typedef struct {
    mpegts_file_context_t       file_read;
    uint16_t                    program_id;
    mpeg_stream_type            stream_type;
    mpeg_stream_group_type      stream_judge;
    int64_t                     gop_number;
} mpegts_stream_context_t;

typedef struct {
    mpeg_stream_group_type      stream_judge;
    mpeg_stream_type            stream_type;
    uint16_t                    program_id;
} mpegts_pid_in_pmt_t;

typedef struct {
    parser_status_type          status;
    char                       *mpegts;
    mpegts_file_context_t       file_read;
    int32_t                     pid_list_num_in_pat;
    uint16_t                   *pid_list_in_pat;
    int32_t                     pid_list_num_in_pmt;
    mpegts_pid_in_pmt_t        *pid_list_in_pmt;
    uint32_t                    packet_check_retry_num;
    uint16_t                    pmt_program_id;
    uint16_t                    pcr_program_id;
    mpegts_stream_context_t    *video_stream;
    mpegts_stream_context_t    *audio_stream;
    uint8_t                     video_stream_num;
    uint8_t                     audio_stream_num;
    int64_t                     pcr;
} mpegts_info_t;

static int32_t mpegts_check_sync_byte_position( FILE *input, int32_t packet_size, int packet_check_count )
{
    int32_t position = -1;
    int check_count = -1;
    fpos_t start_fpos;
    fgetpos( input, &start_fpos );
    uint8_t c;
    while( fread( &c, 1, 1, input ) == 1 && position < packet_size )
    {
        check_count = -1;
        ++position;
        if( c != SYNC_BYTE )
            continue;
        fpos_t fpos;
        fgetpos( input, &fpos );
        check_count = packet_check_count;
        while( check_count )
        {
            if( fseeko( input, packet_size - 1, SEEK_CUR ) )
                goto no_sync_byte;
            if( fread( &c, 1, 1, input ) != 1 )
                goto detect_sync_byte;
            if( c != SYNC_BYTE )
                break;
            --check_count;
        }
        if( !check_count )
            break;
        fsetpos( input, &fpos );
    }
detect_sync_byte:
    fsetpos( input, &start_fpos );
    if( position >= packet_size )
        return -1;
    return position;
no_sync_byte:
    fsetpos( input, &start_fpos );
    return -1;
}

static void mpegts_fread( uint8_t *read_buffer, int64_t read_size, mpegts_file_context_t *file )
{
    if( read_size > file->ts_packet_length )
    {
        dprintf( LOG_LV1, "[log] illegal parameter!!  packet_len:%d  read_size:%"PRId64"\n", file->ts_packet_length, read_size );
        read_size = file->ts_packet_length;
    }
    fread( read_buffer, 1, read_size, file->input );
    file->ts_packet_length -= read_size;
}

typedef enum {
    MPEGTS_SEEK_CUR,
    MPEGTS_SEEK_NEXT,
    MPEGTS_SEEK_SET,
    MPEGTS_SEEK_RESET
} mpegts_seek_type;

static void mpegts_fseek( mpegts_file_context_t *file, int64_t seek_offset, mpegts_seek_type seek_type )
{
    int origin = (seek_type == MPEGTS_SEEK_CUR || seek_type == MPEGTS_SEEK_NEXT) ? SEEK_CUR : SEEK_SET;
    if( seek_type == MPEGTS_SEEK_NEXT )
        seek_offset += file->ts_packet_length + file->packet_size - TS_PACKET_SIZE;
    fseeko( file->input, seek_offset, origin );
    file->sync_byte_position = -1;
    /* set ts_packet_length. */
    switch( seek_type )
    {
        case MPEGTS_SEEK_CUR :
            file->ts_packet_length -= seek_offset;
            break;
        case MPEGTS_SEEK_NEXT :
            file->ts_packet_length = 0;
            break;
        case MPEGTS_SEEK_SET :
            //file->ts_packet_length = ???;
            break;
        case MPEGTS_SEEK_RESET :
            file->ts_packet_length = TS_PACKET_SIZE;
            break;
        default :
            break;
    }
}

static int mpegts_seek_sync_byte_position( mpegts_file_context_t *file )
{
    if( !file->sync_byte_position )
        goto detect_sync_byte;
    if( file->sync_byte_position < 0 )
        file->sync_byte_position = mpegts_check_sync_byte_position( file->input, file->packet_size, 1 );
    if( file->sync_byte_position < 0 || fseeko( file->input, file->sync_byte_position, SEEK_CUR ) )
        return -1;
detect_sync_byte:
    file->sync_byte_position = 0;
    file->ts_packet_length   = TS_PACKET_SIZE;
    file->read_position      = ftello( file->input );
    return 0;
}

static int mpegts_first_check( mpegts_file_context_t *file )
{
    int result = -1;
    dprintf( LOG_LV2, "[check] mpegts_first_check()\n" );
    for( int i = 0; i < TS_PACKET_TYPE_NUM; ++i )
    {
        static const int32_t mpegts_packet_size[TS_PACKET_TYPE_NUM] =
            {
                TS_PACKET_SIZE, TTS_PACKET_SIZE, FEC_TS_PACKET_SIZE
            };
        int32_t position = mpegts_check_sync_byte_position( file->input, mpegts_packet_size[i], TS_PACKET_FIRST_CHECK_COUNT_NUM );
        if( position != -1 )
        {
            file->packet_size        = mpegts_packet_size[i];
            file->sync_byte_position = position;
            result = 0;
            break;
        }
    }
    dprintf( LOG_LV3, "[check] packet size:%d\n", file->packet_size );
    return result;
}

#define TS_PACKET_HEADER_SIZE               (4)

static inline void show_packet_header_info( mpegts_packet_header_t *h )
{
    dprintf( LOG_LV4,
            "[check] sync_byte:0x%x\n"
            "        error_indicator:%d\n"
            "        start_indicator:%d\n"
            "        priority:%d\n"
            "        scrambling:%d\n"
            "        PID:0x%04X\n"
            "        adpf_control:%d\n"
            "        continuity_counter:%d\n"
            , h->sync_byte
            , h->transport_error_indicator
            , h->payload_unit_start_indicator
            , h->transport_priority
            , h->transport_scrambling_control
            , h->program_id
            , h->adaptation_field_control
            , h->continuity_counter );
}

static int mpegts_read_packet_header( mpegts_file_context_t *file, mpegts_packet_header_t *h )
{
    if( mpegts_seek_sync_byte_position( file ) )
        return -1;
    uint8_t ts_header[TS_PACKET_HEADER_SIZE];
    mpegts_fread( ts_header, TS_PACKET_HEADER_SIZE, file );
    /* setup header data. */
    h->sync_byte                     =    ts_header[0];
    h->transport_error_indicator     = !!(ts_header[1] & 0x80);
    h->payload_unit_start_indicator  = !!(ts_header[1] & 0x40);
    h->transport_priority            = !!(ts_header[1] & 0x20);
    h->program_id                    =  ((ts_header[1] & 0x1F) << 8) | ts_header[2];
    h->transport_scrambling_control  =   (ts_header[3] & 0xC0) >> 6;
    h->adaptation_field_control      =   (ts_header[3] & 0x30) >> 4;
    h->continuity_counter            =   (ts_header[3] & 0x0F);
    /* ready next. */
    file->sync_byte_position = -1;
    return 0;
}

#define TS_PID_PAT_SECTION_HEADER_SIZE          (8)
#define TS_PID_PMT_SECTION_HEADER_SIZE          (12)
#define TS_PACKET_SECTION_CRC32_SIZE            (4)
#define TS_PACKET_PAT_SECTION_DATA_SIZE         (4)
#define TS_PACKET_PMT_SECTION_DATA_SIZE         (5)
#define TS_PACKET_TABLE_SECTION_SIZE_MAX        (1024)      /* 10bits: size is 12bits, first 2bits = '00' */

static inline void show_table_section_info( void *si_p )
{
    mpegts_table_info_t *table_si = (mpegts_table_info_t *)si_p;
    dprintf( LOG_LV4,
            "[check] table_id:0x%02d\n"
            "        syntax_indicator:%d\n"
            "        section_length:%d\n"
            "        id_number:%d\n"
            "        version_number:%d\n"
            "        current_next:%d\n"
            "        section_number:%d\n"
            "        last_section_number:%d\n"
            , table_si->common.table_id
            , table_si->common.section_syntax_indicator
            , table_si->common.section_length
            , table_si->common.id_number
            , table_si->common.version_number
            , table_si->common.current_next_indicator
            , table_si->common.section_number
            , table_si->common.last_section_number );
}

static inline void show_pmt_section_info( mpegts_pmt_section_info_t *pmt_si )
{
    show_table_section_info( pmt_si );
    dprintf( LOG_LV4,
            "        PCR_PID:0x%04X\n"
            "        program_info_length:%d\n"
            , pmt_si->pcr_program_id
            , pmt_si->program_info_length );
}

static int mpegts_search_program_id_packet( mpegts_file_context_t *file, mpegts_packet_header_t *h, uint16_t search_program_id )
{
    int check_count = file->packet_check_count_num;
    do
    {
        if( !check_count )
            return 1;
        --check_count;
        if( mpegts_read_packet_header( file, h ) )
            return -1;
        if( h->program_id == search_program_id )
            break;
        /* seek next packet head. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
    return 0;
}

#define SKIP_ADAPTATION_FIELD( f, h, s )            \
{                                                   \
    if( (h).adaptation_field_control > 1 )          \
    {                                               \
        mpegts_fread( &s, 1, f );                   \
        mpegts_fseek( f, s, MPEGTS_SEEK_CUR );      \
    }                                               \
}

static int mpegts_get_table_section_header( mpegts_file_context_t *file, mpegts_packet_header_t *h, uint16_t search_program_id, uint8_t *section_header, uint16_t section_header_length )
{
    dprintf( LOG_LV4, "[check] mpegts_get_table_section_header()\n" );
    do
    {
        int search_result = mpegts_search_program_id_packet( file, h, search_program_id );
        if( search_result )
            return search_result;
        show_packet_header_info( h );
    }
    while( !h->payload_unit_start_indicator );
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( file, *h, adaptation_field_size )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    /* check pointer field. */
    uint8_t pointer_field;
    mpegts_fread( &pointer_field, 1, file );
    if( pointer_field )
        mpegts_fseek( file, pointer_field, MPEGTS_SEEK_CUR );
    /* read section header. */
    mpegts_fread( section_header, section_header_length, file );
    return 0;
}

static int mpegts_seek_packet_payload_data( mpegts_file_context_t *file, mpegts_packet_header_t *h, uint16_t search_program_id, int indicator_check, int indicator_status )
{
    dprintf( LOG_LV4, "[check] mpegts_seek_packet_payload_data()\n" );
    if( mpegts_search_program_id_packet( file, h, search_program_id ) )
        return -1;
    show_packet_header_info( h );
    /* check start indicator. */
    if( indicator_check && (indicator_status == h->payload_unit_start_indicator) )
        return 1;
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( file, *h, adaptation_field_size )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    return 0;
}

static int mpegts_get_table_section_data( mpegts_file_context_t *file, uint16_t search_program_id, uint8_t *section_buffer, uint16_t section_length )
{
    mpegts_packet_header_t h;
    int64_t start_position = ftello( file->input );
    int32_t rest_ts_packet_length = file->ts_packet_length;
    /* buffering payload data. */
    int read_count = 0;
    int need_ts_packet_payload_data = 0;
    while( read_count < section_length )
    {
        if( need_ts_packet_payload_data )
            /* seek next packet payload data. */
            if( mpegts_seek_packet_payload_data( file, &h, search_program_id, 1, 1 ) )
                return -1;
        need_ts_packet_payload_data = 1;
        int32_t read_size = (section_length - read_count > file->ts_packet_length) ? file->ts_packet_length : section_length - read_count;
        mpegts_fread( &(section_buffer[read_count]), read_size, file );
        read_count += read_size;
        dprintf( LOG_LV4, "[check] section data read:%d, rest_packet:%d\n", read_size, file->ts_packet_length );
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    /* reset buffering start packet position. */
    mpegts_fseek( file, start_position, MPEGTS_SEEK_SET );
    file->ts_packet_length = rest_ts_packet_length;
    return 0;
}

static int mpegts_search_pat_packet( mpegts_file_context_t *file, mpegts_pat_section_info_t *pat_si )
{
    mpegts_packet_header_t h;
    uint8_t section_header[TS_PID_PAT_SECTION_HEADER_SIZE];
    do
        if( mpegts_get_table_section_header( file, &h, TS_PID_PAT, section_header, TS_PID_PAT_SECTION_HEADER_SIZE ) )
            return -1;
    while( section_header[0] != PSI_TABLE_ID_PAT );
    /* setup header data. */
    pat_si->table_id                 =    section_header[0];
    pat_si->section_syntax_indicator = !!(section_header[1] & 0x80);
    /* '0'              1 bit        = !!(section_header[1] & 0x40);            */
    /* reserved '11'    2 bit        =   (section_header[1] & 0x30) >> 4;       */
    pat_si->section_length           =  ((section_header[1] & 0x0F) << 8) | section_header[2];
    pat_si->transport_stream_id      =   (section_header[3] << 8) | section_header[4];
    /* reserved '11'    2bit         =   (section_header[5] & 0xC0) >> 6;       */
    pat_si->version_number           =   (section_header[5] & 0x3E) >> 1;
    pat_si->current_next_indicator   =   (section_header[5] & 0x01);
    pat_si->section_number           =    section_header[6];
    pat_si->last_section_number      =    section_header[7];
    show_table_section_info( pat_si );
    return 0;
}

static int mpegts_parse_pat( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_parse_pat()\n" );
    int64_t read_pos;
    /* search. */
    mpegts_pat_section_info_t pat_si;
    int section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_pat_packet( &(info->file_read), &pat_si ) )
            return -1;
        /* get section length. */
        section_length = pat_si.section_length - 5;     /* 5: section_header[3]-[7] */
        if( (section_length - TS_PACKET_SECTION_CRC32_SIZE) % TS_PACKET_PAT_SECTION_DATA_SIZE )
            continue;
        /* check file position. */
        read_pos = info->file_read.read_position;
        /* buffering section data. */
        if( mpegts_get_table_section_data( &(info->file_read), TS_PID_PAT, section_buffer, section_length ) )
            continue;
        break;
    }
    if( !retry_count )
        return -1;
    /* listup. */
    info->pid_list_in_pat = malloc( sizeof(uint16_t) * ((section_length - TS_PACKET_SECTION_CRC32_SIZE) / TS_PACKET_PAT_SECTION_DATA_SIZE) );
    if( !info->pid_list_in_pat )
        return -1;
    int pid_list_num = 0, read_count = 0;
    while( read_count < section_length - TS_PACKET_SECTION_CRC32_SIZE )
    {
        uint8_t *section_data = &(section_buffer[read_count]);
        read_count += TS_PACKET_PAT_SECTION_DATA_SIZE;
        uint16_t program_number =  (section_data[0] << 8) | section_data[1];
        /* '111'        3 bit   =  (section_data[2] & 0xE0) >> 4;       */
        uint16_t pmt_program_id = ((section_data[2] & 0x1F) << 8) | section_data[3];
        dprintf( LOG_LV2, "[check] program_number:%d, pmt_PID:0x%04X\n", program_number, pmt_program_id );
        if( program_number )
            info->pid_list_in_pat[pid_list_num++] = pmt_program_id;
    }
    if( (section_length - read_count) != TS_PACKET_SECTION_CRC32_SIZE )
    {
        free( info->pid_list_in_pat );
        return -1;
    }
    info->pid_list_num_in_pat = pid_list_num;
    uint8_t *crc_32 = &(section_buffer[read_count]);
    dprintf( LOG_LV2, "[check] CRC32:" );
    for( int i = 0; i < TS_PACKET_SECTION_CRC32_SIZE; ++i )
        dprintf( LOG_LV2, " %02X", crc_32[i] );
    dprintf( LOG_LV2, "\n" );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", read_pos );
    /* ready next. */
    mpegts_fseek( &(info->file_read), 0, MPEGTS_SEEK_NEXT );
    info->file_read.sync_byte_position = -1;
    info->file_read.read_position      = read_pos;
    return 0;
}

static int mpegts_search_pmt_packet( mpegts_info_t *info, mpegts_pmt_section_info_t *pmt_si )
{
    /* search. */
    mpegts_packet_header_t h;
    uint8_t section_header[TS_PID_PMT_SECTION_HEADER_SIZE];
    int pid_list_index = -1;
    int64_t start_position = ftello( info->file_read.input );
    do
    {
        mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
        ++pid_list_index;
        if( pid_list_index >= info->pid_list_num_in_pat )
            return -1;
        if( 0 > mpegts_get_table_section_header( &(info->file_read), &h, info->pid_list_in_pat[pid_list_index], section_header, TS_PID_PMT_SECTION_HEADER_SIZE ) )
            return -1;
    }
    while( section_header[0] != PSI_TABLE_ID_PMT );
    /* setup PMT PID. */
    info->pmt_program_id = h.program_id;
    /* setup header data. */
    pmt_si->table_id                 =   section_header[0];
    pmt_si->section_syntax_indicator =  (section_header[1] & 0x80) >> 7;
    /* '0'              1 bit        =  (section_header[1] & 0x40) >> 6;        */
    /* reserved '11'    2 bit        =  (section_header[1] & 0x30) >> 4;        */
    pmt_si->section_length           = ((section_header[1] & 0x0F) << 8) | section_header[2];
    pmt_si->program_number           =  (section_header[3] << 8) | section_header[4];
    /* reserved '11'    2bit         =  (section_header[5] & 0xC0) >> 6;        */
    pmt_si->version_number           =  (section_header[5] & 0x3E) >> 1;
    pmt_si->current_next_indicator   =  (section_header[5] & 0x01);
    pmt_si->section_number           =  (section_header[6] & 0xC0) >> 6;
    pmt_si->last_section_number      =  (section_header[7] & 0xC0) >> 6;
    pmt_si->pcr_program_id           = ((section_header[8] & 0x1F) << 8) | section_header[9];
    pmt_si->program_info_length      = ((section_header[10] & 0x0F) << 8) | section_header[11];
    show_pmt_section_info( pmt_si );
    /* setup PCR PID. */
    info->pcr_program_id = pmt_si->pcr_program_id;
    return 0;
}

static int mpegts_parse_pmt( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_parse_pmt()\n" );
    int64_t read_pos;
    /* search. */
    mpegts_pmt_section_info_t pmt_si;
    int section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_pmt_packet( info, &pmt_si ) )
            return -1;
        /* get section length. */
        section_length = pmt_si.section_length - 9;             /* 9: section_header[3]-[11] */
        mpegts_fseek( &(info->file_read), pmt_si.program_info_length, MPEGTS_SEEK_CUR );
        section_length -= pmt_si.program_info_length;
        dprintf( LOG_LV4, "[check] section_length:%d\n", section_length );
        /* check file position. */
        read_pos = info->file_read.read_position;
        /* buffering section data. */
        if( mpegts_get_table_section_data( &(info->file_read), info->pmt_program_id, section_buffer, section_length ) )
            continue;
        /* check pid list num. */
        int pid_list_num = 0, read_count = 0;
        while( read_count < section_length - TS_PACKET_SECTION_CRC32_SIZE )
        {
            uint8_t *section_data = &(section_buffer[read_count]);
            uint16_t ES_info_length = ((section_data[3] & 0x0F) << 8) | section_data[4];
            /* seek next section. */
            read_count += TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length;
            ++pid_list_num;
        }
        if( (section_length - read_count) != TS_PACKET_SECTION_CRC32_SIZE )
            continue;
        info->pid_list_num_in_pmt = pid_list_num;
        break;
    }
    if( !retry_count )
        return -1;
    /* listup. */
    info->pid_list_in_pmt = malloc( sizeof(mpegts_pid_in_pmt_t) * info->pid_list_num_in_pmt );
    if( !info->pid_list_in_pmt )
        return -1;
    int pid_list_num = 0, read_count = 0;
    while( read_count < section_length - TS_PACKET_SECTION_CRC32_SIZE )
    {
        uint8_t *section_data        = &(section_buffer[read_count]);
        mpeg_stream_type stream_type =   section_data[0];
        /* reserved     3 bit        =  (section_data[1] & 0xE0) >> 5 */
        uint16_t elementary_PID      = ((section_data[1] & 0x1F) << 8) | section_data[2];
        /* reserved     4 bit        =  (section_data[3] & 0xF0) >> 4 */
        uint16_t ES_info_length      = ((section_data[3] & 0x0F) << 8) | section_data[4];
        dprintf( LOG_LV2, "[check] stream_type:0x%02X, elementary_PID:0x%04X, ES_info_length:%u\n"
                 , stream_type, elementary_PID, ES_info_length );
        read_count += TS_PACKET_PMT_SECTION_DATA_SIZE;
        /* check descriptor. */
        uint8_t descriptor_tags[(ES_info_length + 1) / 2];
        uint16_t descriptor_num = 0;
        uint16_t descriptor_read_count = 0;
        uint8_t *descriptor_data = &(section_buffer[read_count]);
        mpeg_descriptor_info_t descriptor_info;
        while( descriptor_read_count < ES_info_length - 2 )
        {
            mpeg_stream_get_descriptor_info( stream_type, &(descriptor_data[descriptor_read_count]), &descriptor_info );
            descriptor_tags[descriptor_num] = descriptor_info.tag;
            uint8_t descriptor_length       = descriptor_info.length;
            dprintf( LOG_LV2, "[check] descriptor_tag:0x%02X, descriptor_length:%u\n"
                     , descriptor_tags[descriptor_num], descriptor_length );
            mpeg_stream_debug_descriptor_info( &descriptor_info );     // FIXME
            /* next descriptor. */
            descriptor_read_count += descriptor_length + 2;
            ++descriptor_num;
        }
        /* setup stream type and PID. */
        info->pid_list_in_pmt[pid_list_num].stream_judge = mpeg_stream_judge_type( stream_type, descriptor_tags, descriptor_num );
        info->pid_list_in_pmt[pid_list_num].stream_type  = stream_type;
        info->pid_list_in_pmt[pid_list_num].program_id   = elementary_PID;
        /* seek next section. */
        read_count += ES_info_length;
        ++pid_list_num;
    }
    uint8_t *crc_32 = &(section_buffer[read_count]);
    dprintf( LOG_LV2, "[check] CRC32:" );
    for( int i = 0; i < TS_PACKET_SECTION_CRC32_SIZE; ++i )
        dprintf( LOG_LV2, " %02X", crc_32[i] );
    dprintf( LOG_LV2, "\n" );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", read_pos );
    /* ready next. */
    mpegts_fseek( &(info->file_read), 0, MPEGTS_SEEK_NEXT );
    info->file_read.sync_byte_position = -1;
    info->file_read.read_position      = read_pos;
    return 0;
}

static int mpegts_get_pcr( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_pcr()\n" );
    info->pcr = -1;
    uint16_t program_id = info->pcr_program_id;
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    int64_t read_pos;
    /* search. */
    mpegts_packet_header_t h;
    do
    {
        if( mpegts_search_program_id_packet( &(info->file_read), &h, program_id ) )
            return -1;
        show_packet_header_info( &h );
        /* check adaptation field. */
        uint8_t adaptation_field_size = 0;
        if( h.adaptation_field_control > 1 )
        {
            mpegts_fread( &adaptation_field_size, 1, &(info->file_read) );
            dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
            if( adaptation_field_size < 7 )
                continue;
            /* check file position. */
            read_pos = info->file_read.read_position;
            /* check adaptation field data. */
            uint8_t adpf_data[adaptation_field_size];
            mpegts_fread( adpf_data, adaptation_field_size, &(info->file_read) );
            /* read header. */
            mpegts_adaptation_field_header_t adpf_header;
            adpf_header.discontinuity_indicator              = !!(adpf_data[0] & 0x80);
            adpf_header.random_access_indicator              = !!(adpf_data[0] & 0x40);
            adpf_header.elementary_stream_priority_indicator = !!(adpf_data[0] & 0x20);
            adpf_header.pcr_flag                             = !!(adpf_data[0] & 0x10);
            adpf_header.opcr_flag                            = !!(adpf_data[0] & 0x08);
            adpf_header.splicing_point_flag                  = !!(adpf_data[0] & 0x04);
            adpf_header.transport_private_data_flag          = !!(adpf_data[0] & 0x02);
            adpf_header.adaptation_field_extension_flag      = !!(adpf_data[0] & 0x01);
            /* calculate PCR. */
            if( adpf_header.pcr_flag )
            {
                int64_t pcr_base, pcr_ext;
                pcr_base = (int64_t)adpf_data[1] << 25
                                  | adpf_data[2] << 17
                                  | adpf_data[3] << 9
                                  | adpf_data[4] << 1
                                  | adpf_data[5] >> 7;
                pcr_ext = (adpf_data[5] & 0x01) << 8 | adpf_data[6];
                dprintf( LOG_LV3, "[check] pcr_base:%"PRId64" pcr_ext:%"PRId64"\n", pcr_base, pcr_ext );
                /* setup. */
                info->pcr = pcr_base + pcr_ext / 300;
            }
            if( adpf_header.opcr_flag )
            {
#define NEED_OPCR_VALUE
#undef NEED_OPCR_VALUE
#ifdef NEED_OPCR_VALUE
                int64_t opcr, opcr_base, opcr_ext;
#else
                int64_t opcr_base, opcr_ext;
#endif
                opcr_base = (int64_t)adpf_data[7]  << 25
                                   | adpf_data[8]  << 17
                                   | adpf_data[9]  << 9
                                   | adpf_data[10] << 1
                                   | adpf_data[11] >> 7;
                opcr_ext = (adpf_data[11] & 0x01) << 8 | adpf_data[12];
                dprintf( LOG_LV3, "[check] opcr_base:%"PRId64" opcr_ext:%"PRId64"\n", opcr_base, opcr_ext );
                /* setup. */
#ifdef NEED_OPCR_VALUE
                opcr = opcr_base + opcr_ext / 300;
#endif
#undef NEED_OPCR_VALUE
            }
        }
        /* ready next. */
        mpegts_fseek( &(info->file_read), 0, MPEGTS_SEEK_NEXT );
    }
    while( info->pcr < 0 );
    dprintf( LOG_LV2, "[check] PCR:%"PRId64" [%"PRId64"ms]\n", info->pcr, info->pcr / 90 );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", read_pos );
    /* ready next. */
    info->file_read.read_position = read_pos;
    return 0;
}

static uint16_t mpegts_get_program_id( mpegts_info_t *info, mpeg_stream_type stream_type )
{
    dprintf( LOG_LV3, "[check] mpegts_get_program_id()\n" );
    /* search program id. */
    int pid_list_index = -1;
    do
    {
        ++pid_list_index;
        if( pid_list_index >= info->pid_list_num_in_pmt )
            return TS_PID_ERR;
    }
    while( info->pid_list_in_pmt[pid_list_index].stream_type != stream_type );
    dprintf( LOG_LV3, "[check] %d - stream_type:%d, PID:0x%04X\n"
                    , pid_list_index, info->pid_list_in_pmt[pid_list_index].stream_type, info->pid_list_in_pmt[pid_list_index].program_id );
    return info->pid_list_in_pmt[pid_list_index].program_id;
}

static int mpegts_get_stream_timestamp( mpegts_file_context_t *file, uint16_t program_id, mpeg_pes_packet_start_code_type start_code, int64_t *pts_set_p, int64_t *dts_set_p )
{
    dprintf( LOG_LV2, "[check] mpegts_get_stream_timestamp()\n" );
    mpegts_packet_header_t h;
    int64_t read_pos = -1;
    /* search packet data. */
    int64_t pts = MPEG_TIMESTAMP_INVALID_VALUE, dts = MPEG_TIMESTAMP_INVALID_VALUE;
    do
    {
        /* seek payload data. */
        int ret = mpegts_seek_packet_payload_data( file, &h, program_id, 1, 0 );
        if( ret < 0 )
            return -1;
        if( ret > 0 )
        {
            /* ready next. */
            mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* check file position. */
        read_pos = file->read_position;
        /* check PES Packet Start Code. */
        uint8_t pes_packet_head_data[PES_PACKET_START_CODE_SIZE];
        mpegts_fread( pes_packet_head_data, PES_PACKET_START_CODE_SIZE, file );
        if( mpeg_pes_check_start_code( pes_packet_head_data, start_code ) )
        {
            /* ready next. */
            mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* check PES packet length, flags. */
        int read_size = PES_PACKET_HEADER_CHECK_SIZE + PES_PACKET_PTS_DTS_DATA_SIZE;
        uint8_t pes_header_check_buffer[read_size];
        mpegts_fread( pes_header_check_buffer, read_size, file );
        mpeg_pes_header_info_t pes_info;
        mpeg_pes_get_header_info( pes_header_check_buffer, &pes_info );
        dprintf( LOG_LV3, "[check] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                 , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        if( !pes_info.pts_flag )
        {
            /* ready next. */
            mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* get PTS and DTS value. */
        uint8_t *pes_packet_pts_dts_data = &(pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE]);
        pts = pes_info.pts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[0]) ) : MPEG_TIMESTAMP_INVALID_VALUE;
        dts = pes_info.dts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[5]) ) : pts;
        dprintf( LOG_LV2, "[check] PTS:%"PRId64" DTS:%"PRId64"\n", pts, dts );
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    while( pts == MPEG_TIMESTAMP_INVALID_VALUE );
    /* setup. */
    *pts_set_p = pts;
    *dts_set_p = dts;
    /* ready next. */
    mpegts_fseek( file, read_pos, MPEGTS_SEEK_RESET );      /* reset start position of detect packet. */
    file->read_position      = read_pos;
    file->sync_byte_position = 0;
    return 0;
}

#define GET_PES_PACKET_HEADER( file, pes )                                              \
{                                                                                       \
    uint8_t pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE];                      \
    /* skip PES Packet Start Code. */                                                   \
    mpegts_fseek( file, PES_PACKET_START_CODE_SIZE, MPEGTS_SEEK_CUR );                  \
    /* get PES packet length, flags. */                                                 \
    mpegts_fread( pes_header_check_buffer, PES_PACKET_HEADER_CHECK_SIZE, file );        \
    mpeg_pes_get_header_info( pes_header_check_buffer, &pes );                          \
}

#define BYTE_DATA_SHIFT( data, size )           \
{                                               \
    for( int i = 1; i < size; ++i )             \
        data[i - 1] = data[i];                  \
}

static int mpegts_get_mpeg_video_picture_info( mpegts_file_context_t *file, uint16_t program_id, mpeg_video_info_t *video_info, int64_t *gop_number )
{
    dprintf( LOG_LV2, "[check] mpegts_get_mpeg_video_picture_info()\n" );
    int result = -1;
    /* parse payload data. */
    mpegts_packet_header_t h;
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    int no_exist_start_indicator = 1;
    do
    {
        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
            return -1;
        /* check start indicator. */
        if( no_exist_start_indicator && !h.payload_unit_start_indicator )
        {
            mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        if( h.payload_unit_start_indicator )
        {
            /* check PES packet length, flags. */
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( file, pes_info )
            dprintf( LOG_LV3, "[debug] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                     , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
            mpegts_fseek( file, pes_info.header_length, MPEGTS_SEEK_CUR );
        }
        /* check Stream Start Code. */
        if( no_exist_start_indicator )
        {
            mpegts_fread( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE - 1, file );
            no_exist_start_indicator = 0;
        }
        /* search. */
        while( file->ts_packet_length )
        {
            mpegts_fread( &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1, file );
            /* check Start Code. */
            if( !mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
            {
                uint8_t identifier;
                mpegts_fread( &identifier, 1, file );
                mpegts_fseek( file, -1, MPEGTS_SEEK_CUR );
                mpeg_video_start_code_info_t start_code_info;
                if( mpeg_video_judge_start_code( mpeg_video_head_data, identifier, &start_code_info ) )
                {
                    BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
                    continue;
                }
                uint32_t read_size = start_code_info.read_size;
                int64_t reset_position = ftello( file->input );
                int32_t rest_ts_packet_length = file->ts_packet_length;
                /* get header/extension information. */
                uint8_t buf[read_size];
                uint8_t *buf_p = buf;
                while( file->ts_packet_length < read_size )
                {
                    if( file->ts_packet_length )
                    {
                        int32_t read = file->ts_packet_length;
                        mpegts_fread( buf_p, read, file );
                        buf_p += read;
                        read_size -= read;
                    }
                    mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
                    if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
                        return -1;
                }
                mpegts_fread( buf_p, read_size, file );
                int32_t check_size = mpeg_video_get_header_info( buf, start_code_info.start_code, video_info );
                if( check_size < start_code_info.read_size )
                {
                    /* reset position. */
                    mpegts_fseek( file, reset_position, MPEGTS_SEEK_SET );
                    file->ts_packet_length = rest_ts_packet_length;
                    int64_t seek_size = check_size;
                    while( file->ts_packet_length < seek_size )
                    {
                        seek_size -= file->ts_packet_length;
                        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
                        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
                            return -1;
                    }
                    mpegts_fseek( file, seek_size, MPEGTS_SEEK_CUR );
                }
                /* debug. */
                mpeg_video_debug_header_info( video_info, start_code_info.searching_status );
                /* check the status detection. */
                if( start_code_info.searching_status == DETECT_GSC )
                    ++(*gop_number);
                else if( start_code_info.searching_status == DETECT_PSC )
                    result = 0;
                else if( start_code_info.searching_status == DETECT_SSC
                      || start_code_info.searching_status == DETECT_SEC )
                    goto end_get_video_picture_info;
                /* cleanup buffer. */
                memset( mpeg_video_head_data, 0xFF, MPEG_VIDEO_START_CODE_SIZE );
            }
            BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
        }
        dprintf( LOG_LV4, "[debug] continue next packet. buf:0x%02X 0x%02X 0x%02X 0x--\n"
                        , mpeg_video_head_data[0], mpeg_video_head_data[1], mpeg_video_head_data[2] );
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
end_get_video_picture_info:
    return result;
}

static uint32_t mpegts_get_sample_packets_num( mpegts_file_context_t *file, uint16_t program_id, mpeg_stream_type stream_type )
{
    dprintf( LOG_LV3, "[debug] mpegts_get_sample_packets_num()\n" );
    mpegts_packet_header_t h;
    /* skip first packet. */
    if( mpegts_search_program_id_packet( file, &h, program_id ) )
        return 0;
    mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    /* count. */
    uint32_t ts_packet_count = 0;
    int8_t old_continuity_counter = h.continuity_counter;
    do
    {
        ++ts_packet_count;
        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
            break;
        if( h.continuity_counter != ((old_continuity_counter + 1) & 0x0F) )
            dprintf( LOG_LV3, "[debug] detect Drop!  ts_packet_count:%u  continuity_counter:%u --> %u\n", ts_packet_count, old_continuity_counter, h.continuity_counter );
        old_continuity_counter = h.continuity_counter;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( file, pes_info )
            /* check PES packet header. */
            if( pes_info.pts_flag )
            {
                /* set next start position. */
                mpegts_fseek( file, file->ts_packet_length - TS_PACKET_SIZE, MPEGTS_SEEK_CUR );
                break;
            }
            dprintf( LOG_LV3, "[debug] Detect PES packet doesn't have PTS.\n"
                              "        PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                            , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        }
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
    dprintf( LOG_LV3, "[debug] ts_packet_count:%u\n", ts_packet_count );
    return ts_packet_count;
}

typedef struct {
    uint32_t                data_size;
    int32_t                 read_offset;
} sample_raw_data_info_t;

static int mpegts_get_sample_raw_data_info( mpegts_file_context_t *file, uint16_t program_id, mpeg_stream_type stream_type, mpeg_stream_group_type stream_judge, sample_raw_data_info_t *raw_data_info )
{
    dprintf( LOG_LV3, "[debug] mpegts_get_sample_raw_data_info()\n" );
    uint32_t raw_data_size = 0;
    int32_t start_point = -1;
    /* ready. */
    int64_t start_position = ftello( file->input );
    /* search. */
    int check_start_point = 0;
    int32_t stream_header_check_size = mpeg_stream_get_header_check_size( stream_judge );
    uint8_t check_buffer[TS_PACKET_SIZE + stream_header_check_size];
    uint8_t *buffer_p = check_buffer;
    int32_t buffer_read_offset = 0;
    int32_t buffer_read_size = 0;
    mpegts_packet_header_t h;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
        {
            if( start_point >= 0 )
                /* This sample is the data at the end was shaved. */
                raw_data_size += buffer_read_size + buffer_read_offset;
            else
                start_point = 0;
            dprintf( LOG_LV3, "[debug] read file end.\n" );
            goto end_get_info;
        }
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( file, pes_info )
            /* check PES packet header. */
            if( pes_info.pts_flag )
                check_start_point = 1;
            /* skip PES packet header. */
            mpegts_fseek( file, pes_info.header_length, MPEGTS_SEEK_CUR );
        }
        if( check_start_point )
        {
            /* buffering check data . */
            int32_t buffer_size = file->ts_packet_length + buffer_read_offset;
            if( file->ts_packet_length )
            {
                mpegts_fread( check_buffer + buffer_read_offset, file->ts_packet_length, file );
                buffer_p = check_buffer;
            }
            int32_t header_offset = mpeg_stream_check_header( stream_type, stream_judge, !(start_point < 0), buffer_p, buffer_size );
            if( header_offset < 0 )
            {
                /* continue buffering check. */
                buffer_read_size += buffer_size;
                for( int j = 0; j < stream_header_check_size; ++j )
                    check_buffer[j] = buffer_p[buffer_size - stream_header_check_size + j];
                buffer_read_size -= stream_header_check_size;
                buffer_read_offset = stream_header_check_size;
            }
            else
            {
                buffer_read_offset = 0;
                if( start_point < 0 )
                {
                    /* detect start point. */
                    start_point = buffer_read_size + header_offset;
                    raw_data_size = buffer_size - header_offset;
                }
                else
                {
                    raw_data_size += buffer_read_size + header_offset;
                    /* detect end point(=next start point). */
                    mpegts_fseek( file, file->packet_size - TS_PACKET_SIZE, MPEGTS_SEEK_CUR );
                    break;
                }
                buffer_read_size = 0;
                check_start_point = 0;
            }
        }
        else
            raw_data_size += file->ts_packet_length;
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
end_get_info:
    mpegts_fseek( file, start_position, MPEGTS_SEEK_RESET );
    /* setup. */
    raw_data_info->data_size   = raw_data_size;
    raw_data_info->read_offset = start_point;
    return 0;
}

static void mpegts_get_sample_raw_data( mpegts_file_context_t *file, uint16_t program_id, mpeg_stream_type stream_type, mpeg_stream_group_type stream_judge, uint32_t raw_data_size, int32_t read_offset, uint8_t **buffer, uint32_t *read_size )
{
    dprintf( LOG_LV2, "[check] mpegts_get_sample_raw_data()\n" );
    if( !raw_data_size )
        return;
    /* allocate buffer. */
    *buffer = malloc( raw_data_size );
    if( !(*buffer) )
        return;
    dprintf( LOG_LV3, "[debug] buffer_size:%u\n", raw_data_size );
    /* read. */
    mpegts_packet_header_t h;
    *read_size = 0;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
            break;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( file, pes_info )
            /* skip PES packet header. */
            mpegts_fseek( file, pes_info.header_length, MPEGTS_SEEK_CUR );
        }
        /* check read start point. */
        if( read_offset )
        {
            if( read_offset > file->ts_packet_length )
            {
                read_offset -= file->ts_packet_length;
                mpegts_fseek( file, file->ts_packet_length, MPEGTS_SEEK_CUR );
            }
            else
            {
                mpegts_fseek( file, read_offset, MPEGTS_SEEK_CUR );
                read_offset = 0;
            }
        }
        /* read raw data. */
        if( file->ts_packet_length > 0 )
        {
            if( raw_data_size > *read_size + file->ts_packet_length )
            {
                int32_t read = file->ts_packet_length;
                mpegts_fread( *buffer + *read_size, read, file );
                *read_size += read;
            }
            else
            {
                mpegts_fread( *buffer + *read_size, raw_data_size - *read_size, file );
                *read_size = raw_data_size;
                mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
                break;
            }
        }
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
    return;
}

static void mpegts_get_sample_pes_packet_data( mpegts_file_context_t *file, uint16_t program_id, uint32_t ts_packet_count, uint8_t **buffer, uint32_t *read_size )
{
    dprintf( LOG_LV2, "[check] mpegts_get_sample_pes_packet_data()\n" );
    /* allocate buffer. */
    uint32_t sample_size = ts_packet_count * TS_PACKET_SIZE;
    *buffer = malloc( sample_size );
    if( !(*buffer) )
        return;
    dprintf( LOG_LV3, "[debug] buffer_size:%u\n", sample_size );
    /* read. */
    mpegts_packet_header_t h;
    *read_size = 0;
    for( uint32_t i = 0; i < ts_packet_count; ++i )
    {
        if( mpegts_seek_packet_payload_data( file, &h, program_id, 0, 1 ) )
            return;
        /* read packet data. */
        if( file->ts_packet_length > 0 )
        {
            int32_t read = file->ts_packet_length;
            mpegts_fread( *buffer + *read_size, read, file );
            *read_size += read;
        }
        /* ready next. */
        mpegts_fseek( file, 0, MPEGTS_SEEK_NEXT );
    }
}

static void mpegts_get_sample_ts_packet_data( mpegts_file_context_t *file, uint16_t program_id, uint32_t ts_packet_count, uint8_t **buffer, uint32_t *read_size )
{
    dprintf( LOG_LV2, "[check] mpegts_get_sample_ts_packet_data()\n" );
    /* allocate buffer. */
    uint32_t sample_size = ts_packet_count * TS_PACKET_SIZE;
    *buffer = malloc( sample_size );
    if( !(*buffer) )
        return;
    dprintf( LOG_LV3, "[debug] buffer_size:%u\n", sample_size );
    /* read. */
    mpegts_packet_header_t h;
    *read_size = 0;
    for( uint32_t i = 0; i < ts_packet_count; ++i )
    {
        /* read packet data. */
        mpegts_fread( *buffer + *read_size, TS_PACKET_SIZE, file );
        *read_size += TS_PACKET_SIZE;
        /* seek next packet. */
        if( mpegts_search_program_id_packet( file, &h, program_id ) )
            break;
        mpegts_fseek( file, -(TS_PACKET_HEADER_SIZE), MPEGTS_SEEK_CUR );
    }
}

static mpeg_stream_type get_sample_stream_type( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return STREAM_INVAILED;
    /* check stream type. */
    mpeg_stream_type stream_type = STREAM_INVAILED;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < info->video_stream_num )
        stream_type = info->video_stream[stream_number].stream_type;
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < info->audio_stream_num )
        stream_type = info->audio_stream[stream_number].stream_type;
    return stream_type;
}

static void free_sample_buffer( uint8_t **buffer )
{
    if( !buffer )
        return;
    if( *buffer )
        free( *buffer );
    buffer = NULL;
}

static int get_sample_data( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, int64_t position, uint32_t sample_size, int32_t read_offset, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    /* check program id. */
    mpegts_file_context_t *file_read = NULL;
    uint16_t program_id = TS_PID_ERR;
    mpeg_stream_type stream_type = STREAM_INVAILED;
    mpeg_stream_group_type stream_judge = STREAM_IS_UNKNOWN;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < info->video_stream_num )
    {
        file_read    = &(info->video_stream[stream_number].file_read);
        stream_judge =   info->video_stream[stream_number].stream_judge;
        stream_type  =   info->video_stream[stream_number].stream_type;
        program_id   =   info->video_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < info->audio_stream_num )
    {
        file_read    = &(info->audio_stream[stream_number].file_read);
        stream_judge =   info->audio_stream[stream_number].stream_judge;
        stream_type  =   info->audio_stream[stream_number].stream_type;
        program_id   =   info->audio_stream[stream_number].program_id;
    }
    else
        return -1;
    /* seek reading start position. */
    mpegts_fseek( file_read, position, MPEGTS_SEEK_RESET );
    file_read->sync_byte_position = 0;
    /* get data. */
    uint32_t ts_packet_count = sample_size / TS_PACKET_SIZE;
    uint8_t *buffer = NULL;
    uint32_t read_size = 0;
    switch( get_mode )
    {
        case GET_SAMPLE_DATA_CONTAINER :
            mpegts_get_sample_ts_packet_data( file_read, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_PES_PACKET :
            mpegts_get_sample_pes_packet_data( file_read, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_RAW :
            mpegts_get_sample_raw_data( file_read, program_id, stream_type, stream_judge, sample_size, read_offset,  &buffer, &read_size );
        default :
            break;
    }
    if( !buffer )
        return -1;
    dprintf( LOG_LV3, "[debug] read_size:%d\n", read_size );
    *dst_buffer    = buffer;
    *dst_read_size = read_size;
    return 0;
}

static int64_t get_sample_position( void *ih )
{
#if 0       // FIXME
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    return ftello( info->file_read.input );
#else
    return -1;
#endif
}

static int set_sample_position( void *ih, int64_t position )
{
#if 0       // FIXME
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    mpegts_fseek( &(info->file_read), position, MPEGTS_SEEK_SET );
    info->file_read.sync_byte_position = -1;
//    info->file_read.read_position    = position;
//    info->file_read.ts_packet_length = TS_PACKET_SIZE;
    return 0;
#else
    return -1;
#endif
}

static int seek_next_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    int64_t seek_position = -1;
    mpegts_file_context_t *file_read = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < info->video_stream_num )
    {
        file_read     = &(info->video_stream[stream_number].file_read);
        seek_position =   info->video_stream[stream_number].file_read.read_position;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < info->audio_stream_num )
    {
        file_read     = &(info->audio_stream[stream_number].file_read);
        seek_position =   info->audio_stream[stream_number].file_read.read_position;
    }
    if( seek_position < 0 )
        return -1;
    mpegts_fseek( file_read, seek_position, MPEGTS_SEEK_RESET );
    return 0;
}

static uint32_t get_stream_num( void *ih, mpeg_sample_type sample_type )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return 0;
    uint32_t stream_num = 0;
    if( sample_type == SAMPLE_TYPE_VIDEO )
        stream_num = info->video_stream_num;
    else if( sample_type == SAMPLE_TYPE_AUDIO )
        stream_num = info->audio_stream_num;
    return stream_num;
}

static int64_t get_pcr( void *ih )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    if( info->pcr < 0 )
    {
        int64_t start_position = ftello( info->file_read.input );
        int result = mpegts_get_pcr( info );
        mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
        if( result )
            return -1;
    }
    return info->pcr;
}

static int get_video_info( void *ih, uint8_t stream_number, video_sample_info_t *video_sample_info )
{
    dprintf( LOG_LV2, "[mpegts_parser] get_video_info()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || stream_number >= info->video_stream_num )
        return -1;
    mpegts_file_context_t *file_read    = &(info->video_stream[stream_number].file_read);
    uint16_t program_id                 =   info->video_stream[stream_number].program_id;
    mpeg_stream_type stream_type        =   info->video_stream[stream_number].stream_type;
    mpeg_stream_group_type stream_judge =   info->video_stream[stream_number].stream_judge;
    int64_t *video_stream_gop_number    = &(info->video_stream[stream_number].gop_number);
    /* check PES start code. */
    mpeg_pes_packet_start_code_type start_code = mpeg_pes_get_stream_start_code( stream_judge );
    /* get timestamp. */
    int64_t pts = MPEG_TIMESTAMP_INVALID_VALUE, dts = MPEG_TIMESTAMP_INVALID_VALUE;
    if( mpegts_get_stream_timestamp( file_read, program_id, start_code, &pts, &dts ) )
        return -1;
    int64_t start_position = file_read->read_position;
    /* check raw data. */
    sample_raw_data_info_t raw_data_info = { 0 };
    if( mpegts_get_sample_raw_data_info( file_read, program_id, stream_type, stream_judge, &raw_data_info ) )
        return -1;
    dprintf( LOG_LV4, "[debug] raw_data  size:%u  read_offset:%d\n", raw_data_info.data_size, raw_data_info.read_offset );
    /* parse payload data. */
    int64_t gop_number = -1;
    uint8_t progressive_sequence = 0;
    uint8_t closed_gop = 0;
    uint8_t picture_coding_type = MPEG_VIDEO_UNKNOWN_FRAME;
    int16_t temporal_reference = -1;
    uint8_t picture_structure = 0;
    uint8_t progressive_frame = 0;
    uint8_t repeat_first_field = 0;
    uint8_t top_field_first = 0;
    if( (stream_judge & STREAM_IS_MPEG_VIDEO) == STREAM_IS_MPEG_VIDEO )
    {
        mpeg_video_info_t video_info;
        //memset( &video_info, 0, sizeof(mpeg_video_info_t) );
        if( !mpegts_get_mpeg_video_picture_info( file_read, program_id, &video_info, video_stream_gop_number ) )
        {
            gop_number           = *video_stream_gop_number;
            progressive_sequence = video_info.sequence_ext.progressive_sequence;
            closed_gop           = video_info.gop.closed_gop;
            picture_coding_type  = video_info.picture.picture_coding_type;
            temporal_reference   = video_info.picture.temporal_reference;
            picture_structure    = video_info.picture_coding_ext.picture_structure;
            progressive_frame    = video_info.picture_coding_ext.progressive_frame;
            repeat_first_field   = video_info.picture_coding_ext.repeat_first_field;
            top_field_first      = video_info.picture_coding_ext.top_field_first;
        }
    }
    else
    {
        gop_number = 0;     // FIXME
    }
    /* check packets num. */
    mpegts_fseek( file_read, start_position, MPEGTS_SEEK_RESET );
    file_read->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( file_read, program_id, stream_type );
    if( !ts_packet_count )
        return -1;
    /* setup. */
    video_sample_info->file_position        = start_position;
    video_sample_info->sample_size          = TS_PACKET_SIZE * ts_packet_count;
    video_sample_info->raw_data_size        = raw_data_info.data_size;
    video_sample_info->raw_data_read_offset = raw_data_info.read_offset;
    video_sample_info->pts                  = pts;
    video_sample_info->dts                  = dts;
    video_sample_info->gop_number           = gop_number;
    video_sample_info->progressive_sequence = progressive_sequence;
    video_sample_info->closed_gop           = closed_gop;
    video_sample_info->picture_coding_type  = picture_coding_type;
    video_sample_info->temporal_reference   = temporal_reference;
    video_sample_info->picture_structure    = picture_structure;
    video_sample_info->progressive_frame    = progressive_frame;
    video_sample_info->repeat_first_field   = repeat_first_field;
    video_sample_info->top_field_first      = top_field_first;
    static const char frame[4] = { '?', 'I', 'P', 'B' };
    dprintf( LOG_LV2, "[check] Video PTS:%"PRId64" [%"PRId64"ms], [%c] temporal_reference:%d\n"
                    , pts, pts / 90, frame[picture_coding_type], temporal_reference );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", start_position );
    /* ready next. */
    file_read->sync_byte_position = -1;
    file_read->read_position      = ftello( file_read->input );
    return 0;
}

static int get_audio_info( void *ih, uint8_t stream_number, audio_sample_info_t *audio_sample_info )
{
    dprintf( LOG_LV2, "[mpegts_parser] get_audio_info()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || stream_number >= info->audio_stream_num )
        return -1;
    mpegts_file_context_t *file_read    = &(info->audio_stream[stream_number].file_read);
    uint16_t program_id                 =   info->audio_stream[stream_number].program_id;
    mpeg_stream_type stream_type        =   info->audio_stream[stream_number].stream_type;
    mpeg_stream_group_type stream_judge =   info->audio_stream[stream_number].stream_judge;
    /* check PES start code. */
    mpeg_pes_packet_start_code_type start_code = mpeg_pes_get_stream_start_code( stream_judge );
    /* get timestamp. */
    int64_t pts = MPEG_TIMESTAMP_INVALID_VALUE, dts = MPEG_TIMESTAMP_INVALID_VALUE;
    if( mpegts_get_stream_timestamp( file_read, program_id, start_code, &pts, &dts ) )
        return -1;
    int64_t start_position = file_read->read_position;
    /* check raw data. */
    sample_raw_data_info_t raw_data_info = { 0 };
    if( mpegts_get_sample_raw_data_info( file_read, program_id, stream_type, stream_judge, &raw_data_info ) )
        return -1;
    dprintf( LOG_LV4, "[debug] raw_data  size:%u  read_offset:%d\n", raw_data_info.data_size, raw_data_info.read_offset );
    /* check packets num. */
    //mpegts_fseek( file_read, start_position, MPEGTS_SEEK_RESET );
    //file_read->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( file_read, program_id, stream_type );
    if( !ts_packet_count )
        return -1;
    /* setup. */
    audio_sample_info->file_position        = start_position;
    audio_sample_info->sample_size          = TS_PACKET_SIZE * ts_packet_count;
    audio_sample_info->raw_data_size        = raw_data_info.data_size;
    audio_sample_info->raw_data_read_offset = raw_data_info.read_offset;
    audio_sample_info->pts                  = pts;
    audio_sample_info->dts                  = dts;
    audio_sample_info->sampling_frequency   = 0;        // FIXME
    audio_sample_info->bitrate              = 0;        // FIXME
    audio_sample_info->channel              = 0;        // FIXME
    audio_sample_info->layer                = 0;        // FIXME
    dprintf( LOG_LV2, "[check] Audio PTS:%"PRId64" [%"PRId64"ms]\n", pts, pts / 90 );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", start_position );
    /* ready next. */
    file_read->sync_byte_position = -1;
    file_read->read_position = ftello( file_read->input );
    return 0;
}

static int set_pmt_stream_info( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] [sub] set_pmt_stream_info()\n" );
    int64_t start_position = ftello( info->file_read.input );
    mpegts_packet_header_t h;
    /* check stream num. */
    uint32_t video_stream_num = 0, audio_stream_num = 0;
    for( int pid_list_index = 0; pid_list_index < info->pid_list_num_in_pmt; ++pid_list_index )
    {
        mpeg_stream_group_type stream_judge = info->pid_list_in_pmt[pid_list_index].stream_judge;
        if( stream_judge & STREAM_IS_VIDEO )
            ++video_stream_num;
        else if( stream_judge & STREAM_IS_AUDIO )
            ++audio_stream_num;
    }
    if( !video_stream_num && !audio_stream_num )
        return -1;
    /* allocate streams context. */
    mpegts_stream_context_t *video_context = NULL, *audio_context = NULL;
    if( video_stream_num )
        video_context = (mpegts_stream_context_t *)malloc( sizeof(mpegts_stream_context_t) * video_stream_num );
    if( video_stream_num )
        audio_context = (mpegts_stream_context_t *)malloc( sizeof(mpegts_stream_context_t) * audio_stream_num );
    if( (video_stream_num && !video_context)
     || (audio_stream_num && !audio_context) )
        goto fail_set_pmt_stream_info;
    /* check exist. */
    video_stream_num = audio_stream_num = 0;
    for( int pid_list_index = 0; pid_list_index < info->pid_list_num_in_pmt; ++pid_list_index )
    {
        uint16_t program_id                 = info->pid_list_in_pmt[pid_list_index].program_id;
        mpeg_stream_type stream_type        = info->pid_list_in_pmt[pid_list_index].stream_type;
        mpeg_stream_group_type stream_judge = info->pid_list_in_pmt[pid_list_index].stream_judge;
        /*  */
        static const char* stream_name[2] = { "video", "audio" };
        int index;
        mpegts_stream_context_t *stream = NULL;
        uint32_t *stream_num;
        if( stream_judge & STREAM_IS_VIDEO )
        {
            stream     = &(video_context[video_stream_num]);
            stream_num = &video_stream_num;
            index      = 0;
        }
        else if( stream_judge & STREAM_IS_AUDIO )
        {
            stream     = &(audio_context[audio_stream_num]);
            stream_num = &audio_stream_num;
            index      = 1;
        }
        if( stream )
        {
            if( !mpegts_search_program_id_packet( &(info->file_read), &h, program_id ) )
            {
                FILE *input = fopen( info->mpegts, "rb" );
                if( input )
                {
                    stream->file_read.input                  = input;
                    stream->file_read.packet_size            = info->file_read.packet_size;
                    stream->file_read.sync_byte_position     = -1;
                    stream->file_read.read_position          = 0;
                    stream->file_read.ts_packet_length       = TS_PACKET_SIZE;
                    stream->file_read.packet_check_count_num = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
                    stream->program_id                       = program_id;
                    stream->stream_type                      = stream_type;
                    stream->stream_judge                     = stream_judge;
                    stream->gop_number                       = -1;
                    dprintf( LOG_LV2, "[check] %s PID:0x%04X  stream_type:0x%02X\n", stream_name[index], program_id, stream_type );
                    mpegts_fseek( &(stream->file_read), info->file_read.read_position, MPEGTS_SEEK_SET );
                    ++(*stream_num);
                }
            }
            mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
        }
    }
    if( !video_stream_num )
    {
        free( video_context );
        video_context = NULL;
    }
    if( !audio_stream_num )
    {
        free( audio_context );
        audio_context = NULL;
    }
    if( !video_context && !audio_context )
        return -1;
    /* setup. */
    info->video_stream     = video_context;
    info->audio_stream     = audio_context;
    info->video_stream_num = video_stream_num;
    info->audio_stream_num = audio_stream_num;
    return 0;
fail_set_pmt_stream_info:
    if( video_context )
        free( video_context );
    if( audio_context )
        free( audio_context );
    return -1;
}

static void release_stream_handle( mpegts_info_t *info )
{
    if( info->video_stream )
    {
        for( int32_t i = 0; i <  info->video_stream_num; ++i )
            fclose( info->video_stream[i].file_read.input );
        free( info->video_stream );
        info->video_stream     = NULL;
        info->video_stream_num = 0;
    }
    if( info->audio_stream )
    {
        for( int32_t i = 0; i <  info->audio_stream_num; ++i )
            fclose( info->audio_stream[i].file_read.input );
        free( info->audio_stream );
        info->audio_stream     = NULL;
        info->audio_stream_num = 0;
    }
}

static void release_pid_list( mpegts_info_t *info )
{
    release_stream_handle( info );
    if( info->pid_list_in_pmt )
    {
        free( info->pid_list_in_pmt );
        info->pid_list_in_pmt = NULL;
    }
    if( info->pid_list_in_pat )
    {
        free( info->pid_list_in_pat );
        info->pid_list_in_pat = NULL;
    }
    info->status = PARSER_STATUS_NON_PARSING;
}

static int parse_pmt_info( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] [sub] parse_pmt_info()\n" );
    if( mpegts_parse_pmt( info ) )
        goto fail_parse;
    if( mpegts_get_pcr( info ) )
        goto fail_parse;
    return 0;
fail_parse:
    release_pid_list( info );
    return -1;
}

static int set_pmt_program_id( mpegts_info_t *info, uint16_t program_id )
{
    dprintf( LOG_LV2, "[check] set_pmt_program_id()\n" );
    if( info->pid_list_in_pat )
        free( info->pid_list_in_pat );
    info->pid_list_in_pat = malloc( sizeof(uint16_t) );
    if( !info->pid_list_in_pat )
        return -1;
    info->pid_list_num_in_pat = 1;
    info->pid_list_in_pat[0]  = program_id;
    if( info->status == PARSER_STATUS_NON_PARSING )
        return 0;
    /* reset pmt information. */
    info->file_read.sync_byte_position = 0;
    info->file_read.read_position      = -1;
    info->pcr_program_id               = TS_PID_ERR;
    info->pcr                          = -1;
    release_stream_handle( info );
    if( info->pid_list_in_pmt )
    {
        free( info->pid_list_in_pmt );
        info->pid_list_in_pmt = NULL;
    }
    int64_t start_position = ftello( info->file_read.input );
    int result = result = parse_pmt_info( info );
    if( result )
        goto end_reset;
    mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
    result = set_pmt_stream_info( info );
end_reset:
    mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
    return result;
}

static int set_stream_program_id( mpegts_info_t *info, uint16_t program_id )
{
    dprintf( LOG_LV2, "[check] set_stream_program_id()\n" );
    /* search program id and stream type. */
    int result = -1;
#if 0       // FIXME
    for( int pid_list_index = 0; pid_list_index < info->pid_list_num_in_pmt; ++pid_list_index )
    {
        if( info->pid_list_in_pmt[pid_list_index].program_id == program_id )
        {
            mpeg_stream_group_type stream_judge = set_stream_info( info, info->pid_list_in_pmt[pid_list_index].program_id, info->pid_list_in_pmt[pid_list_index].stream_type, info->pid_list_in_pmt[pid_list_index].stream_judge );
            if( (stream_judge & STREAM_IS_VIDEO) || (stream_judge & STREAM_IS_AUDIO) )
                result = 0;
        }
    }
#endif
    return result;
}

static int set_program_id( void *ih, mpegts_select_pid_type pid_type, uint16_t program_id )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    dprintf( LOG_LV2, "[mpegts_parser] set_program_id()\n"
                      "[check] program_id: 0x%04X\n", program_id );
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
    {
        dprintf( LOG_LV2, "[check] illegal PID is specified. using PID in PAT.\n" );
        return 1;
    }
    int result = -1;
    switch( pid_type )
    {
        case PID_TYPE_PMT :
            result = set_pmt_program_id( info, program_id );
            break;
        case PID_TYPE_VIDEO :
        case PID_TYPE_AUDIO :
            result = set_stream_program_id( info, program_id );
            break;
        case PID_TYPE_PAT :
        default :
            break;
    }
    return result;
}

static uint16_t get_program_id( void *ih, mpeg_stream_type stream_type )
{
    dprintf( LOG_LV2, "[mpegts_parser] get_program_id()\n"
                      "[check] stream_type: 0x%02X\n", stream_type );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return TS_PID_ERR;
    return mpegts_get_program_id( info, stream_type );
}

static int parse( void *ih )
{
    dprintf( LOG_LV2, "[mpegts_parser] parse()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    if( info->status == PARSER_STATUS_PARSED )
        release_pid_list( info );
    int result = -1;
    int64_t start_position = ftello( info->file_read.input );
    if( !info->pid_list_in_pat && mpegts_parse_pat( info ) )
        goto end_parse;
    result = parse_pmt_info( info );
    if( result )
        goto end_parse;
    mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
    result = set_pmt_stream_info( info );
end_parse:
    if( !result )
        info->status = PARSER_STATUS_PARSED;
    else
        dprintf( LOG_LV2, "[mpegts_parser] failed parse.\n" );
    mpegts_fseek( &(info->file_read), start_position, MPEGTS_SEEK_RESET );
    return result;
}

static void *initialize( const char *input_file )
{
    dprintf( LOG_LV2, "[mpegts_parser] initialize()\n" );
    mpegts_info_t *info = calloc( sizeof(mpegts_info_t), 1 );
    char *mpegts = strdup( input_file );
    FILE *input = fopen( input_file, "rb" );
    if( !info || !mpegts || !input )
        goto fail_initialize;
    /* initialize. */
    info->mpegts                           = mpegts;
    info->file_read.input                  = input;
    info->file_read.packet_size            = TS_PACKET_SIZE;
    info->file_read.sync_byte_position     = -1;
    info->file_read.read_position          = 0;
    info->file_read.ts_packet_length       = TS_PACKET_SIZE;
    info->file_read.packet_check_count_num = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
    info->packet_check_retry_num           = TS_PACKET_SEARCH_RETRY_COUNT_NUM;
    info->pcr_program_id                   = TS_PID_ERR;
    info->pcr                              = -1;
    /* first check. */
    if( mpegts_first_check( &(info->file_read) ) )
        goto fail_initialize;
    return info;
fail_initialize:
    dprintf( LOG_LV2, "[mpegts_parser] failed initialize.\n" );
    if( input )
        fclose( input );
    if( mpegts )
        free( mpegts );
    if( info )
        free( info );
    return NULL;
}

static void release( void *ih )
{
    dprintf( LOG_LV2, "[mpegts_parser] release()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return;
    /*  release. */
    release_pid_list( info );
    fclose( info->file_read.input );
    free( info->mpegts );
    free( info );
}

mpeg_parser_t mpegts_parser = {
    initialize,
    release,
    parse,
    set_program_id,
    get_program_id,
    get_video_info,
    get_audio_info,
    get_pcr,
    get_stream_num,
    get_sample_position,
    set_sample_position,
    seek_next_sample_position,
    get_sample_data,
    free_sample_buffer,
    get_sample_stream_type
};
