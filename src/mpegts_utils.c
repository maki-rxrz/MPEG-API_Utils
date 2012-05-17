/*****************************************************************************
 * mpegts_utils.c
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
#include "mpegts_def.h"
#include "mpegts_utils.h"

#define SYNC_BYTE                           '\x47'

#define TS_PACKET_SIZE                      (188)
#define TTS_PACKET_SIZE                     (192)
#define FEC_TS_PACKET_SIZE                  (204)

#define TS_PACKET_TYPE_NUM                  (3)
#define TS_PACKET_FIRST_CHECK_COUNT_NUM     (4)
#define TS_PACKET_SEARCH_CHECK_COUNT_NUM    (1000)
#define TS_PACKET_SEARCH_RETRY_COUNT_NUM    (5)

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
    h->sync_byte                     =   ts_header[0];
    h->transport_error_indicator     =  (ts_header[1] & 0x80) >> 7;
    h->payload_unit_start_indicator  =  (ts_header[1] & 0x40) >> 6;
    h->transport_priority            =  (ts_header[1] & 0x20) >> 5;
    h->program_id                    = ((ts_header[1] & 0x1F) << 8) | ts_header[2];
    h->transport_scrambling_control  =  (ts_header[3] & 0xC0) >> 6;
    h->adaptation_field_control      =  (ts_header[3] & 0x30) >> 4;
    h->continuity_counter            =  (ts_header[3] & 0x0F);
    /* ready next. */
    info->sync_byte_position = -1;
    //info->read_position      = ftello( info->input ) - TS_PACKET_HEADER_SIZE;
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
    }
    while( h->program_id != search_program_id );
    return 0;
}

#define SKIP_ADAPTATION_FIELD( i, h, l, s, n )      \
{                                                   \
    if( (h).adaptation_field_control > 1 )          \
    {                                               \
        fread( &s, 1, 1, i->input );                \
        fseeko( i->input, s, SEEK_CUR );            \
        (l) -= 1 + s;                               \
    }                                               \
    else if( n )                                    \
    {                                               \
        fseeko( i->input, n, SEEK_CUR );            \
        (l) -= n;                                   \
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
    SKIP_ADAPTATION_FIELD( info, *h, *ts_packet_length, adaptation_field_size, 1 )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    /* read section header. */
    fread( section_header, 1, section_header_length, info->input );
    *ts_packet_length -= section_header_length;
    return 0;
}

static int mpegts_seek_packet_playload_data( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id, int32_t *ts_packet_length, int indicator_check, int indicator_status )
{
    dprintf( LOG_LV4, "[check] mpegts_seek_packet_playload_data()\n" );
    if( mpegts_search_program_id_packet( info, h, search_program_id ) )
        return -1;
    show_packet_header_info( h );
    /* check start indicator. */
    if( indicator_check && (indicator_status == h->payload_unit_start_indicator) )
        return 1;
    *ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( info, *h, *ts_packet_length, adaptation_field_size, 0 )
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    return 0;
}

static int mpegts_get_table_section_data( mpegts_info_t *info, uint16_t search_program_id, uint8_t *section_buffer, uint16_t section_length, int32_t *ts_packet_length )
{
    mpegts_packet_header_t h;
    fpos_t start_pos;
    fgetpos( info->input, &start_pos );
    /* buffering payload data. */
    int read_count = 0;
    int need_ts_packet_payload_data = 0;
    while( read_count < section_length )
    {
        if( need_ts_packet_payload_data )
            /* seek next packet payload data. */
            if( mpegts_seek_packet_playload_data( info, &h, search_program_id, ts_packet_length, 1, 1 ) )
                return -1;
        need_ts_packet_payload_data = 1;
        int read_size = (section_length - read_count > *ts_packet_length) ? *ts_packet_length : section_length - read_count;
        fread( &(section_buffer[read_count]), 1, read_size, info->input );
        *ts_packet_length -= read_size;
        read_count += read_size;
        dprintf( LOG_LV4, "[check] section data read:%d, packet:%d\n", read_size, *ts_packet_length );
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
    pat_si->table_id                 =   section_header[0];
    pat_si->section_syntax_indicator =  (section_header[1] & 0x80) >> 7;
    // '0'              1 bit        =  (section_header[1] & 0x40) >> 6;
    // reserved '11'    2 bit        =  (section_header[1] & 0x30) >> 4;
    pat_si->section_length           = ((section_header[1] & 0x0F) << 8) | section_header[2];
    pat_si->transport_stream_id      =  (section_header[3] << 8) | section_header[4];
    // reserved '11'    2bit         =  (section_header[5] & 0xC0) >> 6;
    pat_si->version_number           =  (section_header[5] & 0x3E) >> 1;
    pat_si->current_next_indicator   =  (section_header[5] & 0x01);
    pat_si->section_number           =  (section_header[6] & 0xC0) >> 6;
    pat_si->last_section_number      =  (section_header[7] & 0xC0) >> 6;
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
        if( mpegts_get_table_section_data( info, TS_PID_PAT, section_buffer, section_length, &ts_packet_length ) )
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
        // '111'        3 bit   =  (section_data[2] & 0xC0) >> 4;
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
    info->sync_byte_position = -1;
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
    // '0'              1 bit        =  (section_header[1] & 0x40) >> 6;
    // reserved '11'    2 bit        =  (section_header[1] & 0x30) >> 4;
    pmt_si->section_length           = ((section_header[1] & 0x0F) << 8) | section_header[2];
    pmt_si->program_number           =  (section_header[3] << 8) | section_header[4];
    // reserved '11'    2bit         =  (section_header[5] & 0xC0) >> 6;
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
        if( mpegts_get_table_section_data( info, info->pmt_program_id, section_buffer, section_length, &ts_packet_length ) )
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
    info->sync_byte_position = -1;
    info->read_position      = read_pos;
    return 0;
}

static int mpegts_get_pcr( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_pcr()\n" );
    info->pcr = -1;
    int32_t ts_packet_length;
    int64_t read_pos;
    /* search. */
    mpegts_packet_header_t h;
    do
    {
        if( mpegts_search_program_id_packet( info, &h, info->pcr_program_id ) )
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
    info->sync_byte_position = -1;
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

static int mpegts_get_stream_timestamp( mpegts_info_t *info, uint16_t program_id, mpeg_pes_packet_star_code_type start_code, int64_t *pts_set_p, int64_t *dts_set_p )
{
    dprintf( LOG_LV2, "[check] mpegts_get_stream_timestamp()\n" );
    mpegts_packet_header_t h;
    int32_t ts_packet_length;
    int64_t read_pos = -1;
    /* search packet data. */
    uint8_t pes_packet_head_data[PES_PACKET_STATRT_CODE_SIZE];
    int no_exist_start_indicator = 1;
    int64_t pts = -1, dts = -1;
    do
    {
        /* seek playload data. */
        int ret = mpegts_seek_packet_playload_data( info, &h, program_id, &ts_packet_length, no_exist_start_indicator, 0 );
        if( ret < 0 )
            return -1;
        if( ret > 0 )
            continue;
        /* check file position. */
        read_pos = ftello( info->input ) - (TS_PACKET_SIZE - ts_packet_length);
        /* check PES Packet Start Code. */
        if( no_exist_start_indicator )
        {
            fread( pes_packet_head_data, 1, PES_PACKET_STATRT_CODE_SIZE - 1, info->input );
            no_exist_start_indicator = 0;
            ts_packet_length -= PES_PACKET_STATRT_CODE_SIZE - 1;
        }
        int non_exist_pes_start = 1;
        while( ts_packet_length )
        {
            fread( &(pes_packet_head_data[PES_PACKET_STATRT_CODE_SIZE - 1]), 1, 1, info->input );
            --ts_packet_length;
            if( !mpeg_pes_check_start_code( pes_packet_head_data, start_code ) )
            {
                non_exist_pes_start = 0;
                break;
            }
            for( int i = 1; i < PES_PACKET_STATRT_CODE_SIZE; ++i )
                pes_packet_head_data[i - 1] = pes_packet_head_data[i];
        }
        if( non_exist_pes_start )
            continue;
        /* check PES packet length, flags. */
        int read_size = PES_PACKET_HEADER_CHECK_SIZE + PES_PACKET_PTS_DTS_DATA_SIZE;
        int read_count = 0;
        uint8_t pes_timestamp_buffer[read_size];
        if( ts_packet_length < read_size )
        {
            fread( pes_timestamp_buffer, 1, ts_packet_length, info->input );
            read_count = ts_packet_length;
            /* seek next payload data. */
            int ret = mpegts_seek_packet_playload_data( info, &h, program_id, &ts_packet_length, 1, 1 );
            if( ret < 0 )
                return -1;
            if( ret > 0 )
            {
                no_exist_start_indicator = 1;
                continue;
            }
        }
        fread( &(pes_timestamp_buffer[read_count]), 1, read_size - read_count, info->input );
        ts_packet_length -= read_size - read_count;
        mpeg_pes_header_info_t pes_info;
        mpeg_pes_get_header_info( pes_timestamp_buffer, &pes_info );
        dprintf( LOG_LV3, "[check] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                 , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        if( !pes_info.pts_flag )
        {
            no_exist_start_indicator = 1;
            continue;
        }
        /* get PTS and DTS value. */
        uint8_t *pes_packet_pts_dts_data = &(pes_timestamp_buffer[PES_PACKET_HEADER_CHECK_SIZE]);
        pts = pes_info.pts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[0]) ) : -1;
        dts = pes_info.dts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[5]) ) : -1;
        dprintf( LOG_LV2, "[check] PTS:%"PRId64" DTS:%"PRId64"\n", pts, dts );
        /* setup. */
        *pts_set_p = pts;
        *dts_set_p = dts;
        /* reset position. */
        fseeko( info->input, read_pos, SEEK_SET );
        /* ready next. */
        info->sync_byte_position = 0;
        info->read_position      = read_pos;
    }
    while( pts < 0 );
    return 0;
}

typedef enum {
    NON_DETECT = -1,
    DETECT_SHC,
    DETECT_SESC,
    DETECT_SDE,
    DETECT_SSE,
    DETECT_ESC,
    DETECT_UDSC,
    DETECT_SEC,
    DETECT_GSC,
    DETECT_PSC,
    DETECT_PCESC,
    DETECT_QME,
    DETECT_PDE,
    DETECT_PTSE,
    DETECT_PSSE,
    DETECT_CPRE,
    DETECT_SSC
} start_code_searching_status;

static void show_video_info( mpeg_video_info_t *video_info, start_code_searching_status searching_status )
{
    /* debug. */
    switch( searching_status )
    {
        case DETECT_SHC :
            dprintf( LOG_LV2,
                    "[check] detect Sequence Start Code.\n"
                    "        frame_size:%dx%d\n"
                    "        aspect_ratio_code:%d\n"
                    "        frame_rate_code:%d\n"
                    "        bit_rate:%d\n"
                    "        vbv_buffer_size:%d\n"
                    "        constrained_parameters_flag:%d\n"
                    "        load_intra_quantiser_matrix:%d\n"
                    "        load_non_intra_quantiser_matrix:%d\n"
                    , video_info->sequence.horizontal_size
                    , video_info->sequence.vertical_size
                    , video_info->sequence.aspect_ratio_information
                    , video_info->sequence.frame_rate_code
                    , video_info->sequence.bit_rate
                    , video_info->sequence.vbv_buffer_size
                    , video_info->sequence.constrained_parameters_flag
                    , video_info->sequence.load_intra_quantiser_matrix
                    , video_info->sequence.load_non_intra_quantiser_matrix );
            break;
        case DETECT_SESC :
            dprintf( LOG_LV2,
                    "        profile_and_level_indication:%d\n"
                    "        progressive_sequence:%d\n"
                    "        chroma_format:%d\n"
                    "        horizontal_size_extension:%d\n"
                    "        vertical_size_extension:%d\n"
                    "        bit_rate_extension:%d\n"
                    "        vbv_buffer_size_extension:%d\n"
                    "        low_delay:%d\n"
                    "        frame_rate_extension_n:%d\n"
                    "        frame_rate_extension_d:%d\n"
                    , video_info->sequence_ext.profile_and_level_indication
                    , video_info->sequence_ext.progressive_sequence
                    , video_info->sequence_ext.chroma_format
                    , video_info->sequence_ext.horizontal_size_extension
                    , video_info->sequence_ext.vertical_size_extension
                    , video_info->sequence_ext.bit_rate_extension
                    , video_info->sequence_ext.vbv_buffer_size_extension
                    , video_info->sequence_ext.low_delay
                    , video_info->sequence_ext.frame_rate_extension_n
                    , video_info->sequence_ext.frame_rate_extension_d );
            break;
        case DETECT_SDE :
            dprintf( LOG_LV2,
                    "        video_format:%d\n"
                    "        colour_description:%d\n"
                    "        colour_primaries:%d\n"
                    "        transfer_characteristics:%d\n"
                    "        matrix_coefficients:%d\n"
                    "        display_horizontal_size:%d\n"
                    "        display_vertical_size:%d\n"
                    , video_info->sequence_display_ext.video_format
                    , video_info->sequence_display_ext.colour_description
                    , video_info->sequence_display_ext.colour_primaries
                    , video_info->sequence_display_ext.transfer_characteristics
                    , video_info->sequence_display_ext.matrix_coefficients
                    , video_info->sequence_display_ext.display_horizontal_size
                    , video_info->sequence_display_ext.display_vertical_size );
            break;
        case DETECT_SSE :
            dprintf( LOG_LV2,
                    "        scalable_mode:%d\n"
                    "        layer_id:%d\n"
                    "        lower_layer_prediction_horizontal_size:%d\n"
                    "        lower_layer_prediction_vertical_size:%d\n"
                    "        horizontal_subsampling_factor_m:%d\n"
                    "        horizontal_subsampling_factor_n:%d\n"
                    "        vertical_subsampling_factor_m:%d\n"
                    "        vertical_subsampling_factor_n:%d\n"
                    "        picture_mux_enable:%d\n"
                    "        mux_to_progressive_sequence:%d\n"
                    "        picture_mux_order:%d\n"
                    "        picture_mux_factor:%d\n"
                    , video_info->sequence_scalable_ext.scalable_mode
                    , video_info->sequence_scalable_ext.layer_id
                    , video_info->sequence_scalable_ext.lower_layer_prediction_horizontal_size
                    , video_info->sequence_scalable_ext.lower_layer_prediction_vertical_size
                    , video_info->sequence_scalable_ext.horizontal_subsampling_factor_m
                    , video_info->sequence_scalable_ext.horizontal_subsampling_factor_n
                    , video_info->sequence_scalable_ext.vertical_subsampling_factor_m
                    , video_info->sequence_scalable_ext.vertical_subsampling_factor_n
                    , video_info->sequence_scalable_ext.picture_mux_enable
                    , video_info->sequence_scalable_ext.mux_to_progressive_sequence
                    , video_info->sequence_scalable_ext.picture_mux_order
                    , video_info->sequence_scalable_ext.picture_mux_factor );
            break;
        case DETECT_ESC :
        case DETECT_UDSC :
        case DETECT_SEC :
            break;
        case DETECT_GSC :
            dprintf( LOG_LV2,
                    "[check] detect GOP Start Code.\n"
                    "        time_code:%d\n"
                    "        closed_gop:%d\n"
                    "        broken_link:%d\n"
                    , video_info->gop.time_code
                    , video_info->gop.closed_gop
                    , video_info->gop.broken_link );
            break;
        case DETECT_PSC :
            dprintf( LOG_LV2,
                    "[check] detect Picture Start Code.\n"
                    "        temporal_reference:%d\n"
                    "        picture_coding_type:%d\n"
                    "        vbv_delay:%d\n"
                    "        full_pel_forward_vector:%d\n"
                    "        forward_f_code:%d\n"
                    "        full_pel_backword_vector:%d\n"
                    "        backward_f_code:%d\n"
                    , video_info->picture.temporal_reference
                    , video_info->picture.picture_coding_type
                    , video_info->picture.vbv_delay
                    , video_info->picture.full_pel_forward_vector
                    , video_info->picture.forward_f_code
                    , video_info->picture.full_pel_backword_vector
                    , video_info->picture.backward_f_code );
            break;
        case DETECT_PCESC :
            dprintf( LOG_LV2,
                    "        forward_horizontal:%d\n"
                    "        forward_vertical:%d\n"
                    "        backward_horizontal:%d\n"
                    "        backward_vertical:%d\n"
                    "        intra_dc_precision:%d\n"
                    "        picture_structure:%d\n"
                    "        top_field_first:%d\n"
                    "        frame_predictive_frame_dct:%d\n"
                    "        concealment_motion_vectors:%d\n"
                    "        q_scale_type:%d\n"
                    "        intra_vlc_format:%d\n"
                    "        alternate_scan:%d\n"
                    "        repeat_first_field:%d\n"
                    "        chroma_420_type:%d\n"
                    "        progressive_frame:%d\n"
                    "        composite_display_flag:%d\n"
                    "        v_axis:%d\n"
                    "        field_sequence:%d\n"
                    "        sub_carrier:%d\n"
                    "        burst_amplitude:%d\n"
                    "        sub_carrier_phase:%d\n"
                    , video_info->picture_coding_ext.f_code[0].horizontal
                    , video_info->picture_coding_ext.f_code[0].vertical
                    , video_info->picture_coding_ext.f_code[1].horizontal
                    , video_info->picture_coding_ext.f_code[1].vertical
                    , video_info->picture_coding_ext.intra_dc_precision
                    , video_info->picture_coding_ext.picture_structure
                    , video_info->picture_coding_ext.top_field_first
                    , video_info->picture_coding_ext.frame_predictive_frame_dct
                    , video_info->picture_coding_ext.concealment_motion_vectors
                    , video_info->picture_coding_ext.q_scale_type
                    , video_info->picture_coding_ext.intra_vlc_format
                    , video_info->picture_coding_ext.alternate_scan
                    , video_info->picture_coding_ext.repeat_first_field
                    , video_info->picture_coding_ext.chroma_420_type
                    , video_info->picture_coding_ext.progressive_frame
                    , video_info->picture_coding_ext.composite_display_flag
                    , video_info->picture_coding_ext.v_axis
                    , video_info->picture_coding_ext.field_sequence
                    , video_info->picture_coding_ext.sub_carrier
                    , video_info->picture_coding_ext.burst_amplitude
                    , video_info->picture_coding_ext.sub_carrier_phase );
            break;
        case DETECT_QME :
            dprintf( LOG_LV2,
                    "        load_intra_quantiser_matrix:%d\n"
                    "        load_non_intra_quantiser_matrix:%d\n"
                    "        load_chroma_intra_quantiser_matrix:%d\n"
                    "        load_chroma_non_intra_quantiser_matrix:%d\n"
                    , video_info->quant_matrix_ext.load_intra_quantiser_matrix
                    , video_info->quant_matrix_ext.load_non_intra_quantiser_matrix
                    , video_info->quant_matrix_ext.load_chroma_intra_quantiser_matrix
                    , video_info->quant_matrix_ext.load_chroma_non_intra_quantiser_matrix );
            break;
        case DETECT_PDE :
            dprintf( LOG_LV2,
                    "        number_of_frame_centre_offsets:%d\n"
                    "        offsets[0].horizontal:%d\n"
                    "        offsets[0].vertical_offset:%d\n"
                    "        offsets[1].horizontal:%d\n"
                    "        offsets[1].vertical_offset:%d\n"
                    "        offsets[2].horizontal:%d\n"
                    "        offsets[2].vertical_offset:%d\n"
                    , video_info->picture_display_ext.number_of_frame_centre_offsets
                    , video_info->picture_display_ext.frame_centre_offsets[0].horizontal_offset
                    , video_info->picture_display_ext.frame_centre_offsets[0].vertical_offset
                    , video_info->picture_display_ext.frame_centre_offsets[1].horizontal_offset
                    , video_info->picture_display_ext.frame_centre_offsets[1].vertical_offset
                    , video_info->picture_display_ext.frame_centre_offsets[2].horizontal_offset
                    , video_info->picture_display_ext.frame_centre_offsets[2].vertical_offset );
            break;
        case DETECT_PTSE :
            dprintf( LOG_LV2,
                    "        reference_select_code:%d\n"
                    "        forward_temporal_reference:%d\n"
                    "        backward_temporal_reference:%d\n"
                    , video_info->picture_temporal_scalable_ext.reference_select_code
                    , video_info->picture_temporal_scalable_ext.forward_temporal_reference
                    , video_info->picture_temporal_scalable_ext.backward_temporal_reference );
            break;
        case DETECT_PSSE :
            dprintf( LOG_LV2,
                    "        lower_layer_temporal_reference:%d\n"
                    "        lower_layer_horizontal_offset:%d\n"
                    "        lower_layer_vertical_offset:%d\n"
                    "        spatial_temporal_weight_code_table_index:%d\n"
                    "        lower_layer_progressive_frame:%d\n"
                    "        lower_layer_deinterlaced_field_select:%d\n"
                    , video_info->picture_spatial_scalable_ext.lower_layer_temporal_reference
                    , video_info->picture_spatial_scalable_ext.lower_layer_horizontal_offset
                    , video_info->picture_spatial_scalable_ext.lower_layer_vertical_offset
                    , video_info->picture_spatial_scalable_ext.spatial_temporal_weight_code_table_index
                    , video_info->picture_spatial_scalable_ext.lower_layer_progressive_frame
                    , video_info->picture_spatial_scalable_ext.lower_layer_deinterlaced_field_select );
            break;
        case DETECT_CPRE :
            dprintf( LOG_LV2,
                    "        copyright_flag:%d\n"
                    "        copyright_identifier:%d\n"
                    "        original_or_copy:%d\n"
                    "        copyright_number_1:%d\n"
                    "        copyright_number_2:%d\n"
                    "        copyright_number_3:%d\n"
                    , video_info->copyright_ext.copyright_flag
                    , video_info->copyright_ext.copyright_identifier
                    , video_info->copyright_ext.original_or_copy
                    , video_info->copyright_ext.copyright_number_1
                    , video_info->copyright_ext.copyright_number_2
                    , video_info->copyright_ext.copyright_number_3 );
            break;
        case DETECT_SSC :
            dprintf( LOG_LV2,
                    "[check] detect Slice Start Code.\n" );
            break;
        default :
            break;
    }
}

static int mpegts_get_video_picture_info( mpegts_info_t *info, uint16_t program_id, mpeg_video_info_t *video_info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_video_picture_info()\n" );
    /* parse payload data. */
    mpegts_packet_header_t h;
    start_code_searching_status searching_status = NON_DETECT;
    int32_t ts_packet_length;
    uint8_t mpeg_video_head_data[MPEG_VIDEO_STATRT_CODE_SIZE];
    int no_exist_start_indicator = 1;
    do
    {
        if( mpegts_seek_packet_playload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            return -1;
        /* check start indicator. */
        if( no_exist_start_indicator && !h.payload_unit_start_indicator )
            continue;
        /* check Stream Start Code. */
        if( no_exist_start_indicator )
        {
            fread( mpeg_video_head_data, 1, MPEG_VIDEO_STATRT_CODE_SIZE - 1, info->input );
            ts_packet_length -= MPEG_VIDEO_STATRT_CODE_SIZE - 1;
            no_exist_start_indicator = 0;
        }
        /* search. */
        while( ts_packet_length )
        {
            fread( &(mpeg_video_head_data[MPEG_VIDEO_STATRT_CODE_SIZE - 1]), 1, 1, info->input );
            --ts_packet_length;
            /* check Start Code. */
            static const struct {
                mpeg_video_star_code_type   start_code;
                uint32_t                    read_size;
                start_code_searching_status status;
            } code_list[MPEG_VIDEO_START_CODE_MAX] =
                {
                    { MPEG_VIDEO_START_CODE_SHC , MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE , DETECT_SHC  },
                    { MPEG_VIDEO_START_CODE_ESC , MPEG_VIDEO_HEADER_EXTENSION_MIN_SIZE    , DETECT_ESC  },
                    { MPEG_VIDEO_START_CODE_UDSC, 0                                       , DETECT_UDSC },
                    { MPEG_VIDEO_START_CODE_SEC , 0                                       , DETECT_SEC  },
                    { MPEG_VIDEO_START_CODE_GSC , MPEG_VIDEO_GOP_SECTION_HEADER_SIZE      , DETECT_GSC  },
                    { MPEG_VIDEO_START_CODE_PSC , MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE  , DETECT_PSC  },
                    { MPEG_VIDEO_START_CODE_SSC , MPEG_VIDEO_SLICE_SECTION_HEADER_SIZE    , DETECT_SSC  }
                };
            for( int i = 0; i < MPEG_VIDEO_START_CODE_MAX; ++i )
            {
                if( !code_list[i].read_size || mpeg_video_check_start_code( mpeg_video_head_data, code_list[i].start_code ) )
                    continue;
                mpeg_video_star_code_type start_code = code_list[i].start_code;
                uint32_t read_size                   = code_list[i].read_size;
                searching_status                     = code_list[i].status;
                if( start_code == MPEG_VIDEO_START_CODE_ESC )
                {
                    uint8_t identifier;
                    fread( &identifier, 1, 1, info->input );
                    fseeko( info->input, -1, SEEK_CUR );
                    int extension = mpeg_video_check_extension_start_code_identifier( identifier );
                    static const struct {
                        start_code_searching_status     searching_status;
                        uint32_t                        read_size;
                    } extention_type_list[EXTENSION_TYPE_MAX] =
                        {
                            { NON_DETECT  , 0                                                   },
                            { DETECT_SESC , MPEG_VIDEO_SEQUENCE_EXTENSION_SIZE                  },
                            { DETECT_SDE  , MPEG_VIDEO_SEQUENCE_DISPLAY_EXTENSION_SIZE          },
                            { DETECT_SSE  , MPEG_VIDEO_SEQUENCE_SCALABLE_EXTENSION_SIZE         },
                            { DETECT_PCESC, MPEG_VIDEO_PICTURE_CODING_EXTENSION_SIZE            },
                            { DETECT_QME  , MPEG_VIDEO_QUANT_MATRIX_EXTENSION_SIZE              },
                            { DETECT_PDE  , MPEG_VIDEO_PICTURE_DISPLAY_EXTENSION_SIZE           },
                            { DETECT_PTSE , MPEG_VIDEO_PICTURE_TEMPORAL_SCALABLE_EXTENSION_SIZE },
                            { DETECT_PSSE , MPEG_VIDEO_PICTURE_SPATIAL_SCALABLE_EXTENSION_SIZE  },
                            { DETECT_CPRE , MPEG_VIDEO_COPYRIGHT_EXTENSION_SIZE                 }
                        };
                    searching_status = extention_type_list[extension].searching_status;
                    read_size        = extention_type_list[extension].read_size;
                    if( !read_size )
                        continue;
                }
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
                    if( mpegts_seek_packet_playload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
                        return -1;
                }
                fread( buf_p, 1, read_size, info->input );
                ts_packet_length -= read_size;
                mpeg_video_get_header_info( buf, start_code, video_info );
                /* debug. */
                show_video_info( video_info, searching_status );
                /* check the status detection. */
                if( searching_status == DETECT_SSC )
                    goto end_get_video_picture_info;
                /* cleanup buffer. */
                memset( mpeg_video_head_data, 0, MPEG_VIDEO_STATRT_CODE_SIZE );
                break;
            }
            for( int i = 1; i < MPEG_VIDEO_STATRT_CODE_SIZE; ++i )
                mpeg_video_head_data[i - 1] = mpeg_video_head_data[i];
        }
        dprintf( LOG_LV4, "[debug] continue next packet. buf:0x%02X 0x%02X 0x%02X 0x--\n"
                        , mpeg_video_head_data[0], mpeg_video_head_data[1], mpeg_video_head_data[2] );
    }
    while( 1 );
end_get_video_picture_info:
    return 0;
}

static int mpegts_get_video_pts( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_video_pts()\n" );
    /* search program id. */
    uint16_t program_id = mpegts_get_program_id( info, info->video_stream_type );
    if( program_id == TS_PID_ERR )
        return -1;
    /* get timestamp. */
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, program_id, PES_PACKET_START_CODE_VIDEO_STREAM, &pts, &dts ) )
        return -1;
    /* parse payload data. */
    mpeg_video_info_t video_info;
    memset( &video_info, 0, sizeof(mpeg_video_info_t) );
    if( mpegts_get_video_picture_info( info, program_id, &video_info ) )
        return -1;
    /* setup. */
    info->video_pts          = pts;
    info->video_dts          = dts;
    info->video_frame_type   = video_info.picture.picture_coding_type;
    info->video_order_in_gop = video_info.picture.temporal_reference;
    if( info->video_frame_type == MPEG_VIDEO_I_FRAME )
        info->video_key_pts = pts;
    static const char frame[4] = { '?', 'I', 'P', 'B' };
    dprintf( LOG_LV2, "[check] Video PTS:%"PRId64" [%"PRId64"ms], [%c] temporal_reference:%d\n"
                    , info->video_pts, info->video_pts / 90, frame[info->video_frame_type], info->video_order_in_gop );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", info->read_position );
    /* ready next. */
    info->sync_byte_position = -1;
    return 0;
}

static int mpegts_get_audio_pts( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_audio_pts()\n" );
    /* search program id. */
    uint16_t program_id = mpegts_get_program_id( info, info->audio_stream_type );
    if( program_id == TS_PID_ERR )
        return -1;
    /* get timestamp. */
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, program_id, PES_PACKET_START_CODE_AUDIO_STREAM, &pts, &dts ) )
        return -1;
    /* setup. */
    info->audio_pts = pts;
    info->audio_dts = dts;
    dprintf( LOG_LV2, "[check] Audio PTS:%"PRId64" [%"PRId64"ms]\n", info->audio_pts, info->audio_pts / 90 );
    dprintf( LOG_LV2, "[check] file position:%"PRId64"\n", info->read_position );
    /* ready next. */
    info->sync_byte_position = -1;
    return 0;
}

extern int mpegts_api_get_info( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_info()\n" );
    if( !info || !info->input )
        return -1;
    fpos_t start_fpos;
    fgetpos( info->input, &start_fpos );
    /* check MPEG2-TS and PAT/PMT/PCR packet. */
    if( mpegts_first_check( info ) )
        return -1;
    if( !info->pid_list_in_pat )
        if( mpegts_parse_pat( info ) )
            return -1;
    if( mpegts_parse_pmt( info ) )
        goto fail_get_info;
    if( mpegts_get_pcr( info ) )
        goto fail_get_info;
    /* check video and audio PTS. */
    enum {
        BOTH_VA_NONE  = 0x11,
        VIDEO_NONE    = 0x10,
        AUDIO_NONE    = 0x01,
        BOTH_VA_EXIST = 0x00
    };
    int check_stream_exist = BOTH_VA_EXIST;
    fsetpos( info->input, &start_fpos );
    check_stream_exist |= mpegts_get_video_pts( info ) ? VIDEO_NONE : 0;
    if( !(check_stream_exist & VIDEO_NONE) )
        while( info->video_order_in_gop || info->video_key_pts < 0 )
            if( mpegts_get_video_pts( info ) )
                break;
    fsetpos( info->input, &start_fpos );
    check_stream_exist |= mpegts_get_audio_pts( info ) ? AUDIO_NONE : 0;
    fsetpos( info->input, &start_fpos );
    return !!(check_stream_exist == BOTH_VA_NONE);
fail_get_info:
    if( info->pid_list_in_pmt )
        free( info->pid_list_in_pmt );
    if( info->pid_list_in_pat )
        free( info->pid_list_in_pat );
    fsetpos( info->input, &start_fpos );
    return -1;
}

extern int mpegts_api_set_pmt_program_id( mpegts_info_t *info, uint16_t pmt_program_id )
{
    dprintf( LOG_LV2, "[check] mpegts_api_set_pmt_program_id()\n"
                      "        pmt_program_id: 0x%04X\n", pmt_program_id );
    if( pmt_program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
    {
        dprintf( LOG_LV2, "[check] illegal PID is specified. using PID in PAT.\n" );
        return 1;
    }
    if( info->pid_list_in_pat )
        free( info->pid_list_in_pat );
    info->pid_list_in_pat = malloc( sizeof(uint16_t) );
    if( !info->pid_list_in_pat )
        return -1;
    info->pid_list_num_in_pat = 1;
    info->pid_list_in_pat[0]  = pmt_program_id;
    return 0;
}

extern void mpegts_api_initialize_info( mpegts_info_t *info, FILE *input )
{
    memset( info, 0, sizeof(mpegts_info_t) );
    info->input                   = input;
    info->packet_size             = TS_PACKET_SIZE;
    info->sync_byte_position      = -1;
    info->read_position           = -1;
    info->packet_check_count_num  = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
    info->packet_check_retry_num  = TS_PACKET_SEARCH_RETRY_COUNT_NUM;
    info->pcr                     = -1;
    info->video_key_pts           = -1;
    info->video_pts               = -1;
    info->audio_pts               = -1;
    info->video_order_in_gop      = -1;
}

extern void mpegts_api_release_info( mpegts_info_t *info )
{
    if( info->pid_list_in_pmt )
        free( info->pid_list_in_pmt );
    if( info->pid_list_in_pat )
        free( info->pid_list_in_pat );
}
