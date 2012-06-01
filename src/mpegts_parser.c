/*****************************************************************************
 * mpegts_parser.c
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
    uint8_t         stream_type;
    uint16_t        program_id;
} mpegts_pid_in_pmt_t;

typedef struct {
    parser_status_type      status;
    FILE                   *input;
    int32_t                 packet_size;
    int32_t                 sync_byte_position;
    int64_t                 read_position;
    int64_t                 video_position;
    int64_t                 audio_position;
    int32_t                 pid_list_num_in_pat;
    uint16_t               *pid_list_in_pat;
    int32_t                 pid_list_num_in_pmt;
    mpegts_pid_in_pmt_t    *pid_list_in_pmt;
    uint32_t                packet_check_count_num;
    uint32_t                packet_check_retry_num;
    uint16_t                pmt_program_id;
    uint16_t                pcr_program_id;
    uint16_t                video_program_id;
    uint16_t                audio_program_id;
    uint8_t                 video_stream_type;
    uint8_t                 audio_stream_type;
    int64_t                 pcr;
    int64_t                 gop_number;
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

static int mpegts_seek_sync_byte_position( mpegts_info_t *info )
{
    if( !info->sync_byte_position )
        return 0;
    if( info->sync_byte_position < 0 )
        info->sync_byte_position = mpegts_check_sync_byte_position( info->input, info->packet_size, 1 );
    if( info->sync_byte_position < 0 || fseeko( info->input, info->sync_byte_position, SEEK_CUR ) )
        return -1;
    info->sync_byte_position = 0;
    return 0;
}

static int mpegts_first_check( mpegts_info_t *info )
{
    int result = -1;
    dprintf( LOG_LV2, "[check] mpegts_first_check()\n" );
    for( int i = 0; i < TS_PACKET_TYPE_NUM; ++i )
    {
        static const int32_t mpegts_packet_size[TS_PACKET_TYPE_NUM] =
            {
                TS_PACKET_SIZE, TTS_PACKET_SIZE, FEC_TS_PACKET_SIZE
            };
        int32_t position = mpegts_check_sync_byte_position( info->input, mpegts_packet_size[i], TS_PACKET_FIRST_CHECK_COUNT_NUM );
        if( position != -1 )
        {
            info->packet_size        = mpegts_packet_size[i];
            info->sync_byte_position = position;
            result = 0;
            break;
        }
    }
    dprintf( LOG_LV3, "[check] packet size:%d\n", info->packet_size );
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

static int mpegts_read_packet_header( mpegts_info_t *info, mpegts_packet_header_t *h )
{
    if( mpegts_seek_sync_byte_position( info ) )
        return -1;
    uint8_t ts_header[TS_PACKET_HEADER_SIZE];
    fread( ts_header, 1, TS_PACKET_HEADER_SIZE, info->input );
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
    info->sync_byte_position = -1;
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

static int mpegts_search_program_id_packet( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id )
{
    int check_count = info->packet_check_count_num;
    do
    {
        if( !check_count )
            return 1;
        --check_count;
        if( mpegts_read_packet_header( info, h ) )
            return -1;
        if( h->program_id == search_program_id )
            break;
        /* seek next packet head. */
        fseeko( info->input, info->packet_size - TS_PACKET_HEADER_SIZE, SEEK_CUR );
    }
    while( 1 );
    return 0;
}

#define SKIP_ADAPTATION_FIELD( i, h, l, s )         \
{                                                   \
    if( (h).adaptation_field_control > 1 )          \
    {                                               \
        fread( &s, 1, 1, i->input );                \
        fseeko( i->input, s, SEEK_CUR );            \
        (l) -= 1 + s;                               \
    }                                               \
}

static int mpegts_get_table_section_header( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id, uint8_t *section_header, uint16_t section_header_length, int32_t *ts_packet_length )
{
    dprintf( LOG_LV4, "[check] mpegts_get_table_section_header()\n" );
    do
    {
        int search_result = mpegts_search_program_id_packet( info, h, search_program_id );
        if( search_result )
            return search_result;
        show_packet_header_info( h );
    }
    while( !h->payload_unit_start_indicator );
    *ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( info, *h, *ts_packet_length, adaptation_field_size )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    /* check pointer field. */
    uint8_t pointer_field;
    fread( &pointer_field, 1, 1, info->input );
    -- *ts_packet_length;
    if( pointer_field )
    {
        fseeko( info->input, pointer_field, SEEK_CUR );
        *ts_packet_length -= pointer_field;
    }
    /* read section header. */
    fread( section_header, 1, section_header_length, info->input );
    *ts_packet_length -= section_header_length;
    return 0;
}

static int mpegts_seek_packet_payload_data( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id, int32_t *ts_packet_length, int indicator_check, int indicator_status )
{
    dprintf( LOG_LV4, "[check] mpegts_seek_packet_payload_data()\n" );
    if( mpegts_search_program_id_packet( info, h, search_program_id ) )
        return -1;
    show_packet_header_info( h );
    /* check start indicator. */
    if( indicator_check && (indicator_status == h->payload_unit_start_indicator) )
        return 1;
    *ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( info, *h, *ts_packet_length, adaptation_field_size )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    return 0;
}

static int mpegts_get_table_section_data( mpegts_info_t *info, uint16_t search_program_id, uint8_t *section_buffer, uint16_t section_length, int32_t rest_ts_packet_length )
{
    mpegts_packet_header_t h;
    fpos_t start_pos;
    fgetpos( info->input, &start_pos );
    /* buffering payload data. */
    int32_t ts_packet_length = rest_ts_packet_length;
    int read_count = 0;
    int need_ts_packet_payload_data = 0;
    while( read_count < section_length )
    {
        if( need_ts_packet_payload_data )
            /* seek next packet payload data. */
            if( mpegts_seek_packet_payload_data( info, &h, search_program_id, &ts_packet_length, 1, 1 ) )
                return -1;
        need_ts_packet_payload_data = 1;
        int read_size = (section_length - read_count > ts_packet_length) ? ts_packet_length : section_length - read_count;
        fread( &(section_buffer[read_count]), 1, read_size, info->input );
        ts_packet_length -= read_size;
        read_count += read_size;
        dprintf( LOG_LV4, "[check] section data read:%d, rest_packet:%d\n", read_size, ts_packet_length );
        /* ready next. */
        info->sync_byte_position = ts_packet_length + info->packet_size - TS_PACKET_SIZE;
    }
    /* reset buffering start packet position. */
    fsetpos( info->input, &start_pos );
    return 0;
}

static int mpegts_search_pat_packet( mpegts_info_t *info, mpegts_pat_section_info_t *pat_si, int32_t *ts_packet_length )
{
    mpegts_packet_header_t h;
    uint8_t section_header[TS_PID_PAT_SECTION_HEADER_SIZE];
    do
        if( mpegts_get_table_section_header( info, &h, TS_PID_PAT, section_header, TS_PID_PAT_SECTION_HEADER_SIZE, ts_packet_length ) )
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
    int32_t ts_packet_length;
    int64_t read_pos;
    /* search. */
    mpegts_pat_section_info_t pat_si;
    int section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_pat_packet( info, &pat_si, &ts_packet_length ) )
            return -1;
        /* get section length. */
        section_length = pat_si.section_length - 5;     /* 5: section_header[3]-[7] */
        if( (section_length - TS_PACKET_SECTION_CRC32_SIZE) % TS_PACKET_PAT_SECTION_DATA_SIZE )
            continue;
        /* check file position. */
        read_pos = ftello( info->input ) - (TS_PACKET_SIZE - ts_packet_length);
        /* buffering section data. */
        if( mpegts_get_table_section_data( info, TS_PID_PAT, section_buffer, section_length, ts_packet_length ) )
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
    info->sync_byte_position = ts_packet_length + info->packet_size - TS_PACKET_SIZE;
    info->read_position      = read_pos;
    return 0;
}

static int mpegts_search_pmt_packet( mpegts_info_t *info, mpegts_pmt_section_info_t *pmt_si, int32_t *ts_packet_length )
{
    /* search. */
    mpegts_packet_header_t h;
    uint8_t section_header[TS_PID_PMT_SECTION_HEADER_SIZE];
    int pid_list_index = -1;
    fpos_t start_fpos;
    fgetpos( info->input, &start_fpos );
    do
    {
        fsetpos( info->input, &start_fpos );
        ++pid_list_index;
        if( pid_list_index >= info->pid_list_num_in_pat )
            return -1;
        if( 0 > mpegts_get_table_section_header( info, &h, info->pid_list_in_pat[pid_list_index], section_header, TS_PID_PMT_SECTION_HEADER_SIZE, ts_packet_length ) )
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
    int32_t ts_packet_length;
    int64_t read_pos;
    /* search. */
    mpegts_pmt_section_info_t pmt_si;
    int section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_pmt_packet( info, &pmt_si, &ts_packet_length ) )
            return -1;
        /* get section length. */
        section_length = pmt_si.section_length - 9;             /* 9: section_header[3]-[11] */
        fseeko( info->input, pmt_si.program_info_length, SEEK_CUR );
        section_length -= pmt_si.program_info_length;
        ts_packet_length -= pmt_si.program_info_length;
        dprintf( LOG_LV4, "[check] section_length:%d\n", section_length );
        /* check file position. */
        read_pos = ftello( info->input ) - (TS_PACKET_SIZE - ts_packet_length);
        /* buffering section data. */
        if( mpegts_get_table_section_data( info, info->pmt_program_id, section_buffer, section_length, ts_packet_length ) )
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
        uint8_t *section_data = &(section_buffer[read_count]);
        uint8_t stream_type     =   section_data[0];
        /* reserved     3 bit   =  (section_data[1] & 0xE0) >> 5 */
        uint16_t elementary_PID = ((section_data[1] & 0x1F) << 8) | section_data[2];
        /* reserved     4 bit   =  (section_data[3] & 0xF0) >> 4 */
        uint16_t ES_info_length = ((section_data[3] & 0x0F) << 8) | section_data[4];
        dprintf( LOG_LV2, "[check] stream_type:0x%02X, elementary_PID:0x%04X, ES_info_length:%d\n"
                 , stream_type, elementary_PID, ES_info_length );
        /* setup stream type and PID. */
        info->pid_list_in_pmt[pid_list_num].stream_type = stream_type;
        info->pid_list_in_pmt[pid_list_num].program_id  = elementary_PID;
        /* seek next section. */
        read_count += TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length;
        ++pid_list_num;
    }
    uint8_t *crc_32 = &(section_buffer[read_count]);
    dprintf( LOG_LV2, "[check] CRC32:" );
    for( int i = 0; i < TS_PACKET_SECTION_CRC32_SIZE; ++i )
        dprintf( LOG_LV2, " %02X", crc_32[i] );
    dprintf( LOG_LV2, "\n" );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", read_pos );
    /* ready next. */
    info->sync_byte_position = ts_packet_length + info->packet_size - TS_PACKET_SIZE;
    info->read_position      = read_pos;
    return 0;
}

static int mpegts_get_pcr( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_pcr()\n" );
    info->pcr = -1;
    uint16_t program_id = info->pcr_program_id;
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    int32_t ts_packet_length;
    int64_t read_pos;
    /* search. */
    mpegts_packet_header_t h;
    do
    {
        if( mpegts_search_program_id_packet( info, &h, program_id ) )
            return -1;
        show_packet_header_info( &h );
        ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
        /* check adaptation field. */
        uint8_t adaptation_field_size = 0;
        if( h.adaptation_field_control > 1 )
        {
            fread( &adaptation_field_size, 1, 1, info->input );
            --ts_packet_length;
            dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
            if( adaptation_field_size < 7 )
                continue;
            /* check file position. */
            read_pos = ftello( info->input ) - (TS_PACKET_SIZE - ts_packet_length);
            /* check adaptation field data. */
            uint8_t adpf_data[adaptation_field_size];
            fread( adpf_data, 1, adaptation_field_size, info->input );
            ts_packet_length -= adaptation_field_size;
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
    }
    while( info->pcr < 0 );
    dprintf( LOG_LV2, "[check] PCR:%"PRId64" [%"PRId64"ms]\n", info->pcr, info->pcr / 90 );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", read_pos );
    /* ready next. */
    info->sync_byte_position = ts_packet_length + info->packet_size - TS_PACKET_SIZE;
    info->read_position      = read_pos;
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

static int mpegts_get_stream_timestamp( mpegts_info_t *info, uint16_t program_id, mpeg_pes_packet_start_code_type start_code, int64_t *pts_set_p, int64_t *dts_set_p )
{
    dprintf( LOG_LV2, "[check] mpegts_get_stream_timestamp()\n" );
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    int64_t read_pos = -1;
    /* search packet data. */
    int64_t pts = -1, dts = -1;
    do
    {
        /* seek payload data. */
        int ret = mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 1, 0 );
        if( ret < 0 )
            return -1;
        if( ret > 0 )
            continue;
        /* check file position. */
        read_pos = ftello( info->input ) - (TS_PACKET_SIZE - ts_packet_length);
        /* check PES Packet Start Code. */
        uint8_t pes_packet_head_data[PES_PACKET_START_CODE_SIZE];
        fread( pes_packet_head_data, 1, PES_PACKET_START_CODE_SIZE, info->input );
        ts_packet_length -= PES_PACKET_START_CODE_SIZE;
        if( mpeg_pes_check_start_code( pes_packet_head_data, start_code ) )
            continue;
        /* check PES packet length, flags. */
        int read_size = PES_PACKET_HEADER_CHECK_SIZE + PES_PACKET_PTS_DTS_DATA_SIZE;
        uint8_t pes_header_check_buffer[read_size];
        fread( pes_header_check_buffer, 1, read_size, info->input );
        ts_packet_length -= read_size;
        mpeg_pes_header_info_t pes_info;
        mpeg_pes_get_header_info( pes_header_check_buffer, &pes_info );
        dprintf( LOG_LV3, "[check] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                 , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        if( !pes_info.pts_flag )
            continue;
        /* get PTS and DTS value. */
        uint8_t *pes_packet_pts_dts_data = &(pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE]);
        pts = pes_info.pts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[0]) ) : -1;
        dts = pes_info.dts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[5]) ) : pts;
        dprintf( LOG_LV2, "[check] PTS:%"PRId64" DTS:%"PRId64"\n", pts, dts );
        /* setup. */
        *pts_set_p = pts;
        *dts_set_p = dts;
        /* ready next. */
        info->sync_byte_position = 0;
        info->read_position      = read_pos;
        /* reset position. */
        fseeko( info->input, read_pos, SEEK_SET );
    }
    while( pts < 0 );
    return 0;
}

#define GET_PES_PACKET_HEADER( input, len, pes )                                \
{                                                                               \
    uint8_t pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE];              \
    /* skip PES Packet Start Code. */                                           \
    fseeko( input, PES_PACKET_START_CODE_SIZE, SEEK_CUR );                      \
    len -= PES_PACKET_START_CODE_SIZE;                                          \
    /* get PES packet length, flags. */                                         \
    fread( pes_header_check_buffer, 1, PES_PACKET_HEADER_CHECK_SIZE, input );   \
    len -= PES_PACKET_HEADER_CHECK_SIZE;                                        \
    mpeg_pes_get_header_info( pes_header_check_buffer, &pes );                  \
}

#define BYTE_DATA_SHIFT( data, size )           \
{                                               \
    for( int i = 1; i < size; ++i )             \
        data[i - 1] = data[i];                  \
}

static int mpegts_get_mpeg_video_picture_info( mpegts_info_t *info, uint16_t program_id, mpeg_video_info_t *video_info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_mpeg_video_picture_info()\n" );
    int result = -1;
    /* parse payload data. */
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    int no_exist_start_indicator = 1;
    do
    {
        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            return -1;
        /* check start indicator. */
        if( no_exist_start_indicator && !h.payload_unit_start_indicator )
            continue;
        if( h.payload_unit_start_indicator )
        {
            /* check PES packet length, flags. */
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( info->input, ts_packet_length, pes_info )
            dprintf( LOG_LV3, "[debug] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                     , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
            fseeko( info->input, pes_info.header_length, SEEK_CUR );
            ts_packet_length -= pes_info.header_length;
        }
        /* check Stream Start Code. */
        if( no_exist_start_indicator )
        {
            fread( mpeg_video_head_data, 1, MPEG_VIDEO_START_CODE_SIZE - 1, info->input );
            ts_packet_length -= MPEG_VIDEO_START_CODE_SIZE - 1;
            no_exist_start_indicator = 0;
        }
        /* search. */
        while( ts_packet_length )
        {
            fread( &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1, 1, info->input );
            --ts_packet_length;
            /* check Start Code. */
            if( !mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
            {
                uint8_t identifier;
                fread( &identifier, 1, 1, info->input );
                fseeko( info->input, -1, SEEK_CUR );
                mpeg_video_start_code_info_t start_code_info;
                if( mpeg_video_judge_start_code( mpeg_video_head_data, identifier, &start_code_info ) )
                {
                    BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE )
                    continue;
                }
                uint32_t read_size = start_code_info.read_size;
                int64_t reset_position = ftello( info->input );
                int32_t rest_ts_packet_length = ts_packet_length;
                /* get header/extension information. */
                uint8_t buf[read_size];
                uint8_t *buf_p = buf;
                while( ts_packet_length < read_size )
                {
                    if( ts_packet_length )
                    {
                        fread( buf_p, 1, ts_packet_length, info->input );
                        read_size -= ts_packet_length;
                        buf_p += ts_packet_length;
                        ts_packet_length = 0;
                    }
                    if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
                        return -1;
                }
                fread( buf_p, 1, read_size, info->input );
                ts_packet_length -= read_size;
                int32_t check_size = mpeg_video_get_header_info( buf, start_code_info.start_code, video_info );
                if( check_size < start_code_info.read_size )
                {
                    /* reset position. */
                    fseeko( info->input, reset_position, SEEK_SET );
                    ts_packet_length = rest_ts_packet_length;
                    int64_t seek_size = check_size;
                    while( ts_packet_length < seek_size )
                    {
                        seek_size -= ts_packet_length;
                        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
                            return -1;
                    }
                    fseeko( info->input, seek_size, SEEK_CUR );
                    ts_packet_length -= seek_size;
                }
                /* debug. */
                mpeg_video_debug_header_info( video_info, start_code_info.searching_status );
                /* check the status detection. */
                if( start_code_info.searching_status == DETECT_GSC )
                    ++ info->gop_number;
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
        info->sync_byte_position = info->packet_size - TS_PACKET_SIZE;
    }
    while( 1 );
end_get_video_picture_info:
    return result;
}

static uint32_t mpegts_get_sample_packets_num( mpegts_info_t *info, uint16_t program_id, mpeg_stream_type stream_type )
{
    dprintf( LOG_LV3, "[debug] mpegts_get_sample_packets_num()\n" );
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    if( mpegts_search_program_id_packet( info, &h, program_id ) )
        return -1;
    fseeko( info->input, info->packet_size - TS_PACKET_HEADER_SIZE, SEEK_CUR );
    info->sync_byte_position = -1;
    uint32_t ts_packet_count = 0;
    int8_t old_continuity_counter = h.continuity_counter;
    do
    {
        ++ts_packet_count;
        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            break;
        if( h.continuity_counter != ((old_continuity_counter + 1) & 0x0F) )
            dprintf( LOG_LV3, "[debug] detect Drop!  ts_packet_count:%u  continuity_counter:%u --> %u\n", ts_packet_count, old_continuity_counter, h.continuity_counter );
        old_continuity_counter = h.continuity_counter;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( info->input, ts_packet_length, pes_info )
            /* check PES packet header. */
            if( pes_info.pts_flag )
            {
                /* reset next start position. */
                fseeko( info->input, ts_packet_length - TS_PACKET_SIZE, SEEK_CUR );
                break;
            }
            dprintf( LOG_LV3, "[debug] Detect PES packet doesn't have PTS.\n"
                              "        PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                            , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        }
        /* ready next. */
        fseeko( info->input, ts_packet_length + info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
        info->sync_byte_position = -1;
    }
    while( 1 );
    dprintf( LOG_LV3, "[debug] ts_packet_count:%u\n", ts_packet_count );
    return ts_packet_count;
}

typedef struct {
    uint32_t    data_size;
    int32_t     start_point;
} sample_raw_data_info_t;

static int mpegts_get_sample_raw_data_info( mpegts_info_t *info, uint16_t program_id, mpeg_stream_type stream_type, sample_raw_data_info_t *raw_data_info )
{
    dprintf( LOG_LV3, "[debug] mpegts_get_sample_raw_data_info()\n" );
    uint32_t raw_data_size = 0;
    int32_t start_point = -1;
    /* ready. */
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    /* search. */
    int check_start_point = 0;
    uint8_t check_buffer[TS_PACKET_SIZE + STREAM_HEADER_CHECK_MAX_SIZE];
    int32_t buffer_read_offset = 0;
    int32_t buffer_read_size = 0;
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
        {
            if( start_point >= 0 )
                /* This sample is the data at the end was shaved. */
                raw_data_size += buffer_read_size + buffer_read_offset;
            dprintf( LOG_LV3, "[debug] read file end.\n" );
            goto end_get_info;
        }
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( info->input, ts_packet_length, pes_info )
            /* check PES packet header. */
            if( pes_info.pts_flag )
                check_start_point = 1;
            /* skip PES packet header. */
            fseeko( info->input, pes_info.header_length, SEEK_CUR );
            ts_packet_length -= pes_info.header_length;
        }
        if( check_start_point )
        {
            int32_t buffer_size = ts_packet_length + buffer_read_offset;
            /* buffering check data . */
            fread( check_buffer + buffer_read_offset, 1, ts_packet_length, info->input );
            ts_packet_length = 0;
            int32_t search_start_point = mpeg_stream_check_start_point( stream_type, check_buffer, buffer_size );
            if( search_start_point < 0 )
            {
                /* continue buffering check. */
                buffer_read_size += buffer_size;
                for( int j = 0; j < STREAM_HEADER_CHECK_MAX_SIZE; ++j )
                    check_buffer[j] = check_buffer[buffer_size - STREAM_HEADER_CHECK_MAX_SIZE + j];
                buffer_read_size -= STREAM_HEADER_CHECK_MAX_SIZE;
                buffer_read_offset = STREAM_HEADER_CHECK_MAX_SIZE;
            }
            else
            {
                if( start_point < 0 )
                {
                    /* detect start point. */
                    start_point = buffer_read_size + search_start_point;
                    raw_data_size = buffer_size - search_start_point;
                }
                else
                {
                    /* detect end point(=next start point). */
                    raw_data_size += buffer_read_size + search_start_point;
                    fseeko( info->input, info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
                    break;
                }
                buffer_read_offset = 0;
                buffer_read_size = 0;
                check_start_point = 0;
            }
        }
        else
            raw_data_size += ts_packet_length;
        /* ready next. */
        fseeko( info->input, ts_packet_length + info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
    }
end_get_info:
    fsetpos( info->input, &start_position );
    /* setup. */
    raw_data_info->data_size   = raw_data_size;
    raw_data_info->start_point = start_point;
    return 0;
}

static void mpegts_get_sample_raw_data( mpegts_info_t *info, uint16_t program_id, mpeg_stream_type stream_type, uint8_t **buffer, uint32_t *read_size )
{
    dprintf( LOG_LV2, "[check] mpegts_get_sample_raw_data()\n" );
    /* check raw data. */
    sample_raw_data_info_t raw_data_info;
    if( mpegts_get_sample_raw_data_info( info, program_id, stream_type, &raw_data_info ) )
        return;
    dprintf( LOG_LV4, "[debug] raw_data  size:%u  start_point:%d\n", raw_data_info.data_size, raw_data_info.start_point );
    uint32_t raw_data_size = raw_data_info.data_size;
    int32_t start_point    = raw_data_info.start_point;
    /* allocate buffer. */
    *buffer = malloc( raw_data_size );
    if( !(*buffer) )
        return;
    dprintf( LOG_LV3, "[debug] buffer_size:%u\n", raw_data_size );
    /* read. */
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    *read_size = 0;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            break;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( info->input, ts_packet_length, pes_info )
            /* skip PES packet header. */
            fseeko( info->input, pes_info.header_length, SEEK_CUR );
            ts_packet_length -= pes_info.header_length;
        }
        /* check read start point. */
        if( start_point )
        {
            if( start_point > ts_packet_length )
            {
                fseeko( info->input, ts_packet_length, SEEK_CUR );
                start_point -= ts_packet_length;
                ts_packet_length = 0;
            }
            else
            {
                fseeko( info->input, start_point, SEEK_CUR );
                ts_packet_length -= start_point;
                start_point = 0;
            }
        }
        /* read raw data. */
        if( ts_packet_length > 0 )
        {
            if( raw_data_size > *read_size + ts_packet_length )
            {
                fread( *buffer + *read_size, 1, ts_packet_length, info->input );
                *read_size += ts_packet_length;
            }
            else
            {
                fread( *buffer + *read_size, 1, raw_data_size - *read_size, info->input );
                ts_packet_length -= raw_data_size - *read_size;
                *read_size = raw_data_size;
                fseeko( info->input, ts_packet_length + info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
                break;
            }
        }
        /* ready next. */
        fseeko( info->input, info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
    }
    return;
}

static void mpegts_get_sample_pes_packet_data( mpegts_info_t *info, uint16_t program_id, uint32_t ts_packet_count, uint8_t **buffer, uint32_t *read_size )
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
    int32_t ts_packet_length;
    *read_size = 0;
    for( uint32_t i = 0; i < ts_packet_count; ++i )
    {
        if( mpegts_seek_packet_payload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            return;
        /* read packet data. */
        if( ts_packet_length > 0 )
        {
            fread( *buffer + *read_size, 1, ts_packet_length, info->input );
            *read_size += ts_packet_length;
        }
        /* ready next. */
        fseeko( info->input, info->packet_size - TS_PACKET_SIZE, SEEK_CUR );
    }
}

static void mpegts_get_sample_ts_packet_data( mpegts_info_t *info, uint16_t program_id, uint32_t ts_packet_count, uint8_t **buffer, uint32_t *read_size )
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
        fread( *buffer + *read_size, 1, TS_PACKET_SIZE, info->input );
        *read_size += TS_PACKET_SIZE;
        /* seek next packet. */
        if( mpegts_search_program_id_packet( info, &h, program_id ) )
            break;
        fseeko( info->input, -(TS_PACKET_HEADER_SIZE), SEEK_CUR );
    }
}

static mpeg_stream_type get_sample_stream_type( void *ih, mpeg_sample_type sample_type )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return STREAM_INVAILED;
    /* check stream type. */
    mpeg_stream_type stream_type = STREAM_INVAILED;
    if( sample_type == SAMPLE_TYPE_VIDEO )
        stream_type = info->video_stream_type;
    else if( sample_type == SAMPLE_TYPE_AUDIO )
        stream_type = info->audio_stream_type;
    return stream_type;
}

static int get_sample_data( void *ih, mpeg_sample_type sample_type, int64_t position, uint32_t sample_size, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    /* check program id. */
    uint16_t program_id = TS_PID_ERR;
    mpeg_stream_type stream_type = STREAM_INVAILED;
    if( sample_type == SAMPLE_TYPE_VIDEO )
    {
        program_id  = info->video_program_id;
        stream_type = info->video_stream_type;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO )
    {
        program_id  = info->audio_program_id;
        stream_type = info->audio_stream_type;
    }
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    /* seek reading start position. */
    fseeko( info->input, position, SEEK_SET );
    info->sync_byte_position = 0;
    /* get data. */
    uint32_t ts_packet_count = sample_size / TS_PACKET_SIZE;
    uint8_t *buffer = NULL;
    uint32_t read_size;
    switch( get_mode )
    {
        case GET_SAMPLE_DATA_CONTAINER :
            mpegts_get_sample_ts_packet_data( info, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_PES_PACKET :
            mpegts_get_sample_pes_packet_data( info, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_RAW :
            mpegts_get_sample_raw_data( info, program_id, stream_type, &buffer, &read_size );
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
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    return ftello( info->input );
}

static int set_sample_position( void *ih, int64_t position )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    fseeko( info->input, position, SEEK_SET );
    return 0;
}

static int seek_next_sample_position( void *ih, mpeg_sample_type sample_type )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    int64_t seek_position = -1;
    if( sample_type == SAMPLE_TYPE_VIDEO )
        seek_position = info->video_position;
    else if( sample_type == SAMPLE_TYPE_AUDIO )
        seek_position = info->audio_position;
    if( seek_position < 0 )
        return -1;
    fseeko( info->input, seek_position, SEEK_SET );
    return 0;
}

static int64_t get_pcr( void *ih )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    if( info->pcr < 0 )
    {
        fpos_t start_position;
        fgetpos( info->input, &start_position );
        int result = mpegts_get_pcr( info );
        fsetpos( info->input, &start_position );
        if( result )
            return -1;
    }
    return info->pcr;
}

static int get_video_info( void *ih, video_sample_info_t *video_sample_info )
{
    dprintf( LOG_LV2, "[mpegts_parser] get_video_info()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    uint16_t program_id = info->video_program_id;
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    /* get timestamp. */
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, program_id, PES_PACKET_START_CODE_VIDEO_STREAM, &pts, &dts ) )
        return -1;
    /* parse payload data. */
    int64_t gop_number = 0;
    uint8_t progressive_sequence = 0;
    uint8_t closed_gop = 0;
    uint8_t picture_coding_type = MPEG_VIDEO_UNKNOWN_FRAME;
    int16_t temporal_reference = -1;
    uint8_t picture_structure = 0;
    uint8_t progressive_frame = 0;
    uint8_t repeat_first_field = 0;
    uint8_t top_field_first = 0;
    int stream_judge = mpeg_stream_judge_type( info->video_stream_type );
    if( (stream_judge & STREAM_IS_MPEG_VIDEO) == STREAM_IS_MPEG_VIDEO )
    {
        gop_number = -1;
        mpeg_video_info_t video_info;
        //memset( &video_info, 0, sizeof(mpeg_video_info_t) );
        if( !mpegts_get_mpeg_video_picture_info( info, program_id, &video_info ) )
        {
            gop_number           = info->gop_number;
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
    /* check packets num. */
    fseeko( info->input, info->read_position, SEEK_SET );
    info->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( info, program_id, info->video_stream_type );
    if( !ts_packet_count )
        return -1;
    int64_t read_last_position = ftello( info->input );
    fseeko( info->input, read_last_position, SEEK_SET );
    //info->sync_byte_position = 0;
    /* setup. */
    video_sample_info->file_position        = info->read_position;
    video_sample_info->sample_size          = TS_PACKET_SIZE * ts_packet_count;
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
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", info->read_position );
    /* ready next. */
    info->sync_byte_position = -1;
    info->video_position     = read_last_position;
    return 0;
}

static int get_audio_info( void *ih, audio_sample_info_t *audio_sample_info )
{
    dprintf( LOG_LV2, "[mpegts_parser] get_audio_info()\n" );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    uint16_t program_id = info->audio_program_id;
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    /* check stream type. */
    mpeg_pes_packet_start_code_type start_code = PES_PACKET_START_CODE_AUDIO_STREAM;
    int stream_judge = mpeg_stream_judge_type( info->audio_stream_type );
    if( stream_judge == STREAM_IS_DOLBY_AUDIO )
        start_code = PES_PACKET_START_CODE_AC3_DTS_AUDIO_STREAM;
    /* get timestamp. */
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, program_id, start_code, &pts, &dts ) )
        return -1;
    /* check packets num. */
    //fseeko( info->input, info->read_position, SEEK_SET );
    //info->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( info, program_id, info->audio_stream_type );
    if( !ts_packet_count )
        return -1;
    int64_t read_last_position = ftello( info->input );
    fseeko( info->input, read_last_position, SEEK_SET );
    //info->sync_byte_position = 0;
    /* setup. */
    audio_sample_info->file_position = info->read_position;
    audio_sample_info->sample_size   = TS_PACKET_SIZE * ts_packet_count;
    audio_sample_info->pts           = pts;
    audio_sample_info->dts           = dts;
    dprintf( LOG_LV2, "[check] Audio PTS:%"PRId64" [%"PRId64"ms]\n", pts, pts / 90 );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", info->read_position );
    /* ready next. */
    info->sync_byte_position = -1;
    info->audio_position     = read_last_position;
    return 0;
}

static void set_pmt_first_info( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] [sub] set_pmt_first_info()\n" );
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    /* search program id and stream type�D*/
    uint8_t va_exist = STREAM_IS_UNKNOWN;
    int pid_list_index = 0;
    while( pid_list_index < info->pid_list_num_in_pmt )
    {
        int stream_judge = mpeg_stream_judge_type( info->pid_list_in_pmt[pid_list_index].stream_type );
        if( stream_judge & STREAM_IS_VIDEO )
        {
            uint16_t program_id = info->pid_list_in_pmt[pid_list_index].program_id;
            mpegts_packet_header_t h;
            if( !mpegts_search_program_id_packet( info, &h, program_id ) )
            {
                info->video_program_id  = info->pid_list_in_pmt[pid_list_index].program_id;
                info->video_stream_type = info->pid_list_in_pmt[pid_list_index].stream_type;
                va_exist |= stream_judge;
                dprintf( LOG_LV2, "[check] video PID:0x%04X  stream_type:0x%02X\n", info->video_program_id, info->video_stream_type );
            }
            fsetpos( info->input, &start_position );
        }
        else if( stream_judge & STREAM_IS_AUDIO )
        {
            uint16_t program_id = info->pid_list_in_pmt[pid_list_index].program_id;
            mpegts_packet_header_t h;
            program_id = info->pid_list_in_pmt[pid_list_index].program_id;
            if( !mpegts_search_program_id_packet( info, &h, program_id ) )
            {
                info->audio_program_id  = info->pid_list_in_pmt[pid_list_index].program_id;
                info->audio_stream_type = info->pid_list_in_pmt[pid_list_index].stream_type;
                va_exist |= stream_judge;
                dprintf( LOG_LV2, "[check] audio PID:0x%04X  stream_type:0x%02X\n", info->audio_program_id, info->audio_stream_type );
            }
            fsetpos( info->input, &start_position );
        }
        if( (va_exist & STREAM_IS_VIDEO) && (va_exist & STREAM_IS_AUDIO) )
            break;
        ++pid_list_index;
    }
}

static void release_pid_list( mpegts_info_t *info )
{
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
    set_pmt_first_info( info );
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
    info->sync_byte_position = -1;
    info->read_position      = -1;
    info->pcr_program_id     = TS_PID_ERR;
    info->video_program_id   = TS_PID_ERR;
    info->audio_program_id   = TS_PID_ERR;
    info->video_stream_type  = STREAM_INVAILED;
    info->audio_stream_type  = STREAM_INVAILED;
    info->pcr                = -1;
    info->gop_number         = -1;
    if( info->pid_list_in_pmt )
    {
        free( info->pid_list_in_pmt );
        info->pid_list_in_pmt = NULL;
    }
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    int result = parse_pmt_info( info );
    fsetpos( info->input, &start_position );
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
    int result = 0;
    switch( pid_type )
    {
        case PID_TYPE_PAT :
            result = -1;
            break;
        case PID_TYPE_PMT :
            result = set_pmt_program_id( info, program_id );
            break;
        case PID_TYPE_VIDEO :
            info->video_program_id  = program_id;
            //info->video_stream_type = 0;        // FIXME
            break;
        case PID_TYPE_AUDIO :
            info->audio_program_id  = program_id;
            //info->audio_stream_type = 0;        // FIXME
            break;
        default :
            result = -1;
            break;
    }
    return result;
}

static uint16_t get_program_id( void *ih, uint8_t stream_type )
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
    if( !info || !info->input )
        return -1;
    if( info->status == PARSER_STATUS_PARSED )
        release_pid_list( info );
    fpos_t start_position;
    fgetpos( info->input, &start_position );
    if( !info->pid_list_in_pat && mpegts_parse_pat( info ) )
        goto fail_parse;
    int result = parse_pmt_info( info );
    fsetpos( info->input, &start_position );
    if( !result )
        info->status = PARSER_STATUS_PARSED;
    return result;
fail_parse:
    fsetpos( info->input, &start_position );
    return -1;
}

static void *initialize( const char *mpegts )
{
    dprintf( LOG_LV2, "[mpegts_parser] initialize()\n" );
    mpegts_info_t *info = calloc( sizeof(mpegts_info_t), 1 );
    if( !info )
        return NULL;
    FILE *input = fopen( mpegts, "rb" );
    if( !input )
        goto fail_initialize;
    /* initialize. */
    info->input                   = input;
    info->packet_size             = TS_PACKET_SIZE;
    info->sync_byte_position      = -1;
    info->read_position           = -1;
    info->packet_check_count_num  = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
    info->packet_check_retry_num  = TS_PACKET_SEARCH_RETRY_COUNT_NUM;
    info->pcr_program_id          = TS_PID_ERR;
    info->video_program_id        = TS_PID_ERR;
    info->audio_program_id        = TS_PID_ERR;
    info->video_stream_type       = STREAM_INVAILED;
    info->audio_stream_type       = STREAM_INVAILED;
    info->pcr                     = -1;
    info->gop_number              = -1;
    /* first check. */
    if( mpegts_first_check( info ) )
        goto fail_initialize;
    return info;
fail_initialize:
    dprintf( LOG_LV2, "[mpegts_parser] failed initialize.\n" );
    if( input )
        fclose( input );
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
    if( info->input )
        fclose( info->input );
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
    get_sample_position,
    set_sample_position,
    seek_next_sample_position,
    get_sample_data,
    get_sample_stream_type
};
