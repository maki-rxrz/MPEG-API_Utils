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

#define SKIP_ADAPTATION_FIELD( i, h, s, n )     \
{                                               \
    if( (h).adaptation_field_control > 1 )      \
    {                                           \
        fread( &s, 1, 1, i->input );            \
        fseeko( i->input, s, SEEK_CUR );        \
    }                                           \
    else if( n )                                \
        fseeko( i->input, n, SEEK_CUR );        \
}

static int mpegts_get_table_section_header( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id, uint8_t *section_header, uint16_t section_header_length, int32_t *ts_packet_length )
{
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
    SKIP_ADAPTATION_FIELD( info, *h, adaptation_field_size, 1 );
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    *ts_packet_length -= (adaptation_field_size) ? adaptation_field_size : 1;
    /* read section header. */
    fread( section_header, 1, section_header_length, info->input );
    *ts_packet_length -= section_header_length;
    return 0;
}

static int mpegts_seek_packet_playload_data( mpegts_info_t *info, mpegts_packet_header_t *h, uint16_t search_program_id, int32_t *ts_packet_length, int indicator_check, int indicator_status )
{
    if( mpegts_search_program_id_packet( info, h, search_program_id ) )
        return -1;
    show_packet_header_info( h );
    /* check start indicator. */
    if( indicator_check && (indicator_status == h->payload_unit_start_indicator) )
        return 1;
    *ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
    /* check adaptation field. */
    uint8_t adaptation_field_size = 0;
    SKIP_ADAPTATION_FIELD( info, *h, adaptation_field_size, 0 );
    dprintf( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
    *ts_packet_length -= adaptation_field_size;
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

static int mpegts_get_stream_timestamp( mpegts_info_t *info, mpegts_packet_header_t *h, int64_t *pts_set_p, int64_t *dts_set_p, uint16_t program_id, mpeg_pes_packet_star_code_type start_code )
{
    dprintf( LOG_LV2, "[check] mpegts_get_stream_timestamp()\n" );
    int32_t ts_packet_length;
    int64_t read_pos = -1;
    /* search packet data. */
    uint8_t pes_packet_head_data[PES_PACKET_STATRT_CODE_SIZE];
    int no_exist_start_indicator = 1;
    int64_t pts = -1, dts = -1;
    do
    {
        /* seek playload data. */
        int ret = mpegts_seek_packet_playload_data( info, h, program_id, &ts_packet_length, no_exist_start_indicator, 0 );
        if( ret < 0 )
            return -1;
        if( ret > 0 )
            continue;
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
            int ret = mpegts_seek_packet_playload_data( info, h, program_id, &ts_packet_length, 1, 1 );
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
        fseeko( info->input, ts_packet_length - info->packet_size, SEEK_CUR );
        /* check file position. */
        read_pos = ftello( info->input );
        /* ready next. */
        info->sync_byte_position = 0;
        info->read_position      = read_pos;
    }
    while( pts < 0 );
    return 0;
}

static int mpegts_get_video_pts( mpegts_info_t *info )
{
    dprintf( LOG_LV2, "[check] mpegts_get_video_pts()\n" );
    /* search program id. */
    uint16_t program_id = mpegts_get_program_id( info, STREAM_VIDEO_MPEG2 );
    if( program_id == TS_PID_ERR )
        return -1;
    /* get timestamp. */
    mpegts_packet_header_t h;
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, &h, &pts, &dts, program_id, PES_PACKET_START_CODE_VIDEO_STREAM ) )
        return -1;
    /* parse payload data. */
    typedef enum {
        NON_DETECT = -1,
        DETECT_SHC,
        DETECT_ESC,
        DETECT_UDSC,
        DETECT_SEC,
        DETECT_GSC,
        DETECT_PSC,
        DETECT_SSC
    } start_code_searching_status;
    start_code_searching_status searching_status = NON_DETECT;
    int32_t ts_packet_length;
    mpeg_video_info_t video_info;
    uint8_t mpeg_video_head_data[MPEG_VIDEO_STATRT_CODE_SIZE];
    int no_exist_start_indicator = 1;
    do
    {
        if( mpegts_seek_packet_playload_data( info, &h, program_id, &ts_packet_length, 0, 1 ) )
            return -1;
        /* check start indicator. */
        if( no_exist_start_indicator && !h.payload_unit_start_indicator )
            continue;
        ts_packet_length = TS_PACKET_SIZE - TS_PACKET_HEADER_SIZE;
        /* check Stream Start Code. */
        if( no_exist_start_indicator )
        {
            fread( mpeg_video_head_data, 1, MPEG_VIDEO_STATRT_CODE_SIZE - 1, info->input );
            no_exist_start_indicator = 0;
            ts_packet_length -= (MPEG_VIDEO_STATRT_CODE_SIZE - 1);
        }
        /* search. */
        while( ts_packet_length )
        {
            fread( &(mpeg_video_head_data[MPEG_VIDEO_STATRT_CODE_SIZE - 1]), 1, 1, info->input );
            --ts_packet_length;
            /* check Start Code. */
            if( !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SHC ) )
            {
                uint8_t buf[MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE];
                fread( buf, 1, MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE, info->input );
                ts_packet_length -= MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE;
                mpeg_video_check_section_info( buf, MPEG_VIDEO_START_CODE_SHC, &video_info );
                searching_status = DETECT_SHC;
                dprintf( LOG_LV2,
                        "[check] detect Sequence Start Code.\n"
                        "        frame_size:%dx%d\n"
                        "        aspect_ratio_code:%d\n"
                        "        frame_rate_code:%d\n"
                        , video_info.sequence.horizontal_size
                        , video_info.sequence.vertical_size
                        , video_info.sequence.aspect_ratio_information
                        , video_info.sequence.frame_rate_code );
                /* cleanup buffer. */
                memset( mpeg_video_head_data, 0, MPEG_VIDEO_STATRT_CODE_SIZE );
            }
            else if( !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_GSC ) )
            {
                uint8_t buf[MPEG_VIDEO_GOP_SECTION_HEADER_SIZE];
                fread( buf, 1, MPEG_VIDEO_GOP_SECTION_HEADER_SIZE, info->input );
                ts_packet_length -= MPEG_VIDEO_GOP_SECTION_HEADER_SIZE;
                mpeg_video_check_section_info( buf, MPEG_VIDEO_START_CODE_GSC, &video_info );
                searching_status = DETECT_GSC;
                dprintf( LOG_LV2,
                        "[check] detect GOP Start Code.\n"
                        "        closed_gop:%d\n"
                        "        broken_link:%d\n"
                        , video_info.gop.closed_gop
                        , video_info.gop.broken_link );
                /* cleanup buffer. */
                memset( mpeg_video_head_data, 0, MPEG_VIDEO_STATRT_CODE_SIZE );
            }
            else if( !mpeg_video_check_start_code( mpeg_video_head_data, MPEG_VIDEO_START_CODE_PSC ) )
            {
                uint8_t buf[MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE];
                fread( buf, 1, MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE, info->input );
                ts_packet_length -= MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE;
                mpeg_video_check_section_info( buf, MPEG_VIDEO_START_CODE_PSC, &video_info );
                searching_status = DETECT_PSC;
                dprintf( LOG_LV2,
                        "[check] detect Picture Start Code.\n"
                        "        temporal_reference:%d\n"
                        "        picture_coding_type:%d\n"
                        , video_info.picture.temporal_reference
                        , video_info.picture.picture_coding_type );
                break;
            }
            for( int i = 1; i < MPEG_VIDEO_STATRT_CODE_SIZE; ++i )
                mpeg_video_head_data[i - 1] = mpeg_video_head_data[i];
        }
        /* check the status detection. */
        if( searching_status == DETECT_PSC )
            break;
        dprintf( LOG_LV4, "[debug] coninue next packet. buf:0x%02X 0x%02X 0x%02X 0x--\n"
                        , mpeg_video_head_data[0], mpeg_video_head_data[1], mpeg_video_head_data[2] );
    }
    while( 1 );
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
    uint16_t program_id = mpegts_get_program_id( info, STREAM_AUDIO_AAC );
    if( program_id == TS_PID_ERR )
        return -1;
    /* get timestamp. */
    mpegts_packet_header_t h;
    int64_t pts = -1, dts = -1;
    if( mpegts_get_stream_timestamp( info, &h, &pts, &dts, program_id, PES_PACKET_START_CODE_AUDIO_STREAM ) )
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
