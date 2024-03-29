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

#include "mpeg_common.h"
#include "mpeg_stream.h"
#include "mpeg_video.h"
#include "mpeg_parser.h"
#include "mpegts_def.h"
#include "file_reader.h"
#include "crc.h"

#define SYNC_BYTE                           '\x47'

#define TS_PACKET_SIZE                      (188)
#define TTS_PACKET_SIZE                     (192)
#define FEC_TS_PACKET_SIZE                  (204)

#define TS_PACKET_TYPE_NUM                  (3)
#define TS_PACKET_FIRST_CHECK_COUNT_NUM     (4)
#define TS_PACKET_SEARCH_CHECK_COUNT_NUM    (1000000)
#define TS_PACKET_SEARCH_RETRY_COUNT_NUM    (5)

#define TS_PSI_PACKET_NUM_CHECK_MARGIN      (2)

//#define NEED_OPCR_VALUE
#undef NEED_OPCR_VALUE

typedef struct {
    int32_t                     packet_size;
    int32_t                     sync_byte_position;
    int64_t                     read_position;
    int32_t                     ts_packet_length;
    uint32_t                    packet_check_count_num;
    void                       *fr_ctx;
} mpegts_file_ctx_t;

typedef struct {
    mpegts_file_ctx_t           tsf_ctx;
    uint16_t                    program_id;
    mpeg_stream_type            stream_type;
    mpeg_stream_group_type      stream_judge;
    void                       *stream_parse_info;
    int64_t                     gop_number;
    int32_t                     header_offset;
    struct {
        mpeg_stream_type        reg_stream_type;
        struct {
            char                info[16];
        } keys[GET_INFO_KEY_MAX];
    } private_info;
} mpegts_stream_ctx_t;

typedef struct {
    mpeg_stream_group_type      stream_judge;
    mpeg_stream_type            stream_type;
    uint16_t                    program_id;
    mpeg_stream_type            reg_stream_type;
    uint16_t                    program_number;
    int                         disable_pid;
} mpegts_pid_info_t;

typedef struct {
    uint8_t                     packet_num;
    uint8_t                     packet_cache_count;
    uint16_t                    packet_cache_size;
    uint8_t                    *packet_buffer;
    uint8_t                    *cache_buffer;
    uint8_t                    *section_buffer;
    uint16_t                    section_length;
    uint8_t                     origin_crc32[CRC32_SIZE];
    int32_t                     pid_list_num;
    mpegts_pid_info_t          *pid_list;
    uint16_t                    pmt_program_id;
    uint16_t                    pcr_program_id;
    uint16_t                    ecm_program_id;
    mpegts_stream_ctx_t        *video_stream;
    mpegts_stream_ctx_t        *audio_stream;
    mpegts_stream_ctx_t        *caption_stream;
    mpegts_stream_ctx_t        *dsmcc_stream;
    uint8_t                     video_stream_num;
    uint8_t                     audio_stream_num;
    uint8_t                     caption_stream_num;
    uint8_t                     dsmcc_stream_num;
    int64_t                     pcr;
#ifdef NEED_OPCR_VALUE
    int64_t                     opcr;
#endif
    int64_t                     start_pcr;
    int64_t                     last_pcr;
} mpegts_psi_ctx_t;

typedef struct {
    parser_status_type          status;
    char                       *mpegts;
    int64_t                     file_size;
    int64_t                     buffer_size;
    mpegts_file_ctx_t           tsf_ctx;
    mpegts_psi_ctx_t            pat_ctx;
    mpegts_psi_ctx_t            cat_ctx;
    mpegts_psi_ctx_t           *pmt_ctx;
    int32_t                     pmt_ctx_index;
    uint32_t                    packet_check_retry_num;
    uint16_t                    specified_service_id;
    uint16_t                    specified_pmt_program_id;
    uint16_t                    emm_program_id;
    pmt_target_type             pmt_target;
    mpeg_descriptor_info_t     *descriptor_info;
} mpegts_info_t;

/*  */
#define tsf_ctx_t           mpegts_file_ctx_t
#define tss_ctx_t           mpegts_stream_ctx_t
#define tsp_psi_ctx_t       mpegts_psi_ctx_t

#define tsp_header_t        mpegts_packet_header_t
#define tsp_adpf_header_t   mpegts_adaptation_field_header_t
#define tsp_pat_si_t        mpegts_pat_section_info_t
#define tsp_cat_si_t        mpegts_table_common_info_t
#define tsp_pmt_si_t        mpegts_pmt_section_info_t

static inline void tsp_parse_header( uint8_t *packet, tsp_header_t *h )
{
    h->sync_byte                     =    packet[0];
    h->transport_error_indicator     = !!(packet[1] & 0x80);
    h->payload_unit_start_indicator  = !!(packet[1] & 0x40);
    h->transport_priority            = !!(packet[1] & 0x20);
    h->program_id                    =   (packet[1] & 0x1F) << 8
                                     |    packet[2];
    h->transport_scrambling_control  =    packet[3]         >> 6;
    h->adaptation_field_control      =   (packet[3] & 0x30) >> 4;
    h->continuity_counter            =    packet[3] & 0x0F;
}

static inline void tsp_parse_adpf_header( uint8_t *adpf_data, tsp_adpf_header_t *h )
{
    h->discontinuity_indicator              = !!(adpf_data[0] & 0x80);
    h->random_access_indicator              = !!(adpf_data[0] & 0x40);
    h->elementary_stream_priority_indicator = !!(adpf_data[0] & 0x20);
    h->pcr_flag                             = !!(adpf_data[0] & 0x10);
    h->opcr_flag                            = !!(adpf_data[0] & 0x08);
    h->splicing_point_flag                  = !!(adpf_data[0] & 0x04);
    h->transport_private_data_flag          = !!(adpf_data[0] & 0x02);
    h->adaptation_field_extension_flag      =    adpf_data[0] & 0x01;
}

static inline void tsp_get_pcr( uint8_t *pcr_data, int64_t *pcr_value )
{
    int64_t pcr_base, pcr_ext;
    pcr_base = (int64_t) pcr_data[0] << 25
             | (int64_t) pcr_data[1] << 17
             | (int64_t) pcr_data[2] << 9
             | (int64_t) pcr_data[3] << 1
             | (int64_t) pcr_data[4] >> 7;
    pcr_ext  = (int64_t)(pcr_data[4] & 0x01) << 8
             | (int64_t) pcr_data[5];
    mapi_log( LOG_LV3, "[check] pcr_base:%" PRId64 " pcr_ext:%" PRId64 "\n", pcr_base, pcr_ext );
    /* set up. */
    *pcr_value = pcr_base + pcr_ext / 300;
}

static inline void tsp_parse_pat_header( uint8_t *section_header, tsp_pat_si_t *pat_si )
{
    pat_si->table_id                 =    section_header[0];
    pat_si->section_syntax_indicator = !!(section_header[1] & 0x80);
    /* '0'              1 bit        = !!(section_header[1] & 0x40);            */
    /* reserved '11'    2 bit        =   (section_header[1] & 0x30) >> 4;       */
    pat_si->section_length           =   (section_header[1] & 0x0F) << 8
                                     |    section_header[2];
    pat_si->transport_stream_id      =    section_header[3]         << 8
                                     |    section_header[4];
    /* reserved '11'    2 bit        =    section_header[5]         >> 6;       */
    pat_si->version_number           =   (section_header[5] & 0x3E) >> 1;
    pat_si->current_next_indicator   =    section_header[5] & 0x01;
    pat_si->section_number           =    section_header[6];
    pat_si->last_section_number      =    section_header[7];
}

static inline void tsp_parse_cat_header( uint8_t *section_header, tsp_cat_si_t *cat_si )
{
    cat_si->table_id                 =    section_header[0];
    cat_si->section_syntax_indicator = !!(section_header[1] & 0x80);
    /* '0'              1 bit        = !!(section_header[1] & 0x40);            */
    /* reserved '11'    2 bit        =   (section_header[1] & 0x30) >> 4;       */
    cat_si->section_length           =   (section_header[1] & 0x0F) << 8
                                     |    section_header[2];
    /* reserved        18 bit        =    section_header[3]         << 10
                                     |    section_header[4]         << 2
                                     |    section_header[5]         >> 6;       */
    cat_si->id_number                =    TS_PID_ERR;
    cat_si->version_number           =   (section_header[5] & 0x3E) >> 1;
    cat_si->current_next_indicator   =    section_header[5] & 0x01;
    cat_si->section_number           =    section_header[6];
    cat_si->last_section_number      =    section_header[7];
}

static inline void tsp_parse_pmt_header( uint8_t *section_header, tsp_pmt_si_t *pmt_si )
{
    pmt_si->table_id                 =    section_header[ 0];
    pmt_si->section_syntax_indicator = !!(section_header[ 1] & 0x80);
    /* '0'              1 bit        = !!(section_header[ 1] & 0x40);           */
    /* reserved '11'    2 bit        =   (section_header[ 1] & 0x30) >> 4;      */
    pmt_si->section_length           =   (section_header[ 1] & 0x0F) << 8
                                     |    section_header[ 2];
    pmt_si->program_number           =    section_header[ 3]         << 8
                                     |    section_header[ 4];
    /* reserved '11'    2 bit        =    section_header[ 5]         >> 6;      */
    pmt_si->version_number           =   (section_header[ 5] & 0x3E) >> 1;
    pmt_si->current_next_indicator   =    section_header[ 5] & 0x01;
    pmt_si->section_number           =    section_header[ 6];
    pmt_si->last_section_number      =    section_header[ 7];
    pmt_si->pcr_program_id           =   (section_header[ 8] & 0x1F) << 8
                                     |    section_header[ 9];
    pmt_si->program_info_length      =   (section_header[10] & 0x0F) << 8
                                     |    section_header[11];
}

static inline int64_t mpegts_get_file_size( tsf_ctx_t *tsf_ctx )
{
    return file_reader.get_size( tsf_ctx->fr_ctx );
}

static inline int64_t mpegts_ftell( tsf_ctx_t *tsf_ctx )
{
    return file_reader.ftell( tsf_ctx->fr_ctx );
}

static inline int mpegts_fread( tsf_ctx_t *tsf_ctx, uint8_t *read_buffer, int64_t read_size, int64_t *dest_size )
{
    return file_reader.fread( tsf_ctx->fr_ctx, read_buffer, read_size, dest_size );
}

static inline int mpegts_fseek( tsf_ctx_t *tsf_ctx, int64_t seek_offset, int origin )
{
    return file_reader.fseek( tsf_ctx->fr_ctx, seek_offset, origin );
}

static int mpegts_open( tsf_ctx_t *tsf_ctx, char *file_name, int64_t buffer_size )
{
    if( !tsf_ctx )
        return -1;
    void *fr_ctx = NULL;
    if( file_reader.init( &fr_ctx ) )
        return -1;
    if( file_reader.open( fr_ctx, file_name, buffer_size ) )
        goto fail;
    tsf_ctx->fr_ctx = fr_ctx;
    return 0;
fail:
    file_reader.release( &fr_ctx );
    return -1;
}

static void mpegts_close( tsf_ctx_t *tsf_ctx )
{
    if( !tsf_ctx || !tsf_ctx->fr_ctx )
        return;
    file_reader.close( tsf_ctx->fr_ctx );
    file_reader.release( &(tsf_ctx->fr_ctx) );
}

static int32_t mpegts_check_sync_byte_position( tsf_ctx_t *tsf_ctx, int32_t packet_size, int packet_check_count )
{
    int32_t position       = -1;
    int64_t start_position = mpegts_ftell( tsf_ctx );
    uint8_t c;
    while( mpegts_fread( tsf_ctx, &c, 1, NULL ) == MAPI_SUCCESS && position < packet_size )
    {
        ++position;
        if( c != SYNC_BYTE )
            continue;
        int64_t reset_position = mpegts_ftell( tsf_ctx );
        int     check_count    = packet_check_count;
        while( check_count )
        {
            if( mpegts_fseek( tsf_ctx, packet_size - 1, SEEK_CUR ) )
                goto no_sync_byte;
            if( mpegts_fread( tsf_ctx, &c, 1, NULL ) == MAPI_EOF )
                goto detect_sync_byte;
            if( c != SYNC_BYTE )
                break;
            --check_count;
        }
        if( !check_count )
            break;
        mpegts_fseek( tsf_ctx, reset_position, SEEK_SET );
    }
detect_sync_byte:
    mpegts_fseek( tsf_ctx, start_position, SEEK_SET );
    if( position >= packet_size )
        return -1;
    return position;
no_sync_byte:
    mpegts_fseek( tsf_ctx, start_position, SEEK_SET );
    return -1;
}

static void mpegts_file_read( tsf_ctx_t *tsf_ctx, uint8_t *read_buffer, int64_t read_size )
{
    if( read_size > tsf_ctx->ts_packet_length )
    {
        mapi_log( LOG_LV1, "[log] illegal parameter!!  packet_len:%d  read_size:%" PRId64 "\n"
                         , tsf_ctx->ts_packet_length, read_size );
        read_size = tsf_ctx->ts_packet_length;
    }
    mpegts_fread( tsf_ctx, read_buffer, read_size, NULL );
    tsf_ctx->ts_packet_length -= read_size;
}

typedef enum {
    MPEGTS_SEEK_CUR,
    MPEGTS_SEEK_NEXT,
    MPEGTS_SEEK_SET,
    MPEGTS_SEEK_RESET
} mpegts_seek_type;

static void mpegts_file_seek( tsf_ctx_t *tsf_ctx, int64_t seek_offset, mpegts_seek_type seek_type )
{
    int origin = (seek_type == MPEGTS_SEEK_CUR || seek_type == MPEGTS_SEEK_NEXT) ? SEEK_CUR : SEEK_SET;
    if( seek_type == MPEGTS_SEEK_NEXT )
        seek_offset += tsf_ctx->ts_packet_length + tsf_ctx->packet_size - TS_PACKET_SIZE;
    mpegts_fseek( tsf_ctx, seek_offset, origin );
    tsf_ctx->sync_byte_position = -1;
    /* set ts_packet_length. */
    switch( seek_type )
    {
        case MPEGTS_SEEK_CUR :
            tsf_ctx->ts_packet_length -= seek_offset;
            break;
        case MPEGTS_SEEK_NEXT :
            tsf_ctx->ts_packet_length = 0;
            break;
        case MPEGTS_SEEK_SET :
            //tsf_ctx->ts_packet_length = ???;
            break;
        case MPEGTS_SEEK_RESET :
            tsf_ctx->ts_packet_length = TS_PACKET_SIZE;
            break;
        default :
            break;
    }
}

static int mpegts_seek_sync_byte_position( tsf_ctx_t *tsf_ctx )
{
    if( !tsf_ctx->sync_byte_position )
        goto detect_sync_byte;
    if( tsf_ctx->sync_byte_position < 0 )
        tsf_ctx->sync_byte_position = mpegts_check_sync_byte_position( tsf_ctx, tsf_ctx->packet_size, 1 );
    if( tsf_ctx->sync_byte_position < 0 || mpegts_fseek( tsf_ctx, tsf_ctx->sync_byte_position, SEEK_CUR ) )
        return -1;
detect_sync_byte:
    tsf_ctx->sync_byte_position = 0;
    tsf_ctx->ts_packet_length   = TS_PACKET_SIZE;
    tsf_ctx->read_position      = mpegts_ftell( tsf_ctx );
    return 0;
}

static int mpegts_first_check( tsf_ctx_t *tsf_ctx )
{
    int result = -1;
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    for( int i = 0; i < TS_PACKET_TYPE_NUM; ++i )
    {
        static const int32_t tsp_size[TS_PACKET_TYPE_NUM] =
            {
                TS_PACKET_SIZE, TTS_PACKET_SIZE, FEC_TS_PACKET_SIZE
            };
        int32_t position = mpegts_check_sync_byte_position( tsf_ctx, tsp_size[i], TS_PACKET_FIRST_CHECK_COUNT_NUM );
        if( position != -1 )
        {
            tsf_ctx->packet_size        = tsp_size[i];
            tsf_ctx->sync_byte_position = position;
            result = 0;
            mapi_log( LOG_LV3, "[check] packet size:%d\n", tsf_ctx->packet_size );
            break;
        }
    }
    return result;
}

#define TS_PACKET_HEADER_SIZE               (4)

static inline void show_packet_header_info( tsp_header_t *h )
{
    mapi_log( LOG_LV4,
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

static int mpegts_read_packet_header( tsf_ctx_t *tsf_ctx, tsp_header_t *h )
{
    if( mpegts_seek_sync_byte_position( tsf_ctx ) )
        return -1;
    uint8_t ts_header[TS_PACKET_HEADER_SIZE];
    mpegts_file_read( tsf_ctx, ts_header, TS_PACKET_HEADER_SIZE );
    /* setup header data. */
    tsp_parse_header( ts_header, h );
    /* initialize status. */
    tsf_ctx->sync_byte_position = -1;
    return 0;
}

#define TS_PID_PAT_SECTION_HEADER_SIZE          (8)
#define TS_PID_CAT_SECTION_HEADER_SIZE          (8)
#define TS_PID_PMT_SECTION_HEADER_SIZE          (12)
#define TS_PACKET_PAT_SECTION_DATA_SIZE         (4)
#define TS_PACKET_PMT_SECTION_DATA_SIZE         (5)
#define TS_PACKET_TABLE_SECTION_SIZE_MAX        (1024)      /* 10bits: size is 12bits, first 2bits = '00' */

static inline void show_table_section_info( void *si_p )
{
    mpegts_table_info_t *table_si = (mpegts_table_info_t *)si_p;
    mapi_log( LOG_LV4,
              "[check] table_id:0x%02X\n"
              "        syntax_indicator:%d\n"
              "        section_length:%d\n"
              "        id_number:0x%04X\n"
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

static inline void show_pmt_section_info( tsp_pmt_si_t *pmt_si )
{
    show_table_section_info( pmt_si );
    mapi_log( LOG_LV4,
              "        PCR_PID:0x%04X\n"
              "        program_info_length:%d\n"
              , pmt_si->pcr_program_id
              , pmt_si->program_info_length );
}

static int mpegts_search_program_id_packet( tsf_ctx_t *tsf_ctx, tsp_header_t *h, uint16_t search_program_id )
{
    int check_count = tsf_ctx->packet_check_count_num;
    do
    {
        if( !check_count )
            return 1;
        --check_count;
        if( mpegts_read_packet_header( tsf_ctx, h ) )
            return -1;
        if( h->program_id == search_program_id )
            break;
        /* seek next packet head. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
    return 0;
}

static void mpegts_get_adaptation_field_data
(
    tsf_ctx_t                  *tsf_ctx,
    tsp_header_t               *h,
    uint8_t                    *adpf_data,
    uint8_t                     adaptation_field_size
)
{
    uint8_t discontinuity_indicator = 0;
    uint8_t pcr_flag                = 0;
    uint8_t opcr_flag               = 0;
    /* check adaptation field. */
    if( h->adaptation_field_control > 1 )
    {
        /* check adaptation field data. */
        mpegts_file_read( tsf_ctx, adpf_data, adaptation_field_size );
        /* read header. */
        tsp_adpf_header_t adpf_header;
        tsp_parse_adpf_header( adpf_data, &adpf_header );
        /* setup. */
        discontinuity_indicator = adpf_header.discontinuity_indicator;
        pcr_flag                = adpf_header.pcr_flag;
        opcr_flag               = adpf_header.opcr_flag;
    }
    /* setup header data. */
    h->adpf_info.adaptation_field_size   = adaptation_field_size;
    h->adpf_info.discontinuity_indicator = discontinuity_indicator;
    h->adpf_info.pcr_flag                = pcr_flag;
    h->adpf_info.opcr_flag               = opcr_flag;
}

static int mpegts_get_table_section_header
(
    tsf_ctx_t                  *tsf_ctx,
    tsp_header_t               *h,
    uint16_t                    search_program_id,
    uint8_t                    *section_header,
    uint16_t                    section_header_length
)
{
    mapi_log( LOG_LV4, "[check] %s()\n", __func__ );
    do
    {
        int search_result = mpegts_search_program_id_packet( tsf_ctx, h, search_program_id );
        if( search_result )
        {
            mapi_log( LOG_LV4, "[check] search_result:%d\n", search_result );
            return search_result;
        }
        show_packet_header_info( h );
    }
    while( !h->payload_unit_start_indicator );
    /* check adaptation field. */
    if( h->adaptation_field_control > 1 )
    {
        uint8_t adaptation_field_size = 0;
        mpegts_file_read( tsf_ctx, &adaptation_field_size, 1 );
        mapi_log( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
        /* get adaptation field data. */
        uint8_t adpf_data[adaptation_field_size];
        mpegts_get_adaptation_field_data( tsf_ctx, h, adpf_data, adaptation_field_size );
    }
    /* check pointer field. */
    uint8_t pointer_field;
    mpegts_file_read( tsf_ctx, &pointer_field, 1 );
    mapi_log( LOG_LV4, "[check] pointer_field:%d\n", pointer_field );
    if( pointer_field )
        mpegts_file_seek( tsf_ctx, pointer_field, MPEGTS_SEEK_CUR );
    /* read section header. */
    mpegts_file_read( tsf_ctx, section_header, section_header_length );
    return 0;
}

typedef enum {
    INDICATOR_UNCHECKED = 0x00,
    INDICATOR_IS_OFF    = 0x01,
    INDICATOR_IS_ON     = 0x02
} indicator_check_type;

static int mpegts_seek_packet_payload_data
(
    tsf_ctx_t                  *tsf_ctx,
    tsp_header_t               *h,
    uint16_t                    search_program_id,
    indicator_check_type        indicator_check
)
{
    mapi_log( LOG_LV4, "[check] %s()\n", __func__ );
    if( mpegts_search_program_id_packet( tsf_ctx, h, search_program_id ) )
        return -1;
    show_packet_header_info( h );
    /* check start indicator. */
    if( indicator_check != INDICATOR_UNCHECKED && ((indicator_check == INDICATOR_IS_ON) == h->payload_unit_start_indicator) )
        return 1;
    /* check adaptation field. */
    if( h->adaptation_field_control > 1 )
    {
        uint8_t adaptation_field_size = 0;
        mpegts_file_read( tsf_ctx, &adaptation_field_size, 1 );
        mapi_log( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
        /* get adaptation field data. */
        uint8_t adpf_data[adaptation_field_size];
        mpegts_get_adaptation_field_data( tsf_ctx, h, adpf_data, adaptation_field_size );
    }
    return 0;
}

static int mpegts_get_table_section_data
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    search_program_id,
    uint8_t                    *section_buffer,
    uint16_t                    section_length,
    uint8_t                    *psi_packet_num
)
{
    tsp_header_t h;
    int64_t start_position        = mpegts_ftell( tsf_ctx );
    int32_t rest_ts_packet_length = tsf_ctx->ts_packet_length;
    /* buffering payload data. */
    int read_count                  = 0;
    int need_ts_packet_payload_data = 0;
    *psi_packet_num = 0;
    while( read_count < section_length )
    {
        ++(*psi_packet_num);
        if( need_ts_packet_payload_data )
        {
            /* seek next packet payload data. */
            if( mpegts_seek_packet_payload_data( tsf_ctx, &h, search_program_id, INDICATOR_IS_ON ) )
                return -1;
        }
        else
            need_ts_packet_payload_data = 1;
        int32_t read_size = (section_length - read_count > tsf_ctx->ts_packet_length)
                          ? tsf_ctx->ts_packet_length : section_length - read_count;
        mpegts_file_read( tsf_ctx, &(section_buffer[read_count]), read_size );
        read_count += read_size;
        mapi_log( LOG_LV4, "[check] section data read:%d, rest_packet:%d\n", read_size, tsf_ctx->ts_packet_length );
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    /* reset buffering start packet position. */
    mpegts_file_seek( tsf_ctx, start_position, MPEGTS_SEEK_SET );
    tsf_ctx->ts_packet_length = rest_ts_packet_length;
    return 0;
}

static int mpegts_set_user_specified_pmt( mpegts_info_t *info, uint16_t program_id )
{
    /* check for all. */
    if( program_id == MAPI_ALL_SERVICE_PMT_PID )
    {
        info->pmt_ctx_index = info->pat_ctx.pid_list_num;
        /* reset disable flag. */
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
            info->pat_ctx.pid_list[i].disable_pid = 0;
        return 0;
    }
    /* search in PAT. */
    int32_t pmt_ctx_index;
    for( pmt_ctx_index = 0; pmt_ctx_index < info->pat_ctx.pid_list_num; ++pmt_ctx_index )
        if( info->pat_ctx.pid_list[pmt_ctx_index].program_id == program_id )
            break;
    if( pmt_ctx_index == info->pat_ctx.pid_list_num )
        return -1;
    if( info->pat_ctx.pid_list[pmt_ctx_index].program_number == 0 )
        return -1;
    /* setup active PMT. */
    info->pmt_ctx_index = pmt_ctx_index;
    return 0;
}

static uint16_t mpegts_get_pmt_pid_from_sid( mpegts_info_t *info, uint16_t service_id )
{
    /* search in PAT. */
    for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
        if( info->pat_ctx.pid_list[i].program_number == service_id )
            return info->pat_ctx.pid_list[i].program_id;
    return TS_PID_ERR;
}

static int mpegts_search_pat_packet( tsf_ctx_t *tsf_ctx, tsp_pat_si_t *pat_si )
{
    tsp_header_t h;
    uint8_t section_header[TS_PID_PAT_SECTION_HEADER_SIZE];
    do
        if( mpegts_get_table_section_header( tsf_ctx, &h, TS_PID_PAT, section_header, TS_PID_PAT_SECTION_HEADER_SIZE ) )
            return -1;
    while( section_header[0] != PSI_TABLE_ID_PAT );
    /* setup header data. */
    tsp_parse_pat_header( section_header, pat_si );
    show_table_section_info( pat_si );
    return 0;
}

static int mpegts_parse_pat( mpegts_info_t *info )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    int64_t read_pos = -1;
    /* search. */
    tsp_pat_si_t pat_si;
    uint8_t psi_packet_num;
    int     section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int     retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_pat_packet( &(info->tsf_ctx), &pat_si ) )
            return -1;
        /* get section length. */
        section_length = pat_si.section_length - 5;     /* 5: section_header[3]-[7] */
        if( (section_length - CRC32_SIZE) % TS_PACKET_PAT_SECTION_DATA_SIZE )
            continue;
        /* check file position. */
        read_pos = info->tsf_ctx.read_position;
        /* buffering section data. */
        if( mpegts_get_table_section_data( &(info->tsf_ctx), TS_PID_PAT, section_buffer, section_length, &psi_packet_num ) )
            continue;
        break;
    }
    if( !retry_count )
        return -1;
    /* listup. */
    info->pat_ctx.pid_list = (mpegts_pid_info_t *)malloc( sizeof(mpegts_pid_info_t) * ((section_length - CRC32_SIZE) / TS_PACKET_PAT_SECTION_DATA_SIZE) );
    if( !info->pat_ctx.pid_list )
        return -1;
    int32_t pid_list_num = 0, read_count = 0;
    while( read_count < section_length - CRC32_SIZE )
    {
        uint8_t *section_data   = &(section_buffer[read_count]);
        uint16_t program_number =  (section_data[0] << 8) | section_data[1];
        /* '111'        3 bit   =  (section_data[2] & 0xE0) >> 4;       */
        uint16_t pmt_program_id = ((section_data[2] & 0x1F) << 8) | section_data[3];
        mapi_log( LOG_LV2, "[check] program_number:%d, pmt_PID:0x%04X\n", program_number, pmt_program_id );
        read_count += TS_PACKET_PAT_SECTION_DATA_SIZE;
        if( program_number )
        {
            info->pat_ctx.pid_list[pid_list_num].program_id     = pmt_program_id;
            info->pat_ctx.pid_list[pid_list_num].program_number = program_number;
            ++pid_list_num;
        }
    }
    if( (section_length - read_count) != CRC32_SIZE )
    {
        free( info->pat_ctx.pid_list );
        info->pat_ctx.pid_list = NULL;
        return -1;
    }
    info->pat_ctx.pid_list_num = pid_list_num;
    uint8_t *crc32 = &(section_buffer[read_count]);
    char crc32_str[CRC32_SIZE * 3 + 1] = { 0 };
    for( int i = 0; i < CRC32_SIZE; ++i )
        sprintf( &(crc32_str[i * 3]), " %02X", crc32[i] );
    mapi_log( LOG_LV2, "[check] CRC32:%s\n", crc32_str );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", read_pos );
    /* setup and prepare buffer. */
    info->pat_ctx.packet_num     = psi_packet_num;
    info->pat_ctx.packet_buffer  = (uint8_t *)malloc( info->tsf_ctx.packet_size * (psi_packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) * pid_list_num );       // info->tsf_ctx.packet_size or TS_PACKET_SIZE
    info->pat_ctx.section_buffer = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX * pid_list_num );
    info->pat_ctx.cache_buffer   = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX );
    if( !info->pat_ctx.packet_buffer || !info->pat_ctx.section_buffer || !info->pat_ctx.cache_buffer )
    {
        if( info->pat_ctx.packet_buffer )
        {
            free( info->pat_ctx.packet_buffer );
            info->pat_ctx.packet_buffer = NULL;
        }
        if( info->pat_ctx.section_buffer )
        {
            free( info->pat_ctx.section_buffer );
            info->pat_ctx.section_buffer = NULL;
        }
        if( info->pat_ctx.cache_buffer )
        {
            free( info->pat_ctx.cache_buffer );
            info->pat_ctx.cache_buffer = NULL;
        }
        free( info->pat_ctx.pid_list );
        info->pat_ctx.pid_list = NULL;
        return -1;
    }
    /* seek next. */
    mpegts_file_seek( &(info->tsf_ctx), 0, MPEGTS_SEEK_NEXT );
    info->tsf_ctx.sync_byte_position = -1;
    info->tsf_ctx.read_position      = read_pos;
    return 0;
}

static int mpegts_get_ca_pid_from_descriptor
(
    mpeg_descriptor_info_t *descriptor_info,
    uint16_t                descriptor_num,
    uint16_t               *ca_pid
)
{
    if( !descriptor_num || !descriptor_info->conditional_access || !descriptor_info->conditional_access->CA_system_ID )
        return -1;
    *ca_pid = descriptor_info->conditional_access->CA_PID;
    return 0;
}

static int mpegts_parse_descriptor
(
    uint8_t                *descriptor_data,
    int                     data_length,
    mpeg_descriptor_info_t *descriptor_info,
    uint16_t               *descriptor_num
)
{
    uint16_t tags_num   = 0;
    uint16_t read_count = 0;
    /* init. */
    mpeg_stream_init_descriptor_info( descriptor_info );
    /* parse. */
    while( read_count < data_length - 2 )
    {
        if( mpeg_stream_get_descriptor_info( /* stream_type, */ &(descriptor_data[read_count]), descriptor_info ) )
            return -1;
        uint8_t descriptor_length = descriptor_info->length;
        char descriptor_data_str[descriptor_length * 2 + 1]; //= { 0 };
        descriptor_data_str[descriptor_length * 2] = 0;
        for( int i = 0; i < descriptor_length; ++i )
            sprintf( &(descriptor_data_str[i * 2]), "%02X", descriptor_data[read_count + i + 2] );
        mapi_log( LOG_LV2, "[check] descriptor_tag:0x%02X, descriptor_length:%u, [%s]\n"
                         , descriptor_info->tags[tags_num], descriptor_length, descriptor_data_str );
        mpeg_stream_debug_descriptor_info( descriptor_info, tags_num );
        /* next descriptor. */
        read_count += descriptor_length + 2;
        ++tags_num;
    }
    *descriptor_num = tags_num;
    return 0;
}

static int mpegts_search_cat_packet( tsf_ctx_t *tsf_ctx, tsp_cat_si_t *cat_si )
{
    tsp_header_t h;
    uint8_t section_header[TS_PID_CAT_SECTION_HEADER_SIZE];
    do
        if( mpegts_get_table_section_header( tsf_ctx, &h, TS_PID_CAT, section_header, TS_PID_CAT_SECTION_HEADER_SIZE ) )
            return -1;
    while( section_header[0] != PSI_TABLE_ID_CAT );
    /* setup header data. */
    tsp_parse_cat_header( section_header, cat_si );
    show_table_section_info( cat_si );
    return 0;
}

static int mpegts_parse_cat( mpegts_info_t *info )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    int64_t reset_position = mpegts_ftell( &(info->tsf_ctx) );
    int64_t read_pos       = -1;
    /* search. */
    tsp_cat_si_t cat_si;
    uint8_t psi_packet_num;
    int     section_length;
    uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    int     retry_count = info->packet_check_retry_num;
    while( retry_count )
    {
        --retry_count;
        if( mpegts_search_cat_packet( &(info->tsf_ctx), &cat_si ) )
            goto fail_parse;
        /* get section length. */
        section_length = cat_si.section_length - 5;     /* 5: section_header[3]-[7] */
        if( (section_length - CRC32_SIZE) < 0 )
            continue;
        /* check file position. */
        read_pos = info->tsf_ctx.read_position;
        /* buffering section data. */
        if( mpegts_get_table_section_data( &(info->tsf_ctx), TS_PID_CAT, section_buffer, section_length, &psi_packet_num ) )
            continue;
        break;
    }
    if( !retry_count )
        goto fail_parse;
    int32_t read_count = 0;
    if( section_length - CRC32_SIZE > 0 )
    {
        int descriptor_length = section_length - CRC32_SIZE;
        mpeg_descriptor_info_t *descriptor_info = info->descriptor_info;
        /* parse descriptor. */
        uint8_t  *descriptor_data = &(section_buffer[read_count]);
        uint16_t  descriptor_num  = 0;
        if( mpegts_parse_descriptor( descriptor_data, descriptor_length, descriptor_info, &descriptor_num ) )
            goto fail_parse;
        if( !mpegts_get_ca_pid_from_descriptor( descriptor_info, descriptor_num, &(info->emm_program_id) ) )
            mapi_log( LOG_LV2, "[check] emm_PID:0x%04X\n", info->emm_program_id );
        read_count += descriptor_length;
    }
    uint8_t *crc32 = &(section_buffer[read_count]);
    char crc32_str[CRC32_SIZE * 3 + 1] = { 0 };
    for( int i = 0; i < CRC32_SIZE; ++i )
        sprintf( &(crc32_str[i * 3]), " %02X", crc32[i] );
    mapi_log( LOG_LV2, "[check] CRC32:%s\n", crc32_str );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", read_pos );
    /* setup and prepare buffer. */
    info->cat_ctx.packet_num     = psi_packet_num;
#if 0
    info->cat_ctx.packet_buffer  = (uint8_t *)malloc( info->tsf_ctx.packet_size * (psi_packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) );      // info->tsf_ctx.packet_size or TS_PACKET_SIZE
    info->cat_ctx.section_buffer = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX );
    info->cat_ctx.cache_buffer   = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX );
    if( !info->cat_ctx.packet_buffer || !info->cat_ctx.section_buffer || !info->cat_ctx.cache_buffer )
    {
        if( info->cat_ctx.packet_buffer )
        {
            free( info->cat_ctx.packet_buffer );
            info->cat_ctx.packet_buffer = NULL;
        }
        if( info->cat_ctx.section_buffer )
        {
            free( info->cat_ctx.section_buffer );
            info->cat_ctx.section_buffer = NULL;
        }
        if( info->cat_ctx.cache_buffer )
        {
            free( info->cat_ctx.cache_buffer );
            info->cat_ctx.cache_buffer = NULL;
        }
        goto fail_parse;
    }
#endif
    /* reset position. */
    mpegts_file_seek( &(info->tsf_ctx), reset_position, MPEGTS_SEEK_RESET );
    return 0;
fail_parse:
    mpegts_file_seek( &(info->tsf_ctx), reset_position, MPEGTS_SEEK_RESET );
    return -1;
}

static int mpegts_search_pmt_packet( mpegts_info_t *info, tsp_pmt_si_t *pmt_si, int32_t pid_list_index )
{
    /* search. */
    tsp_header_t h;
    uint8_t section_header[TS_PID_PMT_SECTION_HEADER_SIZE];
    do
        if( mpegts_get_table_section_header( &(info->tsf_ctx), &h, info->pat_ctx.pid_list[pid_list_index].program_id
                                           , section_header, TS_PID_PMT_SECTION_HEADER_SIZE ) )
            return -1;
    while( section_header[0] != PSI_TABLE_ID_PMT );
    /* setup header data. */
    tsp_parse_pmt_header( section_header, pmt_si );
    show_pmt_section_info( pmt_si );
    return 0;
}

#define PMT_PARSE_COUNT_NUM     (9)

static int mpegts_parse_pmt( mpegts_info_t *info, int32_t pmt_ctx_index )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    int64_t read_pos = -1;
    /* prepare. */
    uint8_t *buffer_data = (uint8_t *)malloc( PMT_PARSE_COUNT_NUM * TS_PACKET_TABLE_SECTION_SIZE_MAX );
    if( !buffer_data )
        return -1;
    /* search. */
    uint16_t pmt_program_id = TS_PID_ERR, pcr_program_id = TS_PID_ERR;
    tsp_pmt_si_t pmt_si;
    uint8_t  pmt_packet_num[PMT_PARSE_COUNT_NUM]  = { 0 };
    int      prg_inf_lengths[PMT_PARSE_COUNT_NUM] = { 0 };
    int      section_lengths[PMT_PARSE_COUNT_NUM] = { 0 };
    uint8_t *section_buffers[PMT_PARSE_COUNT_NUM];
    int32_t  section_pid_num[PMT_PARSE_COUNT_NUM] = { 0 };
    for( int i = 0; i < PMT_PARSE_COUNT_NUM; ++i )
        section_buffers[i] = (uint8_t *)(buffer_data + i * TS_PACKET_TABLE_SECTION_SIZE_MAX);
    int64_t reset_position = -1;
    int64_t check_offset   = info->file_size / (PMT_PARSE_COUNT_NUM + 1);
    check_offset -= check_offset % info->tsf_ctx.packet_size;
    for( int i = 0; i < PMT_PARSE_COUNT_NUM; ++i )
    {
        /* search. */
        uint8_t psi_packet_num;
        int section_length = 0, prg_inf_length = 0;
        int retry_count = info->packet_check_retry_num;
        while( retry_count )
        {
            --retry_count;
            if( mpegts_search_pmt_packet( info, &pmt_si, pmt_ctx_index ) )
            {
                retry_count = 0;
                break;
            }
            /* get PMT/PCR PID. */
            pmt_program_id = info->pat_ctx.pid_list[pmt_ctx_index].program_id;
            pcr_program_id = pmt_si.pcr_program_id;
            /* get section length. */
            section_length = pmt_si.section_length - 9;     /* 9: section_header[3]-[11] */
            prg_inf_length = pmt_si.program_info_length;
            mapi_log( LOG_LV4, "[check] section_length:%d\n", section_length );
            /* check file position. */
            read_pos = info->tsf_ctx.read_position;
            /* buffering section data. */
            if( mpegts_get_table_section_data( &(info->tsf_ctx), pmt_program_id, section_buffers[i], section_length, &psi_packet_num ) )
                continue;
            /* check pid list num. */
            int32_t pid_list_num = 0, read_count = prg_inf_length;
            while( read_count < section_length - CRC32_SIZE )
            {
                uint8_t *section_data   = &(section_buffers[i][read_count]);
                uint16_t ES_info_length = ((section_data[3] & 0x0F) << 8) | section_data[4];
                /* seek next section. */
                read_count += TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length;
                ++pid_list_num;
            }
            if( (section_length - read_count) != CRC32_SIZE )
                continue;
            section_pid_num[i] = pid_list_num;
            break;
        }
        if( retry_count )
        {
            if( reset_position < 0 )
                reset_position = read_pos;
            section_lengths[i] = section_length;
            prg_inf_lengths[i] = prg_inf_length;
            pmt_packet_num[i]  = psi_packet_num;
        }
        /* seek next. */
        mpegts_file_seek( &(info->tsf_ctx), read_pos + check_offset, MPEGTS_SEEK_RESET );
    }
    if( reset_position < 0 )
    {
        free( buffer_data );
        return 1;
    }
    mpegts_file_seek( &(info->tsf_ctx), reset_position, MPEGTS_SEEK_RESET );
    read_pos = reset_position;
    /* select target pmt. */
    int target_pmt = 0;
    struct {
        uint32_t    crc32;
        int         count;
        int         target;
    } target_candidate[PMT_PARSE_COUNT_NUM] = { { 0 } };
    for( int i = 0; i < PMT_PARSE_COUNT_NUM; ++i )
    {
        if( section_lengths[i] == 0 )
            continue;
        int crc_pos = section_lengths[i] - CRC32_SIZE;
        uint32_t crc32 = 0;
        for( int j = 0; j < CRC32_SIZE; ++j )
            crc32 = crc32 << 8 | section_buffers[i][crc_pos + j];
        for( int j = 0; j < PMT_PARSE_COUNT_NUM; ++j )
        {
            if( target_candidate[j].crc32 == 0 )
            {
                target_candidate[j].crc32  = crc32;
                target_candidate[j].count  = 1;
                target_candidate[j].target = i;
                break;
            }
            else if( target_candidate[j].crc32 == crc32 )
            {
                ++ target_candidate[j].count;
                break;
            }
        }
    }
    int detection_max = (info->pmt_target == PMT_TARGET_MAX);
    int target_count  = detection_max ? 0 : PMT_PARSE_COUNT_NUM + 1;
    for( int i = 0; i < PMT_PARSE_COUNT_NUM && target_candidate[i].count; ++i )
    {
        mapi_log( LOG_LV2, "[check] pmt candidate[%d]  crc:%08X  detect_count:%d  target:%d\n"
                         , i, target_candidate[i].crc32, target_candidate[i].count, target_candidate[i].target );
        if( detection_max ? target_count < target_candidate[i].count
                          : target_count > target_candidate[i].count )
        {
            target_count = target_candidate[i].count;
            target_pmt   = target_candidate[i].target;
        }
    }
    mapi_log( LOG_LV2, "[check] target_pmt:%d\n", target_pmt );
    int      prg_inf_length = prg_inf_lengths[target_pmt];
    int      section_length = section_lengths[target_pmt];
    uint8_t *section_buffer = section_buffers[target_pmt];
    int      pid_num_in_pmt = section_pid_num[target_pmt];
    uint8_t  psi_packet_num = pmt_packet_num[target_pmt];
    /* listup. */
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
    psi_ctx->pid_list_num = pid_num_in_pmt;
    psi_ctx->pid_list     = (mpegts_pid_info_t *)malloc( sizeof(mpegts_pid_info_t) * pid_num_in_pmt );
    if( !psi_ctx->pid_list )
        goto fail_parse;
    int32_t pid_list_num = 0, read_count = 0;
    mpeg_descriptor_info_t *descriptor_info = info->descriptor_info;
    if( prg_inf_length )
    {
        /* parse descriptor. */
        uint8_t  *descriptor_data = &(section_buffer[read_count]);
        uint16_t  descriptor_num  = 0;
        if( mpegts_parse_descriptor( descriptor_data, prg_inf_length, descriptor_info, &descriptor_num ) )
            goto fail_parse;
        if( !mpegts_get_ca_pid_from_descriptor( descriptor_info, descriptor_num, &(psi_ctx->ecm_program_id) ) )
            mapi_log( LOG_LV2, "[check] ecm_PID:0x%04X\n", psi_ctx->ecm_program_id );
        read_count += prg_inf_length;
    }
    while( read_count < section_length - CRC32_SIZE )
    {
        uint8_t *section_data        = &(section_buffer[read_count]);
        mpeg_stream_type stream_type =   section_data[0];
        /* reserved     3 bit        =  (section_data[1] & 0xE0) >> 5 */
        uint16_t elementary_PID      = ((section_data[1] & 0x1F) << 8) | section_data[2];
        /* reserved     4 bit        =  (section_data[3] & 0xF0) >> 4 */
        uint16_t ES_info_length      = ((section_data[3] & 0x0F) << 8) | section_data[4];
        mapi_log( LOG_LV2, "[check] stream_type:0x%02X, elementary_PID:0x%04X, ES_info_length:%u\n"
                         , stream_type, elementary_PID, ES_info_length );
        read_count += TS_PACKET_PMT_SECTION_DATA_SIZE;
        /* parse descriptor. */
        uint8_t  *descriptor_data = &(section_buffer[read_count]);
        uint16_t  descriptor_num  = 0;
        if( mpegts_parse_descriptor( descriptor_data, ES_info_length, descriptor_info, &descriptor_num ) )
            goto fail_parse;
        /* setup stream type and PID. */
        psi_ctx->pid_list[pid_list_num].stream_judge    = mpeg_stream_judge_type( stream_type, descriptor_num, descriptor_info );
        psi_ctx->pid_list[pid_list_num].stream_type     = stream_type;
        psi_ctx->pid_list[pid_list_num].program_id      = elementary_PID;
        psi_ctx->pid_list[pid_list_num].reg_stream_type = mpeg_stream_get_registration_stream_type( descriptor_info );
        /* seek next section. */
        read_count += ES_info_length;
        ++pid_list_num;
    }
    uint8_t *crc32 = &(section_buffer[read_count]);
    char crc32_str[CRC32_SIZE * 3 + 1] = { 0 };
    for( int i = 0; i < CRC32_SIZE; ++i )
        sprintf( &(crc32_str[i * 3]), " %02X", crc32[i] );
    mapi_log( LOG_LV2, "[check] CRC32:%s\n", crc32_str );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", read_pos );
    /* setup and prepare buffer. */
    psi_ctx->pmt_program_id = pmt_program_id;
    psi_ctx->pcr_program_id = pcr_program_id;
    psi_ctx->packet_num     = psi_packet_num;
    psi_ctx->packet_buffer  = (uint8_t *)malloc( info->tsf_ctx.packet_size * (psi_packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) );       // info->tsf_ctx.packet_size or TS_PACKET_SIZE
    psi_ctx->section_buffer = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX );
    psi_ctx->cache_buffer   = (uint8_t *)malloc( TS_PACKET_TABLE_SECTION_SIZE_MAX );
    if( !psi_ctx->packet_buffer || !psi_ctx->section_buffer || !psi_ctx->cache_buffer )
    {
        if( psi_ctx->packet_buffer )
        {
            free( psi_ctx->packet_buffer );
            psi_ctx->packet_buffer = NULL;
        }
        if( psi_ctx->section_buffer )
        {
            free( psi_ctx->section_buffer );
            psi_ctx->section_buffer = NULL;
        }
        if( psi_ctx->cache_buffer )
        {
            free( psi_ctx->cache_buffer );
            psi_ctx->cache_buffer = NULL;
        }
        free( psi_ctx->pid_list );
        psi_ctx->pid_list = NULL;
        goto fail_parse;
    }
    /* seek next. */
    mpegts_file_seek( &(info->tsf_ctx), 0, MPEGTS_SEEK_NEXT );
    info->tsf_ctx.sync_byte_position = -1;
    info->tsf_ctx.read_position      = read_pos;
    /* release. */
    free( buffer_data );
    return 0;
fail_parse:
    free( buffer_data );
    return -1;
}

static int mpegts_get_pcr( mpegts_info_t *info, int64_t *pcr, uint32_t pmt_ctx_index )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    *pcr = MPEG_TIMESTAMP_INVALID_VALUE;
    tsp_psi_ctx_t *psi_ctx    = &(info->pmt_ctx[pmt_ctx_index]);
    uint16_t       program_id = psi_ctx->pcr_program_id;
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
        return -1;
    int64_t read_pos = -1;
    /* search. */
    tsp_header_t h;
    do
    {
        if( mpegts_search_program_id_packet( &(info->tsf_ctx), &h, program_id ) )
            return -1;
        show_packet_header_info( &h );
        /* check adaptation field. */
        if( h.adaptation_field_control > 1 )
        {
            uint8_t adaptation_field_size = 0;
            mpegts_file_read( &(info->tsf_ctx), &adaptation_field_size, 1 );
            mapi_log( LOG_LV4, "[check] adpf_size:%d\n", adaptation_field_size );
            if( adaptation_field_size < 7 )
                continue;
            /* check file position. */
            read_pos = info->tsf_ctx.read_position;
            /* get adaptation field data. */
            uint8_t adpf_data[adaptation_field_size];
            mpegts_get_adaptation_field_data( &(info->tsf_ctx), &h, adpf_data, adaptation_field_size );
            /* calculate PCR. */
            if( h.adpf_info.pcr_flag )
            {
                int64_t pcr_value;
                tsp_get_pcr( &(adpf_data[1]), &pcr_value );
                mapi_log( LOG_LV3, "[check] PCR_value:%" PRId64 "\n", pcr_value );
                /* setup. */
                *pcr = pcr_value;
            }
            if( h.adpf_info.opcr_flag )
            {
                int64_t opcr_value;
                tsp_get_pcr( &(adpf_data[7]), &opcr_value );
                mapi_log( LOG_LV3, "[check] OPCR_value:%" PRId64 "\n", opcr_value );
#ifdef NEED_OPCR_VALUE
                /* setup. */
                info->opcr = opcr_value;
#endif
            }
        }
        /* seek next. */
        mpegts_file_seek( &(info->tsf_ctx), 0, MPEGTS_SEEK_NEXT );
    }
    while( *pcr == (int64_t)MPEG_TIMESTAMP_INVALID_VALUE );
    mapi_log( LOG_LV2, "[check] PCR:%" PRId64 " [%" PRId64 "ms]\n", *pcr, *pcr / 90 );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", read_pos );
    /* prepare for next. */
    info->tsf_ctx.read_position = read_pos;
    return 0;
}

static int mpegts_parse_pcr( mpegts_info_t *info, int32_t pmt_ctx_index )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
    int64_t reset_position = mpegts_ftell( &(info->tsf_ctx) );
    /* get start pcr. */
    if( mpegts_get_pcr( info, &(psi_ctx->start_pcr), pmt_ctx_index ) )
        return -1;
    /* get last pcr. */
    int64_t start_position = info->tsf_ctx.read_position;
    int64_t rewind_size    = info->tsf_ctx.packet_size * 200;
    int64_t seek_offset    = info->file_size - start_position;
    seek_offset -= seek_offset % info->tsf_ctx.packet_size;
    do
    {
        int64_t last_pcr = MPEG_TIMESTAMP_INVALID_VALUE;
        /* seek position. */
        seek_offset -= rewind_size;
        if( seek_offset > 0 )
        {
            mpegts_file_seek( &(info->tsf_ctx), start_position + seek_offset, MPEGTS_SEEK_RESET );
            tsp_header_t h;
            if( mpegts_search_program_id_packet( &(info->tsf_ctx), &h, psi_ctx->pcr_program_id ) )
                continue;
            mpegts_file_seek( &(info->tsf_ctx), -(info->tsf_ctx.packet_size), MPEGTS_SEEK_CUR );
        }
        else
        {
            mpegts_file_seek( &(info->tsf_ctx), start_position + info->tsf_ctx.packet_size, MPEGTS_SEEK_RESET );
            psi_ctx->last_pcr = psi_ctx->start_pcr;
        }
        /* search last pcr. */
        while( !mpegts_get_pcr( info, &last_pcr, pmt_ctx_index ) )
            psi_ctx->last_pcr = last_pcr;
    }
    while( psi_ctx->last_pcr == (int64_t)MPEG_TIMESTAMP_INVALID_VALUE );
    /* reset position. */
    mpegts_file_seek( &(info->tsf_ctx), reset_position, MPEGTS_SEEK_RESET );
    mapi_log( LOG_LV2, "[check] start PCR:%" PRId64 " [%" PRId64 "ms]\n", psi_ctx->start_pcr, psi_ctx->start_pcr / 90 );
    mapi_log( LOG_LV2, "[check]  last PCR:%" PRId64 " [%" PRId64 "ms]\n", psi_ctx->last_pcr, psi_ctx->last_pcr / 90 );
    return 0;
}

static uint16_t mpegts_get_program_id( mpegts_info_t *info, mpeg_sample_type sample_type, uint8_t stream_number, uint16_t service_id )
{
    mapi_log( LOG_LV3, "[check] %s()\n", __func__ );
    int32_t pmt_ctx_index = info->pmt_ctx_index;
    /* search service id. */
    if( service_id )
    {
        for( pmt_ctx_index = 0; pmt_ctx_index < info->pat_ctx.pid_list_num; ++pmt_ctx_index )
            if( info->pat_ctx.pid_list[pmt_ctx_index].program_number == service_id )
                break;
        if( pmt_ctx_index == info->pat_ctx.pid_list_num )
            pmt_ctx_index = info->pmt_ctx_index;
    }
    /* search program id. */
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
    tss_ctx_t     *stream  = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
        stream = &(psi_ctx->video_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
        stream = &(psi_ctx->audio_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
        stream = &(psi_ctx->caption_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
        stream = &(psi_ctx->dsmcc_stream[stream_number]);
    else
        return TS_PID_ERR;
    return stream->program_id;
}

static int mpegts_get_stream_timestamp
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    mpeg_pes_stream_id_type     stream_id_type,
    mpeg_timestamp_t           *timestamp
)
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    tsp_header_t h;
    int64_t read_pos = -1;
    /* search packet data. */
    int64_t pts = MPEG_TIMESTAMP_INVALID_VALUE, dts = MPEG_TIMESTAMP_INVALID_VALUE;
    do
    {
        /* seek payload data. */
        int ret = mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_IS_OFF );
        if( ret < 0 )
            return -1;
        if( ret > 0 )
        {
            /* seek next. */
            mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* check file position. */
        read_pos = tsf_ctx->read_position;
        /* check PES Packet Start Code. */
        uint8_t pes_packet_head_data[PES_PACKET_START_CODE_SIZE];
        mpegts_file_read( tsf_ctx, pes_packet_head_data, PES_PACKET_START_CODE_SIZE );
        if( mpeg_pes_check_steam_id_type( pes_packet_head_data, stream_id_type ) )
        {
            /* seek next. */
            mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* check PES packet length, flags. */
        int read_size = PES_PACKET_HEADER_CHECK_SIZE + PES_PACKET_PTS_DTS_DATA_SIZE;
        uint8_t pes_header_check_buffer[read_size];
        mpegts_file_read( tsf_ctx, pes_header_check_buffer, read_size );
        mpeg_pes_header_info_t pes_info;
        mpeg_pes_get_header_info( pes_header_check_buffer, &pes_info );
        mapi_log( LOG_LV3, "[check] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                         , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        if( !pes_info.pts_flag )
        {
            /* seek next. */
            mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        /* get PTS and DTS value. */
        uint8_t *pes_packet_pts_dts_data = &(pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE]);
        pts = pes_info.pts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[0]) ) : (int64_t)MPEG_TIMESTAMP_INVALID_VALUE;
        dts = pes_info.dts_flag ? mpeg_pes_get_timestamp( &(pes_packet_pts_dts_data[5]) ) : pts;
        mapi_log( LOG_LV2, "[check] PTS:%" PRId64 " DTS:%" PRId64 "\n", pts, dts );
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    while( pts == (int64_t)MPEG_TIMESTAMP_INVALID_VALUE );
    /* setup. */
    timestamp->pts = pts;
    timestamp->dts = dts;
    /* prepare for next. */
    mpegts_file_seek( tsf_ctx, read_pos, MPEGTS_SEEK_RESET );       /* reset start position of detect packet. */
    tsf_ctx->read_position      = read_pos;
    tsf_ctx->sync_byte_position = 0;
    return 0;
}

#define GET_PES_PACKET_HEADER( _ctx, _pes )                                             \
do {                                                                                    \
    uint8_t pes_header_check_buffer[PES_PACKET_HEADER_CHECK_SIZE];                      \
    /* skip PES Packet Start Code. */                                                   \
    mpegts_file_seek( _ctx, PES_PACKET_START_CODE_SIZE, MPEGTS_SEEK_CUR );              \
    /* get PES packet length, flags. */                                                 \
    mpegts_file_read( _ctx, pes_header_check_buffer, PES_PACKET_HEADER_CHECK_SIZE );    \
    mpeg_pes_get_header_info( pes_header_check_buffer, &_pes );                         \
} while( 0 )

static inline int read_1byte( tsf_ctx_t *tsf_ctx, tsp_header_t *h, uint16_t program_id, uint8_t *buffer )
{
    if( !tsf_ctx->ts_packet_length )
    {
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
        if( mpegts_seek_packet_payload_data( tsf_ctx, h, program_id, INDICATOR_IS_ON ) )
            return -1;
    }
    mpegts_file_read( tsf_ctx, buffer, 1 );
    return 0;
}

static int mpegts_read_mpeg_video_picutre_info
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    mpeg_video_info_t          *video_info,
    int64_t                    *gop_number,
    uint8_t                    *mpeg_video_head_data
)
{
    tsp_header_t h;
    int64_t reset_position        = mpegts_ftell( tsf_ctx );
    int32_t rest_ts_packet_length = tsf_ctx->ts_packet_length;
    /* check identifier. */
    uint8_t identifier;
    if( read_1byte( tsf_ctx, &h, program_id, &identifier ) )
        return -1;
    mpegts_file_seek( tsf_ctx, reset_position, MPEGTS_SEEK_SET );
    tsf_ctx->ts_packet_length = rest_ts_packet_length;
    /* check Start Code. */
    mpeg_video_start_code_info_t start_code_info;
    if( mpeg_video_judge_start_code( mpeg_video_head_data, identifier, &start_code_info ) )
        return -2;
    uint32_t read_size = start_code_info.read_size;
    /* get header/extension information. */
    uint8_t buf[read_size];
    uint8_t *buf_p = buf;
    while( tsf_ctx->ts_packet_length < (int32_t)read_size )
    {
        if( tsf_ctx->ts_packet_length )
        {
            int32_t read = tsf_ctx->ts_packet_length;
            mpegts_file_read( tsf_ctx, buf_p, read );
            buf_p += read;
            read_size -= read;
        }
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
            return -1;
    }
    mpegts_file_read( tsf_ctx, buf_p, read_size );
    int32_t check_size = mpeg_video_get_header_info( buf, start_code_info.start_code, video_info );
    if( check_size < (int32_t)start_code_info.read_size )
    {
        /* reset position. */
        mpegts_file_seek( tsf_ctx, reset_position, MPEGTS_SEEK_SET );
        tsf_ctx->ts_packet_length = rest_ts_packet_length;
        int64_t seek_size = check_size;
        while( tsf_ctx->ts_packet_length < seek_size )
        {
            seek_size -= tsf_ctx->ts_packet_length;
            mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
                return -1;
        }
        mpegts_file_seek( tsf_ctx, seek_size, MPEGTS_SEEK_CUR );
    }
    /* debug. */
    mpeg_video_debug_header_info( video_info, start_code_info.searching_status );
    /* check the status detection. */
    if( start_code_info.searching_status == DETECT_GSC )
        ++(*gop_number);
    else if( start_code_info.searching_status == DETECT_PSC )
        return 2;
    else if( start_code_info.searching_status == DETECT_SSC
          || start_code_info.searching_status == DETECT_SEC )
        return 1;
    return 0;
}

static int mpegts_get_mpeg_video_picture_info
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    mpeg_video_info_t          *video_info,
    int64_t                    *gop_number
)
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    int result = -1;
    /* parse payload data. */
    tsp_header_t h;
    uint8_t mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE];
    memset( mpeg_video_head_data, MPEG_VIDEO_START_CODE_ALL_CLEAR_VALUE, MPEG_VIDEO_START_CODE_SIZE );
    int no_exist_start_indicator = 1;
    do
    {
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
            return -1;
        /* check start indicator. */
        if( no_exist_start_indicator && !h.payload_unit_start_indicator )
        {
            mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            continue;
        }
        if( h.payload_unit_start_indicator )
        {
            /* check PES packet length, flags. */
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
            mapi_log( LOG_LV3, "[debug] PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                             , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
            mpegts_file_seek( tsf_ctx, pes_info.header_length, MPEGTS_SEEK_CUR );
            no_exist_start_indicator = 0;
        }
        /* search Start Code. */
        while( tsf_ctx->ts_packet_length )
        {
            mpegts_file_read( tsf_ctx, &(mpeg_video_head_data[MPEG_VIDEO_START_CODE_SIZE - 1]), 1 );
            /* check Start Code. */
            if( mpeg_video_check_start_code_common_head( mpeg_video_head_data ) )
            {
                BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE );
                continue;
            }
            /* get header information. */
            int read_reslut = mpegts_read_mpeg_video_picutre_info( tsf_ctx, program_id, video_info, gop_number, mpeg_video_head_data );
            if( read_reslut == -2 )
            {
                BYTE_DATA_SHIFT( mpeg_video_head_data, MPEG_VIDEO_START_CODE_SIZE );
                continue;
            }
            else if( read_reslut == 2 )
                result = 0;
            else if( read_reslut < 0 )
                return -1;
            else if( read_reslut )
                goto end_get_video_picture_info;
            /* cleanup buffer. */
            memset( mpeg_video_head_data, MPEG_VIDEO_START_CODE_ALL_CLEAR_VALUE, MPEG_VIDEO_START_CODE_SIZE );
        }
        mapi_log( LOG_LV4, "[debug] continue next packet. buf:0x%02X 0x%02X 0x%02X 0x--\n"
                         , mpeg_video_head_data[0], mpeg_video_head_data[1], mpeg_video_head_data[2] );
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
end_get_video_picture_info:
    return result;
}

static uint32_t mpegts_get_sample_packets_num( tsf_ctx_t *tsf_ctx, uint16_t program_id, mpeg_stream_type stream_type )
{
#if ENABLE_SUPPRESS_WARNINGS
    (void) stream_type;
#endif
    mapi_log( LOG_LV3, "[debug] %s()\n", __func__ );
    tsp_header_t h;
    /* skip first packet. */
    if( mpegts_search_program_id_packet( tsf_ctx, &h, program_id ) )
        return 0;
    mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    /* count. */
    uint32_t ts_packet_count        = 0;
    int8_t   old_continuity_counter = h.continuity_counter;
    do
    {
        ++ts_packet_count;
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
            break;
        if( h.continuity_counter != ((old_continuity_counter + 1) & 0x0F) && h.adpf_info.discontinuity_indicator == 0 )
            mapi_log( LOG_LV3, "[debug] detect Drop!  ts_packet_count:%u  continuity_counter:%u --> %u\n"
                             , ts_packet_count, old_continuity_counter, h.continuity_counter );
        old_continuity_counter = h.continuity_counter;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
            /* check PES packet header. */
            if( pes_info.pts_flag )
            {
                /* set next start position. */
                mpegts_file_seek( tsf_ctx, tsf_ctx->ts_packet_length - TS_PACKET_SIZE, MPEGTS_SEEK_CUR );
                break;
            }
            mapi_log( LOG_LV3, "[debug] Detect PES packet doesn't have PTS.\n"
                               "        PES packet_len:%d, pts_flag:%d, dts_flag:%d, header_len:%d\n"
                             , pes_info.packet_length, pes_info.pts_flag, pes_info.dts_flag, pes_info.header_length );
        }
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    while( 1 );
    mapi_log( LOG_LV3, "[debug] ts_packet_count:%u\n", ts_packet_count );
    return ts_packet_count;
}

static int mpegts_skip_pes_header
(
    tsf_ctx_t                  *tsf_ctx,
    tsp_header_t               *h,
    uint16_t                    program_id,
    mpeg_pes_header_info_t     *pes_info
)
{
    uint8_t skip_header_size = pes_info->header_length;
    /* skip PES packet header. */
    while( skip_header_size )
    {
        if( skip_header_size < tsf_ctx->ts_packet_length )
        {
            mpegts_file_seek( tsf_ctx, skip_header_size, MPEGTS_SEEK_CUR );
            break;
        }
        skip_header_size -= tsf_ctx->ts_packet_length;
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
        /* seek next packet. */
        if( mpegts_seek_packet_payload_data( tsf_ctx, h, program_id, INDICATOR_UNCHECKED ) )
            return -1;
        if( h->payload_unit_start_indicator )
        {
            GET_PES_PACKET_HEADER( tsf_ctx, (*pes_info) );
            skip_header_size = pes_info->header_length;
        }
    }
    return 0;
}

static int mpegts_check_sample_raw_frame_length
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    mpeg_stream_type            stream_type,
    mpeg_stream_group_type      stream_judge,
    uint32_t                    frame_length,
    uint8_t                    *cache_buffer,
    int32_t                     cache_read_size
)
{
    mapi_log( LOG_LV4, "[debug] %s()\n", __func__ );
    int     result                   = -1;
    int64_t start_position           = mpegts_ftell( tsf_ctx );
    int32_t start_rest_packet_length = tsf_ctx->ts_packet_length;
    /* check. */
    int32_t stream_header_check_size = mpeg_stream_get_header_check_size( stream_type, stream_judge );
    uint8_t check_buffer[stream_header_check_size];
    int32_t buffer_read_size = 0;
    int32_t raw_data_size    = cache_read_size;
    if( raw_data_size >= (int32_t)frame_length )
    {
        buffer_read_size = raw_data_size - frame_length;
        if( buffer_read_size > stream_header_check_size )
            buffer_read_size = stream_header_check_size;
        memcpy( check_buffer, cache_buffer + frame_length, buffer_read_size );
    }
    tsp_header_t h;
    while( 1 )
    {
        int32_t read_size = 0;
        int32_t seek_size = 0;
        if( raw_data_size + tsf_ctx->ts_packet_length > (int32_t)frame_length )
        {
            if( raw_data_size < (int32_t)frame_length )
            {
                seek_size = frame_length - raw_data_size;
                mpegts_file_seek( tsf_ctx, seek_size, MPEGTS_SEEK_CUR );
            }
            read_size = stream_header_check_size - buffer_read_size;
            if( read_size > tsf_ctx->ts_packet_length )
                read_size = tsf_ctx->ts_packet_length;
            if( read_size )
            {
                mpegts_file_read( tsf_ctx, check_buffer + buffer_read_size, read_size );
                buffer_read_size += read_size;
            }
            if( buffer_read_size == stream_header_check_size )
            {
                if( mpeg_stream_check_header( stream_type, stream_judge, -1, check_buffer, stream_header_check_size, NULL, NULL ) >= 0 )
                    result = 0;
                break;
            }
        }
        raw_data_size += tsf_ctx->ts_packet_length + read_size + seek_size;
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
        /* seek next packet. */
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
        {
        not_detected:
            if( raw_data_size >= (int32_t)frame_length )
                result = 0;
            goto end_check_frame_length;
        }
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
            /* skip PES packet header. */
            if( mpegts_skip_pes_header( tsf_ctx, &h, program_id, &pes_info ) )
                goto not_detected;
        }
    }
end_check_frame_length:
    mpegts_file_seek( tsf_ctx, start_position, MPEGTS_SEEK_SET );
    tsf_ctx->ts_packet_length = start_rest_packet_length;
    mapi_log( LOG_LV4, "[debug] check_frame_length  result: %d\n", result );
    return result;
}

typedef struct {
    uint32_t                data_size;
    int32_t                 read_offset;
    mpeg_stream_raw_info_t  stream_raw_info;
} sample_raw_data_info_t;

#define RESET_BUFFER_STATUS()                   \
do {                                            \
    int32_t offset = header_offset + 1;         \
    buffer_read_size += offset;                 \
    buffer_p         += offset;                 \
    buffer_read_offset = cache_read_size - 1;   \
} while( 0 )
static int mpegts_get_sample_raw_data_info
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    mpeg_stream_type            stream_type,
    mpeg_stream_group_type      stream_judge,
    sample_raw_data_info_t     *raw_data_info
)
{
    mapi_log( LOG_LV3, "[debug] %s()\n", __func__ );
    uint32_t raw_data_size = 0;
    int32_t  start_point   = -1;
    /* keep position information. */
    int64_t start_position = mpegts_ftell( tsf_ctx );
    /* search. */
    int     check_start_point        = 0;
    int32_t stream_header_check_size = mpeg_stream_get_header_check_size( stream_type, stream_judge );
    uint8_t check_buffer[TS_PACKET_SIZE + stream_header_check_size];
    uint8_t  *buffer_p           = check_buffer;
    int32_t   buffer_read_offset = 0;
    int32_t   buffer_read_size   = 0;
    uint32_t  frame_length       = 0;
    mpeg_stream_raw_info_t stream_raw_info;
    tsp_header_t h;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
        {
        not_detected:
            if( start_point >= 0 )
            {
                /* This sample is the data at the end was shaved. */
                raw_data_size += buffer_read_size + buffer_read_offset;
                if( raw_data_info->stream_raw_info.frame_length )
                    raw_data_size = raw_data_info->stream_raw_info.frame_length;
            }
            else
                start_point = 0;
            mapi_log( LOG_LV3, "[debug] read file end.\n" );
            goto end_get_info;
        }
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
            /* skip PES packet header. */
            if( mpegts_skip_pes_header( tsf_ctx, &h, program_id, &pes_info ) )
                goto not_detected;
            /* check PES packet header. */
            if( pes_info.pts_flag )
                check_start_point = 1;
        }
    retry_check_start_point:
        if( check_start_point )
        {
            /* buffering check data . */
            int32_t buffer_size = tsf_ctx->ts_packet_length + buffer_read_offset;
            if( tsf_ctx->ts_packet_length )
            {
                mpegts_file_read( tsf_ctx, check_buffer + buffer_read_offset, tsf_ctx->ts_packet_length );
                buffer_p = check_buffer;
            }
            int32_t data_offset;
            int32_t header_offset = mpeg_stream_check_header( stream_type, stream_judge, !(start_point < 0), buffer_p, buffer_size
                                                            , (start_point < 0) ? &(raw_data_info->stream_raw_info) : &stream_raw_info
                                                            , &data_offset );
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
                    frame_length = raw_data_info->stream_raw_info.frame_length;
                    int32_t cache_read_size = buffer_size - header_offset - data_offset;
                    /* check frame length. */
                    if( frame_length
                     && mpegts_check_sample_raw_frame_length( tsf_ctx, program_id, stream_type, stream_judge, frame_length
                                                            , buffer_p + header_offset + data_offset, cache_read_size ) )
                    {
                        /* retry. */
                        RESET_BUFFER_STATUS();
                        start_point = -1;
                        goto retry_check_start_point;
                    }
                    /* detect start point. */
                    start_point = buffer_read_size + header_offset;
                    raw_data_size = cache_read_size;
                }
                else
                {
                    frame_length = stream_raw_info.frame_length;
                    int32_t cache_read_size = buffer_size - header_offset - data_offset;
                    /* check frame length. */
                    if( frame_length
                     && mpegts_check_sample_raw_frame_length( tsf_ctx, program_id, stream_type, stream_judge, frame_length
                                                            , buffer_p + header_offset + data_offset, cache_read_size ) )
                    {
                        /* retry. */
                        RESET_BUFFER_STATUS();
                        goto retry_check_start_point;
                    }
                    raw_data_size += buffer_read_size + header_offset;
                    /* detect end point(=next start point). */
                    mpegts_file_seek( tsf_ctx, tsf_ctx->packet_size - TS_PACKET_SIZE, MPEGTS_SEEK_CUR );
                    break;
                }
                buffer_read_size = 0;
                check_start_point = 0;
            }
        }
        else
            raw_data_size += tsf_ctx->ts_packet_length;
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
end_get_info:
    mpegts_file_seek( tsf_ctx, start_position, MPEGTS_SEEK_RESET );
    /* setup. */
    raw_data_info->data_size   = raw_data_size;
    raw_data_info->read_offset = start_point;
    return 0;
}
#undef RESET_BUFFER_STATUS

static void mpegts_get_sample_raw_data
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
 /* mpeg_stream_type            stream_type,
    mpeg_stream_group_type      stream_judge, */
    uint32_t                    raw_data_size,
    int32_t                     read_offset,
    uint8_t                   **buffer,
    uint32_t                   *read_size
)
{
    mapi_log( LOG_LV3, "[check] %s()\n", __func__ );
    if( !raw_data_size )
        return;
    /* allocate buffer. */
    *buffer = (uint8_t *)malloc( raw_data_size );
    if( !(*buffer) )
        return;
    mapi_log( LOG_LV3, "[debug] buffer_size:%u\n", raw_data_size );
    /* read. */
    tsp_header_t h;
    *read_size = 0;
    while( 1 )
    {
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
            break;
        if( h.payload_unit_start_indicator )
        {
            mpeg_pes_header_info_t pes_info;
            GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
            /* skip PES packet header. */
            if( mpegts_skip_pes_header( tsf_ctx, &h, program_id, &pes_info ) )
                break;
        }
        /* check read start point. */
        if( read_offset )
        {
            if( read_offset > tsf_ctx->ts_packet_length )
            {
                read_offset -= tsf_ctx->ts_packet_length;
                mpegts_file_seek( tsf_ctx, tsf_ctx->ts_packet_length, MPEGTS_SEEK_CUR );
            }
            else
            {
                mpegts_file_seek( tsf_ctx, read_offset, MPEGTS_SEEK_CUR );
                read_offset = 0;
            }
        }
        /* read raw data. */
        if( tsf_ctx->ts_packet_length > 0 )
        {
            if( raw_data_size > *read_size + tsf_ctx->ts_packet_length )
            {
                int32_t read = tsf_ctx->ts_packet_length;
                mpegts_file_read( tsf_ctx, *buffer + *read_size, read );
                *read_size += read;
            }
            else
            {
                mpegts_file_read( tsf_ctx, *buffer + *read_size, raw_data_size - *read_size );
                *read_size = raw_data_size;
                mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
                break;
            }
        }
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
    return;
}

static void mpegts_get_sample_pes_packet_data
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    uint32_t                    ts_packet_count,
    uint8_t                   **buffer,
    uint32_t                   *read_size
)
{
    mapi_log( LOG_LV3, "[check] %s()\n", __func__ );
    /* allocate buffer. */
    uint32_t sample_size = ts_packet_count * TS_PACKET_SIZE;
    *buffer = (uint8_t *)malloc( sample_size );
    if( !(*buffer) )
        return;
    mapi_log( LOG_LV3, "[debug] buffer_size:%u\n", sample_size );
    /* read. */
    tsp_header_t h;
    *read_size = 0;
    for( uint32_t i = 0; i < ts_packet_count; ++i )
    {
        if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
            return;
        /* read packet data. */
        if( tsf_ctx->ts_packet_length > 0 )
        {
            int32_t read = tsf_ctx->ts_packet_length;
            mpegts_file_read( tsf_ctx, *buffer + *read_size, read );
            *read_size += read;
        }
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    }
}

static void mpegts_get_sample_ts_packet_data
(
    tsf_ctx_t                  *tsf_ctx,
    uint16_t                    program_id,
    uint32_t                    ts_packet_count,
    uint8_t                   **buffer,
    uint32_t                   *read_size
)
{
    mapi_log( LOG_LV3, "[check] %s()\n", __func__ );
    /* allocate buffer. */
    uint32_t sample_size = ts_packet_count * TS_PACKET_SIZE;
    *buffer = (uint8_t *)malloc( sample_size );
    if( !(*buffer) )
        return;
    mapi_log( LOG_LV3, "[debug] buffer_size:%u\n", sample_size );
    /* read. */
    tsp_header_t h;
    *read_size = 0;
    for( uint32_t i = 0; i < ts_packet_count; ++i )
    {
        /* read packet data. */
        mpegts_file_read( tsf_ctx, *buffer + *read_size, TS_PACKET_SIZE );
        *read_size += TS_PACKET_SIZE;
        /* seek next packet. */
        if( mpegts_search_program_id_packet( tsf_ctx, &h, program_id ) )
            break;
        mpegts_file_seek( tsf_ctx, -(TS_PACKET_HEADER_SIZE), MPEGTS_SEEK_CUR );
    }
}

static int mpegts_malloc_stream_parse_ctx
(
 /* mpeg_stream_type            stream_type, */
    mpeg_stream_group_type      stream_judge,
    void                      **stream_parse_info
)
{
    *stream_parse_info = NULL;
    if( (stream_judge & STREAM_IS_MPEG_VIDEO) == STREAM_IS_MPEG_VIDEO )
    {
        void *ctx = malloc( sizeof(mpeg_video_info_t) );
        if( !ctx )
            return -1;
        *stream_parse_info = ctx;
    }
    return 0;
}

static tsp_psi_ctx_t *mpegts_get_pmt_ctx( mpegts_info_t *info, uint16_t program_id )
{
    if( info->pmt_ctx_index < info->pat_ctx.pid_list_num )
    {
        if( program_id == info->pmt_ctx[info->pmt_ctx_index].pmt_program_id )
            return &(info->pmt_ctx[info->pmt_ctx_index]);
    }
    else
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
        {
            if( info->pat_ctx.pid_list[i].disable_pid )
                continue;
            if( program_id == info->pmt_ctx[i].pmt_program_id )
                return &(info->pmt_ctx[i]);
        }
    return NULL;
}

static tsp_psi_ctx_t *mpegts_get_pcr_ctx( mpegts_info_t *info, uint16_t program_id )
{
    if( info->pmt_ctx_index < info->pat_ctx.pid_list_num )
    {
        if( program_id == info->pmt_ctx[info->pmt_ctx_index].pcr_program_id )
            return &(info->pmt_ctx[info->pmt_ctx_index]);
    }
    else
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
        {
            if( info->pat_ctx.pid_list[i].disable_pid )
                continue;
            if( program_id == info->pmt_ctx[i].pcr_program_id )
                return &(info->pmt_ctx[i]);
        }
    return NULL;
}

static tsp_psi_ctx_t *mpegts_get_ecm_ctx( mpegts_info_t *info, uint16_t program_id )
{
    if( info->pmt_ctx_index < info->pat_ctx.pid_list_num )
    {
        if( program_id == info->pmt_ctx[info->pmt_ctx_index].ecm_program_id )
            return &(info->pmt_ctx[info->pmt_ctx_index]);
    }
    else
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
        {
            if( info->pat_ctx.pid_list[i].disable_pid )
                continue;
            if( program_id == info->pmt_ctx[i].ecm_program_id )
                return &(info->pmt_ctx[i]);
        }
    return NULL;
}

static inline uint16_t get_output_stream_nums( output_stream_type output, uint8_t v_num, uint8_t a_num, uint8_t c_num, uint8_t d_num )
{
    return ((output & OUTPUT_STREAM_VIDEO  ) ? v_num : 0)
         + ((output & OUTPUT_STREAM_AUDIO  ) ? a_num : 0)
         + ((output & OUTPUT_STREAM_CAPTION) ? c_num : 0)
         + ((output & OUTPUT_STREAM_DSMCC  ) ? d_num : 0);
}

static int mpegts_update_psi
(
    mpegts_info_t              *info,
    uint16_t                    program_id,
    tsp_psi_ctx_t              *psi_ctx,
    output_stream_type          output_stream
)
{
    /* parse ts packets. */
    int      read_pos       = 0;
    uint8_t *section_header = NULL;
    /* headers */
    tsp_header_t h;
    while( read_pos < psi_ctx->packet_cache_size )
    {
        /* setup header data. */
        uint8_t *ts_header = &(psi_ctx->packet_buffer[read_pos]);
        tsp_parse_header( ts_header, &h );
        read_pos += TS_PACKET_HEADER_SIZE;
        if( h.payload_unit_start_indicator )
        {
            /* check adaptation field. */
            if( h.adaptation_field_control > 1 )
            {
                uint8_t adaptation_field_size = psi_ctx->packet_buffer[read_pos];
                read_pos += 1;
                if( adaptation_field_size )
                    read_pos += adaptation_field_size;
            }
            /* check pointer field. */
            uint8_t pointer_field = psi_ctx->packet_buffer[read_pos];
            read_pos += 1;
            if( pointer_field )
                read_pos += pointer_field;
            /* set section header. */
            section_header = &(psi_ctx->packet_buffer[read_pos]);
            break;
        }
    }
    if( !section_header )
        return -1;
    /* parse section header. */
    int section_start         = read_pos;
    int section_length        = 0;
    int prg_inf_length        = 0;
    int section_header_length = 0;
    if( program_id == TS_PID_PAT )
    {
        /* PAT */
        /* setup header data. */
        tsp_pat_si_t pat_si;
        tsp_parse_pat_header( section_header, &pat_si );
        /* get section length. */
        section_length        = pat_si.section_length;
        section_header_length = TS_PID_PAT_SECTION_HEADER_SIZE;
    }
    else if( program_id == TS_PID_CAT )
    {
        /* CAT */
        /* setup header data. */
        tsp_cat_si_t cat_si;
        tsp_parse_cat_header( section_header, &cat_si );
        /* get section length. */
        section_length        = cat_si.section_length;
        section_header_length = TS_PID_CAT_SECTION_HEADER_SIZE;
    }
    else if( program_id == psi_ctx->pmt_program_id )
    {
        /* PMT */
        /* setup header data. */
        tsp_pmt_si_t pmt_si;
        tsp_parse_pmt_header( section_header, &pmt_si );
        /* get section length. */
        section_length        = pmt_si.section_length;
        prg_inf_length        = pmt_si.program_info_length;
        section_header_length = TS_PID_PMT_SECTION_HEADER_SIZE;
    }
    int section_total = section_length + 3;     /* 3: section_header[0]-[2] */
    /* check payload total size. */
    int read_count = 0;
    for( int i = 1; read_pos < psi_ctx->packet_cache_size; ++i )
    {
        int payload_size = TS_PACKET_SIZE * i - read_pos;
        read_count += payload_size;
        read_pos   += payload_size;
        /* seek next packet payload data. */
        read_pos += TS_PACKET_HEADER_SIZE;
    }
    if( read_count < section_total )
        return -1;
    /* check if update is necessary. */
    int32_t index_start = 0;
    int32_t output_num  = 1;
    if( program_id == TS_PID_PAT )
    {
        /* PAT */
        if( psi_ctx->pid_list_num == 1 )
            return 1;
        if( info->pmt_ctx_index == psi_ctx->pid_list_num )
        {
          //index_start = 0;
            output_num  = psi_ctx->pid_list_num;
        }
        else
        {
            index_start = info->pmt_ctx_index;
          //output_num  = 1;
        }
    }
    else if( program_id == TS_PID_CAT )
    {
        /* CAT */
        return 1;
    }
    else if( program_id == psi_ctx->pmt_program_id )
    {
        /* PMT */
        uint16_t total_stream_num  = psi_ctx->video_stream_num + psi_ctx->audio_stream_num + psi_ctx->caption_stream_num + psi_ctx->dsmcc_stream_num;
        uint16_t output_stream_num = get_output_stream_nums( output_stream, psi_ctx->video_stream_num, psi_ctx->audio_stream_num, psi_ctx->caption_stream_num, psi_ctx->dsmcc_stream_num );
        if( total_stream_num == output_stream_num )
            return 1;
    }
    else
        return 0;
    int32_t index_max = index_start + output_num;
    /* buffering section data. */
  //uint8_t section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX];
    uint8_t *section_buffer = psi_ctx->cache_buffer;
    memcpy( section_buffer, section_header, section_header_length );
    read_count = section_header_length;
    read_pos   = section_start + section_header_length;
    for( int i = 1; read_pos < psi_ctx->packet_cache_size; ++i )
    {
        int payload_size = TS_PACKET_SIZE * i - read_pos;
        int rest_size    = section_total - read_count;
        if( payload_size > rest_size )
        {
            memcpy( &(section_buffer[read_count]), &(psi_ctx->packet_buffer[read_pos]), rest_size );
            read_count += rest_size;
            break;
        }
        memcpy( &(section_buffer[read_count]), &(psi_ctx->packet_buffer[read_pos]), payload_size );
        read_count += payload_size;
        read_pos   += payload_size;
        /* seek next packet payload data. */
        read_pos += TS_PACKET_HEADER_SIZE;
    }
#if 0
    if( read_count < section_total )
        return -1;
#endif
    /* check CRC32. */
    int crc32_pos = section_total - CRC32_SIZE;
    if( memcmp( psi_ctx->origin_crc32, &(section_buffer[crc32_pos]), CRC32_SIZE ) )
    {
        /* copy section information. */
        int write_count = section_header_length + prg_inf_length;
        for( int32_t i = index_start; i < index_max; ++i )
        {
            uint8_t *write_buffer = &(psi_ctx->section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX * i]);
            memcpy( write_buffer, section_buffer, write_count );
        }
        read_count = write_count;
        /* update section data. */
        if( program_id == TS_PID_PAT )
        {
            /* PAT */
            int r_counts[index_max];
            int w_counts[index_max];
            for( int32_t i = index_start; i < index_max; ++i )
            {
                uint8_t *write_buffer = &(psi_ctx->section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX * i]);
                r_counts[i] = read_count;
                w_counts[i] = write_count;
                while( r_counts[i] < section_total - CRC32_SIZE )
                {
                    uint8_t *section_data   = &(section_buffer[r_counts[i]]);
                    uint16_t program_number =  (section_data[0] << 8) | section_data[1];
                    uint16_t pmt_program_id = ((section_data[2] & 0x1F) << 8) | section_data[3];
                    if( program_number == 0 || pmt_program_id == info->pmt_ctx[i].pmt_program_id )
                    {
                        memcpy( &(write_buffer[w_counts[i]]), &(section_buffer[r_counts[i]]), TS_PACKET_PAT_SECTION_DATA_SIZE );
                        w_counts[i] += TS_PACKET_PAT_SECTION_DATA_SIZE;
                    }
                    /* seek next section. */
                    r_counts[i] += TS_PACKET_PAT_SECTION_DATA_SIZE;
                }
            }
            read_count  = r_counts[index_start];
            write_count = w_counts[index_start];
        }
#if 0
        else if( program_id == TS_PID_CAT )
        {
            /* CAT */
            // Not supported.
        }
#endif
        else if( program_id == psi_ctx->pmt_program_id )
        {
            /* PMT */
            while( read_count < section_total - CRC32_SIZE )
            {
                uint8_t *section_data        = &(section_buffer[read_count]);
              //mpeg_stream_type stream_type =   section_data[0];
                uint16_t elementary_PID      = ((section_data[1] & 0x1F) << 8) | section_data[2];
                uint16_t ES_info_length      = ((section_data[3] & 0x0F) << 8) | section_data[4];
                /* check output stream. */
                int     output         = 0;
                int32_t pid_list_index = 0;
                while( pid_list_index < psi_ctx->pid_list_num )
                {
                    if( elementary_PID == psi_ctx->pid_list[pid_list_index].program_id )
                        break;
                     ++pid_list_index;
                }
                if( pid_list_index < psi_ctx->pid_list_num )
                {
                    mpeg_stream_group_type stream_judge = psi_ctx->pid_list[pid_list_index].stream_judge;
                    if( stream_judge & STREAM_IS_VIDEO && output_stream & OUTPUT_STREAM_VIDEO )
                        output = 1;
                    else if( stream_judge & STREAM_IS_AUDIO && output_stream & OUTPUT_STREAM_AUDIO )
                        output = 1;
                    else if( stream_judge & STREAM_IS_CAPTION && output_stream & OUTPUT_STREAM_CAPTION )
                        output = 1;
                    else if( stream_judge & STREAM_IS_DSMCC && output_stream & OUTPUT_STREAM_DSMCC )
                        output = 1;
                }
                if( output )
                {
                    memcpy( &(psi_ctx->section_buffer[write_count]), &(section_buffer[read_count]), TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length );
                    write_count += TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length;
                }
                /* seek next section. */
                read_count += TS_PACKET_PMT_SECTION_DATA_SIZE + ES_info_length;
            }
        }
        /* update section information. */
        psi_ctx->section_length = write_count - 3 + CRC32_SIZE;
        for( int32_t i = index_start; i < index_max; ++i )
        {
            uint8_t *write_buffer = &(psi_ctx->section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX * i]);
            /* section length. */
            write_buffer[1] = (section_buffer[1] & 0xF0) | psi_ctx->section_length >> 8;
            write_buffer[2] =  psi_ctx->section_length & 0xFF;
            /* CRC32. */
            uint32_t new_crc = calc_crc32( write_buffer, write_count );
            for( int i = 0; i < CRC32_SIZE; ++i )
                write_buffer[write_count + i] = (new_crc >> (32 - 8 * (i + 1))) & 0xFF;
        }
        /* save original CRC32 for update check. */
        memcpy( psi_ctx->origin_crc32, &(section_buffer[crc32_pos]), CRC32_SIZE );
    }
    /* update ts packets. */
    section_total = psi_ctx->section_length + 3;        /* 3: section_header[0]-[2] */
    for( int32_t i = index_start; i < index_max; ++i )
    {
        uint8_t *read_buffer  = &(psi_ctx->section_buffer[TS_PACKET_TABLE_SECTION_SIZE_MAX * i]);
        uint8_t *write_buffer = &(psi_ctx->packet_buffer[info->tsf_ctx.packet_size * (psi_ctx->packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) * i]);
        if( i > 0)
        {
            /* copy ts packet header. */
            memcpy( write_buffer, psi_ctx->packet_buffer, section_start );
            for( int packet_count = 1; packet_count < psi_ctx->packet_cache_count; ++packet_count )
                memcpy( &(write_buffer[TS_PACKET_SIZE * packet_count]), &(psi_ctx->packet_buffer[TS_PACKET_SIZE * packet_count]), TS_PACKET_HEADER_SIZE );
        }
        read_count = 0;
        read_pos   = section_start;
        for( int packet_count = 1; read_pos < psi_ctx->packet_cache_size; ++packet_count )
        {
            int payload_size = TS_PACKET_SIZE * packet_count - read_pos;
            if( read_count < section_total )
            {
                int rest_size = section_total - read_count;
                if( payload_size > rest_size )
                {
                    memcpy( &(write_buffer[read_pos]), &(read_buffer[read_count]), rest_size );
                    memset( &(write_buffer[read_pos + rest_size]), 0xFF, payload_size - rest_size );
                }
                else
                    memcpy( &(write_buffer[read_pos]), &(read_buffer[read_count]), payload_size );
                read_count += payload_size;
            }
            else
            {
                memset( &(write_buffer[read_pos]), 0xFF, payload_size );
                /* set '0' to adaptation_field_control of unnecessary packet. */
                write_buffer[read_pos - TS_PACKET_HEADER_SIZE + 3] &= ~0x30;
            }
            read_pos += payload_size;
            /* seek next packet payload data. */
            read_pos += TS_PACKET_HEADER_SIZE;
        }
    }
    return 0;
}

static const char *get_stream_information
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    get_information_key_type    key
)
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || key >= GET_INFO_KEY_MAX )
        return NULL;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    tss_ctx_t     *stream  = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
        stream = &(psi_ctx->video_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
        stream = &(psi_ctx->audio_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
        stream = &(psi_ctx->caption_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
        stream = &(psi_ctx->dsmcc_stream[stream_number]);
    else
        return NULL;
    return stream->private_info.keys[key].info;
}

static mpeg_stream_type get_sample_stream_type( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return STREAM_INVALID;
    /* check stream type. */
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    tss_ctx_t     *stream  = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
        stream = &(psi_ctx->video_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
        stream = &(psi_ctx->audio_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
        stream = &(psi_ctx->caption_stream[stream_number]);
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
        stream = &(psi_ctx->dsmcc_stream[stream_number]);
    else
        return STREAM_INVALID;
    if( stream->stream_type == STREAM_PES_PRIVATE_DATA || stream->stream_type == STREAM_VIDEO_PRIVATE /* = STREAM_AUDIO_LPCM */ )
        return stream->private_info.reg_stream_type;
    return stream->stream_type;
}

static void free_sample_buffer( uint8_t **buffer )
{
    if( !buffer )
        return;
    if( *buffer )
        free( *buffer );
    buffer = NULL;
}

static int get_sample_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    int64_t                     position,
    uint32_t                    sample_size,
    int32_t                     read_offset,
    uint8_t                   **dst_buffer,
    uint32_t                   *dst_read_size,
    get_sample_data_mode        get_mode
)
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    /* check program id. */
    tsp_psi_ctx_t          *psi_ctx      = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t              *tsf_ctx      = NULL;
    uint16_t                program_id   = TS_PID_ERR;
 /* mpeg_stream_type        stream_type  = STREAM_INVALID;
    mpeg_stream_group_type  stream_judge = STREAM_IS_UNKNOWN; */
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
    {
        tsf_ctx      = &(psi_ctx->video_stream[stream_number].tsf_ctx);
     /* stream_judge =   psi_ctx->video_stream[stream_number].stream_judge;
        stream_type  =   psi_ctx->video_stream[stream_number].stream_type; */
        program_id   =   psi_ctx->video_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
    {
        tsf_ctx      = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
     /* stream_judge =   psi_ctx->audio_stream[stream_number].stream_judge;
        stream_type  =   psi_ctx->audio_stream[stream_number].stream_type; */
        program_id   =   psi_ctx->audio_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
    {
        tsf_ctx      = &(psi_ctx->caption_stream[stream_number].tsf_ctx);
     /* stream_judge =   psi_ctx->caption_stream[stream_number].stream_judge;
        stream_type  =   psi_ctx->caption_stream[stream_number].stream_type; */
        program_id   =   psi_ctx->caption_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
    {
        tsf_ctx      = &(psi_ctx->dsmcc_stream[stream_number].tsf_ctx);
     /* stream_judge =   psi_ctx->dsmcc_stream[stream_number].stream_judge;
        stream_type  =   psi_ctx->dsmcc_stream[stream_number].stream_type; */
        program_id   =   psi_ctx->dsmcc_stream[stream_number].program_id;
    }
    else
        return -1;
    /* seek reading start position. */
    mpegts_file_seek( tsf_ctx, position, MPEGTS_SEEK_RESET );
    tsf_ctx->sync_byte_position = 0;
    /* get data. */
    uint32_t  ts_packet_count = sample_size / TS_PACKET_SIZE;
    uint8_t  *buffer          = NULL;
    uint32_t  read_size       = 0;
    switch( get_mode )
    {
        case GET_SAMPLE_DATA_CONTAINER :
            mpegts_get_sample_ts_packet_data( tsf_ctx, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_PES_PACKET :
            mpegts_get_sample_pes_packet_data( tsf_ctx, program_id, ts_packet_count, &buffer, &read_size );
            break;
        case GET_SAMPLE_DATA_RAW :
            mpegts_get_sample_raw_data( tsf_ctx, program_id /*, stream_type, stream_judge */
                                      , sample_size, read_offset, &buffer, &read_size );
        default :
            break;
    }
    if( !buffer )
        return -1;
    mapi_log( LOG_LV3, "[debug] read_size:%d\n", read_size );
    *dst_buffer    = buffer;
    *dst_read_size = read_size;
    return 0;
}

static int64_t get_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t     *tsf_ctx = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
        tsf_ctx = &(psi_ctx->video_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
        tsf_ctx = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
        tsf_ctx = &(psi_ctx->caption_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
        tsf_ctx = &(psi_ctx->dsmcc_stream[stream_number].tsf_ctx);
    else
        return -1;
    //return mpegts_ftell( tsf_ctx );
    return tsf_ctx->read_position;
}

static int set_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, int64_t position )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || position < 0 )
        return -1;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t     *tsf_ctx = NULL;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
        tsf_ctx = &(psi_ctx->video_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
        tsf_ctx = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
        tsf_ctx = &(psi_ctx->caption_stream[stream_number].tsf_ctx);
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
        tsf_ctx = &(psi_ctx->dsmcc_stream[stream_number].tsf_ctx);
    else
        return -1;
    mpegts_file_seek( tsf_ctx, position, MPEGTS_SEEK_SET );
    tsf_ctx->read_position    = position;
    tsf_ctx->ts_packet_length = TS_PACKET_SIZE;
    return 0;
}

static int seek_next_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_psi_ctx_t *psi_ctx       = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t     *tsf_ctx       = NULL;
    int64_t        seek_position = -1;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
    {
        tsf_ctx       = &(psi_ctx->video_stream[stream_number].tsf_ctx);
        seek_position =   psi_ctx->video_stream[stream_number].tsf_ctx.read_position;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
    {
        tsf_ctx       = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
        seek_position =   psi_ctx->audio_stream[stream_number].tsf_ctx.read_position;
    }
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
    {
        tsf_ctx       = &(psi_ctx->caption_stream[stream_number].tsf_ctx);
        seek_position =   psi_ctx->caption_stream[stream_number].tsf_ctx.read_position;
    }
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
    {
        tsf_ctx       = &(psi_ctx->dsmcc_stream[stream_number].tsf_ctx);
        seek_position =   psi_ctx->dsmcc_stream[stream_number].tsf_ctx.read_position;
    }
    if( seek_position < 0 )
        return -1;
    mpegts_file_seek( tsf_ctx, seek_position, MPEGTS_SEEK_RESET );
    return 0;
}

static int get_specific_stream_data
(
    void                       *ih,
    get_sample_data_mode        get_mode,
    output_stream_type          output_stream,
    int                         update_psi,
    get_stream_data_cb_t       *cb
)
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_header_t   h;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t     *tsf_ctx = &(info->tsf_ctx);
#if 0
    /* reset start position. */
    mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_RESET );
    tsf_ctx->read_position      = 0;
    tsf_ctx->sync_byte_position = -1;
#endif
    /* check output mode. */
    int psi_required = get_mode == GET_SAMPLE_DATA_CONTAINER;
    /* list up the targets. */
    uint8_t video_stream_num   = (output_stream & OUTPUT_STREAM_VIDEO  ) ? psi_ctx->video_stream_num   : 0;
    uint8_t audio_stream_num   = (output_stream & OUTPUT_STREAM_AUDIO  ) ? psi_ctx->audio_stream_num   : 0;
    uint8_t caption_stream_num = (output_stream & OUTPUT_STREAM_CAPTION) ? psi_ctx->caption_stream_num : 0;
    uint8_t dsmcc_stream_num   = (output_stream & OUTPUT_STREAM_DSMCC  ) ? psi_ctx->dsmcc_stream_num   : 0;
    /* search. */
    mpeg_sample_type  sample_type   = SAMPLE_TYPE_PSI;
    uint8_t           stream_number = 0;
    tss_ctx_t        *stream        = NULL;
    tsp_psi_ctx_t    *pmt_psi_ctx   = NULL;
    tsp_psi_ctx_t    *pcr_psi_ctx   = NULL;
    tsp_psi_ctx_t    *ecm_psi_ctx   = NULL;
    while( 1 )
    {
        if( mpegts_read_packet_header( tsf_ctx, &h ) )
            return -1;
        /* seek ts packet head. */
        mpegts_file_seek( tsf_ctx, -(TS_PACKET_HEADER_SIZE), MPEGTS_SEEK_CUR );
        tsf_ctx->sync_byte_position = 0;
        /* check enable service. */
        if( info->pmt_ctx_index == info->pat_ctx.pid_list_num )
        {
            int enable_service = 0;
            for( int32_t i = 0; i < info->pat_ctx.pid_list_num && enable_service == 0; ++i )
            {
                tsp_psi_ctx_t *chk_psi_ctx = &(info->pmt_ctx[i]);
                for( int32_t j = 0; j < chk_psi_ctx->pid_list_num && enable_service == 0; ++j )
                    if( h.program_id == chk_psi_ctx->pid_list[j].program_id )
                    {
                        if( info->pat_ctx.pid_list[i].disable_pid )
                            goto next_packet;
                        enable_service = 1;
                    }
            }
        }
        /* check psi and ecm/emm packcet. */
        if( psi_required )
        {
            if( h.program_id < MPEGTS_PID_RESERVED_CHECK_VALUE )
                break;
            /* EMM */
            if( h.program_id == info->emm_program_id )
                break;
            /* PMT */
            if( (pmt_psi_ctx = mpegts_get_pmt_ctx( info, h.program_id )) )
                break;
            /* ECM */
            if( (ecm_psi_ctx = mpegts_get_ecm_ctx( info, h.program_id )) )
                break;
        }
        /* check the target stream. */
        for( uint8_t i = 0; !stream && i < video_stream_num; ++i )
            if( h.program_id == psi_ctx->video_stream[i].program_id )
            {
                sample_type   = SAMPLE_TYPE_VIDEO;
                stream_number = i;
                stream        = &(psi_ctx->video_stream[i]);
            }
        for( uint8_t i = 0; !stream && i < audio_stream_num; ++i )
            if( h.program_id == psi_ctx->audio_stream[i].program_id )
            {
                sample_type   = SAMPLE_TYPE_AUDIO;
                stream_number = i;
                stream        = &(psi_ctx->audio_stream[i]);
            }
        for( uint8_t i = 0; !stream && i < caption_stream_num; ++i )
            if( h.program_id == psi_ctx->caption_stream[i].program_id )
            {
                sample_type   = SAMPLE_TYPE_CAPTION;
                stream_number = i;
                stream        = &(psi_ctx->caption_stream[i]);
            }
        for( uint8_t i = 0; !stream && i < dsmcc_stream_num; ++i )
            if( h.program_id == psi_ctx->dsmcc_stream[i].program_id )
            {
                sample_type   = SAMPLE_TYPE_DSMCC;
                stream_number = i;
                stream        = &(psi_ctx->dsmcc_stream[i]);
            }
        /* check start position. */
        if( stream && tsf_ctx->read_position >= stream->tsf_ctx.read_position )
            break;
        /* check pcr packcet. */
        if( !stream && psi_required )
        {
            /* PCR */
            if( (pcr_psi_ctx = mpegts_get_pcr_ctx( info, h.program_id )) )
                break;
        }
    next_packet:
        /* seek next. */
        mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
        stream      = NULL;
        sample_type = SAMPLE_TYPE_PSI;
    }
    /* output. */
    uint32_t read_size   = 0;
    int32_t  read_offset = 0;
    switch( get_mode )
    {
        case GET_SAMPLE_DATA_CONTAINER :
            read_size = tsf_ctx->ts_packet_length;
            break;
        case GET_SAMPLE_DATA_PES_PACKET :
            /* seek payload data. */
            if( mpegts_seek_packet_payload_data( tsf_ctx, &h, stream->program_id, INDICATOR_UNCHECKED ) )
                return -1;
            read_size = tsf_ctx->ts_packet_length;
            break;
        case GET_SAMPLE_DATA_RAW :
            if( tsf_ctx->read_position == stream->tsf_ctx.read_position )
            {
                int64_t reset_position = stream->tsf_ctx.read_position;
                /* check offset. */
                sample_raw_data_info_t raw_data_info = { 0 };
                if( mpegts_get_sample_raw_data_info( &(stream->tsf_ctx), stream->program_id
                                                   , stream->stream_type, stream->stream_judge, &raw_data_info ) )
                    return -1;
                read_offset = raw_data_info.read_offset;
                /* chcek header skip. */
                if( mpeg_stream_check_header_skip( stream->stream_judge ) )
                    stream->header_offset = read_offset;
                /* reset. */
                mpegts_file_seek( &(stream->tsf_ctx), reset_position, MPEGTS_SEEK_SET );
                stream->tsf_ctx.read_position    = reset_position;
                stream->tsf_ctx.ts_packet_length = TS_PACKET_SIZE;
            }
            /* seek PES payload data */
            if( mpegts_seek_packet_payload_data( tsf_ctx, &h, stream->program_id, INDICATOR_UNCHECKED ) )
                return -1;
            if( h.payload_unit_start_indicator )
            {
                mpeg_pes_header_info_t pes_info;
                GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
                /* check PES packet header and header skip. */
                if( pes_info.pts_flag && stream->header_offset )
                    read_offset = stream->header_offset;
                /* skip PES packet header. */
                if( mpegts_skip_pes_header( tsf_ctx, &h, stream->program_id, &pes_info ) )
                    return -1;
            }
            read_size = tsf_ctx->ts_packet_length;
            break;
        default :
            break;
    }
    if( cb && cb->func && read_size )
    {
        //mapi_log( LOG_LV4, "[mpegts_parser] %s()  read_size:%u\n", __func__, read_size );
        enum {
            OUTPUT_NONE  = 0,
            OUTPUT_READ  = 1,
            OUTPUT_CACHE = 2,
        };
        uint8_t *buffer = NULL;
        /* check PSI update. */
        int cb_num = 0;
        int output = OUTPUT_READ;
        if( /* psi_required && */ sample_type == SAMPLE_TYPE_PSI )
        {
            if( h.program_id == TS_PID_PAT && info->pat_ctx.pid_list_num > 1 )
                update_psi = 1;
        }
        else
            update_psi = 0;
        if( update_psi )
        {
            psi_ctx = NULL;
            if( h.program_id == TS_PID_PAT )
            {
                /* PAT */
                psi_ctx = &(info->pat_ctx);
                if( psi_ctx->pid_list_num > 1 )
                    cb_num = (info->pmt_ctx_index == psi_ctx->pid_list_num) ? psi_ctx->pid_list_num : 1;
            }
#if 0
            else if( h.program_id == TS_PID_CAT )
                /* CAT */
                psi_ctx = &(info->cat_ctx);
#endif
            else if( pmt_psi_ctx )
                /* PMT */
                psi_ctx = pmt_psi_ctx;
            if( psi_ctx )
            {
                /* cache packet. */
                if(  h.payload_unit_start_indicator )
                    psi_ctx->packet_cache_count = psi_ctx->packet_cache_size = 0;
                mpegts_file_read( tsf_ctx, &(psi_ctx->packet_buffer[psi_ctx->packet_cache_size]), read_size );
                psi_ctx->packet_cache_size += read_size;
                ++ psi_ctx->packet_cache_count;
                /* update PSI */
                if( mpegts_update_psi( info, h.program_id, psi_ctx, output_stream ) > -1 )
                {
                    output = OUTPUT_CACHE;
                    /* output PSI packets. */
                    buffer    = psi_ctx->packet_buffer;
                    read_size = psi_ctx->packet_cache_size;
                }
                else
                {
                    output = OUTPUT_NONE;
                    /* check packet count. */
                    if( psi_ctx->packet_cache_count == psi_ctx->packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN )
                        psi_ctx->packet_cache_count = psi_ctx->packet_cache_size = 0;
                }
            }
        }
        if( output )
        {
            uint8_t read_buffer[256];
            if( output == OUTPUT_READ )
            {
                buffer = read_buffer;
                /* read packet data. */
                mpegts_file_read( tsf_ctx, buffer, read_size );
            }
            /* output. */
            get_stream_data_cb_ret_t cb_ret = {
                .sample_type    = sample_type,
                .stream_number  = stream_number,
                .buffer         = buffer,
                .read_size      = read_size,
                .read_offset    = read_offset,
                .progress       = mpegts_ftell( tsf_ctx ),
                .service_id     = 0,
                .pmt_program_id = 0
            };
            if( cb_num > 1 )
            {
                for( int i = 0; i < cb_num; ++i )
                {
                    if( info->pat_ctx.pid_list[i].disable_pid )
                        continue;
                    cb_ret.buffer         = &(buffer[info->tsf_ctx.packet_size * (info->pat_ctx.packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) * i]);
                    cb_ret.service_id     = info->pat_ctx.pid_list[i].program_number;
                    cb_ret.pmt_program_id = info->pat_ctx.pid_list[i].program_id;
                    cb->func( cb->params, (void *)&cb_ret );
                }
            }
            else
            {
                if( cb_num == 1 )
                    cb_ret.buffer = &(buffer[info->tsf_ctx.packet_size * (info->pat_ctx.packet_num + TS_PSI_PACKET_NUM_CHECK_MARGIN) * info->pmt_ctx_index]);
                if( info->pmt_ctx_index == info->pat_ctx.pid_list_num )
                {
                    if( sample_type != SAMPLE_TYPE_PSI )
                        for( int32_t i = 0; i < info->pat_ctx.pid_list_num && cb_ret.pmt_program_id == 0; ++i )
                        {
                            psi_ctx = &(info->pmt_ctx[i]);
                            for( int32_t j = 0; j < psi_ctx->pid_list_num && cb_ret.pmt_program_id == 0; ++j )
                                if( h.program_id == psi_ctx->pid_list[j].program_id )
                                    cb_ret.pmt_program_id = psi_ctx->pmt_program_id;
                        }
                    else
                    {
                        /* PMT, PCR, ECM */
                        if( pmt_psi_ctx )
                            cb_ret.pmt_program_id = pmt_psi_ctx->pmt_program_id;
                        else if( pcr_psi_ctx )
                            cb_ret.pmt_program_id = pcr_psi_ctx->pmt_program_id;
                        else if( ecm_psi_ctx )
                            cb_ret.pmt_program_id = ecm_psi_ctx->pmt_program_id;
                    }
                }
                cb->func( cb->params, (void *)&cb_ret );
            }
        }
    }
    /* seek next. */
    mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    return 0;
}

static int get_stream_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    int32_t                     read_offset,
    get_sample_data_mode        get_mode,
    get_stream_data_cb_t       *cb
)
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_header_t   h;
    tsp_psi_ctx_t *psi_ctx    = &(info->pmt_ctx[info->pmt_ctx_index]);
    tsf_ctx_t     *tsf_ctx    = NULL;
    uint16_t       program_id = TS_PID_ERR;
    if( sample_type == SAMPLE_TYPE_VIDEO && stream_number < psi_ctx->video_stream_num )
    {
        tsf_ctx    = &(psi_ctx->video_stream[stream_number].tsf_ctx);
        program_id =   psi_ctx->video_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_AUDIO && stream_number < psi_ctx->audio_stream_num )
    {
        tsf_ctx    = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
        program_id =   psi_ctx->audio_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_CAPTION && stream_number < psi_ctx->caption_stream_num )
    {
        tsf_ctx    = &(psi_ctx->caption_stream[stream_number].tsf_ctx);
        program_id =   psi_ctx->caption_stream[stream_number].program_id;
    }
    else if( sample_type == SAMPLE_TYPE_DSMCC && stream_number < psi_ctx->dsmcc_stream_num )
    {
        tsf_ctx    = &(psi_ctx->dsmcc_stream[stream_number].tsf_ctx);
        program_id =   psi_ctx->dsmcc_stream[stream_number].program_id;
    }
    else
        return -1;
    /* search. */
    uint32_t read_size = 0;
    switch( get_mode )
    {
        case GET_SAMPLE_DATA_CONTAINER :
            /* seek ts packet head. */
            if( mpegts_search_program_id_packet( tsf_ctx, &h, program_id ) )
                return -1;
            mpegts_file_seek( tsf_ctx, -(TS_PACKET_HEADER_SIZE), MPEGTS_SEEK_CUR );
            read_size   = tsf_ctx->ts_packet_length;
            read_offset = 0;
            break;
        case GET_SAMPLE_DATA_PES_PACKET :
            /* seek payload data. */
            if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
                return -1;
            read_size   = tsf_ctx->ts_packet_length;
            read_offset = 0;
            break;
        case GET_SAMPLE_DATA_RAW :
            while( 1 )
            {
                /* seek PES payload data */
                if( mpegts_seek_packet_payload_data( tsf_ctx, &h, program_id, INDICATOR_UNCHECKED ) )
                    return -1;
                if( h.payload_unit_start_indicator )
                {
                    mpeg_pes_header_info_t pes_info;
                    GET_PES_PACKET_HEADER( tsf_ctx, pes_info );
                    /* skip PES packet header. */
                    if( mpegts_skip_pes_header( tsf_ctx, &h, program_id, &pes_info ) )
                        return -1;
                }
                /* check read start point. */
                if( read_offset <= tsf_ctx->ts_packet_length )
                    break;
                read_offset -= tsf_ctx->ts_packet_length;
                mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
            }
            if( read_offset )
            {
                mpegts_file_seek( tsf_ctx, read_offset, MPEGTS_SEEK_CUR );
                read_offset = 0;
            }
            read_size = tsf_ctx->ts_packet_length;
            break;
        default :
            read_size   = 0;
            read_offset = 0;
            break;
    }
    if( cb && cb->func && read_size )
    {
        //mapi_log( LOG_LV4, "[mpegts_parser] %s()  read_size:%u\n", __func__, read_size );
        /* read packet data. */
        uint8_t buffer[256];
        mpegts_file_read( tsf_ctx, buffer, read_size );
        /* output. */
        get_stream_data_cb_ret_t cb_ret = {
            .buffer    = buffer,
            .read_size = read_size,
            .progress  = mpegts_ftell( tsf_ctx )
        };
        cb->func( cb->params, (void *)&cb_ret );
    }
    /* seek next. */
    mpegts_file_seek( tsf_ctx, 0, MPEGTS_SEEK_NEXT );
    return read_offset;
}

static uint8_t get_stream_num( void *ih, mpeg_sample_type sample_type, uint16_t service_id )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return 0;
    uint32_t pmt_ctx_index = info->pmt_ctx_index;
    if( service_id )
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
            if( info->pat_ctx.pid_list[i].program_number == service_id )
            {
                pmt_ctx_index = i;
                break;
            }
    /* check stream num. */
    uint8_t stream_num = 0;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
    if( sample_type == SAMPLE_TYPE_VIDEO )
        stream_num = psi_ctx->video_stream_num;
    else if( sample_type == SAMPLE_TYPE_AUDIO )
        stream_num = psi_ctx->audio_stream_num;
    else if( sample_type == SAMPLE_TYPE_CAPTION )
        stream_num = psi_ctx->caption_stream_num;
    else if( sample_type == SAMPLE_TYPE_DSMCC )
        stream_num = psi_ctx->dsmcc_stream_num;
    return stream_num;
}

static int get_pcr( void *ih, pcr_info_t *pcr_info, uint16_t service_id )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    uint32_t pmt_ctx_index = 0;
    if( service_id )
    {
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
            if( info->pat_ctx.pid_list[i].program_number == service_id )
            {
                pmt_ctx_index = i;
                break;
            }
    }
    else if( info->pmt_ctx_index < info->pat_ctx.pid_list_num )
        pmt_ctx_index = info->pmt_ctx_index;
    tsp_psi_ctx_t *psi_ctx        = &(info->pmt_ctx[pmt_ctx_index]);
    int64_t        reset_position = mpegts_ftell( &(info->tsf_ctx) );
    /* get PCR. */
    int result = mpegts_get_pcr( info, &(psi_ctx->pcr), pmt_ctx_index );
    mpegts_file_seek( &(info->tsf_ctx), reset_position, MPEGTS_SEEK_RESET );
    /* setup. */
    pcr_info->pcr       = psi_ctx->pcr;
    pcr_info->start_pcr = psi_ctx->start_pcr;
    pcr_info->last_pcr  = psi_ctx->last_pcr;
    return result;
}

static int get_video_info( void *ih, uint8_t stream_number, video_sample_info_t *video_sample_info )
{
    mapi_log( LOG_LV2, "[mpegts_parser] %s()\n", __func__ );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    if( stream_number >= psi_ctx->video_stream_num )
        return -1;
    tsf_ctx_t              *tsf_ctx                 = &(psi_ctx->video_stream[stream_number].tsf_ctx);
    uint16_t                program_id              =   psi_ctx->video_stream[stream_number].program_id;
    mpeg_stream_type        stream_type             =   psi_ctx->video_stream[stream_number].stream_type;
    mpeg_stream_group_type  stream_judge            =   psi_ctx->video_stream[stream_number].stream_judge;
    void                   *stream_parse_info       =   psi_ctx->video_stream[stream_number].stream_parse_info;
    int64_t                *video_stream_gop_number = &(psi_ctx->video_stream[stream_number].gop_number);
    /* check PES stream id. */
    mpeg_pes_stream_id_type stream_id_type = mpeg_pes_get_steam_id_type( stream_judge );
    /* get timestamp. */
    mpeg_timestamp_t ts = { MPEG_TIMESTAMP_INVALID_VALUE, MPEG_TIMESTAMP_INVALID_VALUE };
    if( mpegts_get_stream_timestamp( tsf_ctx, program_id, stream_id_type, &ts ) )
        return -1;
    int64_t start_position = tsf_ctx->read_position;
    /* check raw data. */
    sample_raw_data_info_t raw_data_info = { 0 };
    if( mpegts_get_sample_raw_data_info( tsf_ctx, program_id, stream_type, stream_judge, &raw_data_info ) )
        return -1;
    mapi_log( LOG_LV4, "[debug] raw_data  size:%u  read_offset:%d\n"
                     , raw_data_info.data_size, raw_data_info.read_offset );
    /* parse payload data. */
    int64_t gop_number           = -1;
    uint8_t progressive_sequence = 0;
    uint8_t closed_gop           = 0;
    uint8_t picture_coding_type  = MPEG_VIDEO_UNKNOWN_FRAME;
    int16_t temporal_reference   = -1;
    uint8_t picture_structure    = 0;
    uint8_t progressive_frame    = 0;
    uint8_t repeat_first_field   = 0;
    uint8_t top_field_first      = 0;
    if( (stream_judge & STREAM_IS_MPEG_VIDEO) == STREAM_IS_MPEG_VIDEO )
    {
        mpeg_video_info_t *video_info = (mpeg_video_info_t *)stream_parse_info;
        if( !mpegts_get_mpeg_video_picture_info( tsf_ctx, program_id, video_info, video_stream_gop_number ) )
        {
            gop_number           = *video_stream_gop_number;
            progressive_sequence = video_info->sequence_ext.progressive_sequence;
            closed_gop           = video_info->gop.closed_gop;
            picture_coding_type  = video_info->picture.picture_coding_type;
            temporal_reference   = video_info->picture.temporal_reference;
            picture_structure    = video_info->picture_coding_ext.picture_structure;
            progressive_frame    = video_info->picture_coding_ext.progressive_frame;
            repeat_first_field   = video_info->picture_coding_ext.repeat_first_field;
            top_field_first      = video_info->picture_coding_ext.top_field_first;
        }
    }
    else
    {
        gop_number = 0;     // FIXME
    }
    /* check packets num. */
    mpegts_file_seek( tsf_ctx, start_position, MPEGTS_SEEK_RESET );
    tsf_ctx->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( tsf_ctx, program_id, stream_type );
    if( !ts_packet_count )
        return -1;
    /* setup. */
    video_sample_info->file_position        = start_position;
    video_sample_info->sample_size          = TS_PACKET_SIZE * ts_packet_count;
    video_sample_info->raw_data_size        = raw_data_info.data_size;
    video_sample_info->raw_data_read_offset = raw_data_info.read_offset;
    video_sample_info->au_size              = tsf_ctx->packet_size;
    video_sample_info->program_id           = program_id;
    video_sample_info->pts                  = ts.pts;
    video_sample_info->dts                  = ts.dts;
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
    mapi_log( LOG_LV2, "[check] Video PTS:%" PRId64 " [%" PRId64 "ms], [%c] temporal_reference:%d\n"
                     , ts.pts, ts.pts / 90, frame[picture_coding_type], temporal_reference );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", start_position );
    /* prepare for next. */
    tsf_ctx->sync_byte_position = -1;
    tsf_ctx->read_position      = mpegts_ftell( tsf_ctx );
    return 0;
}

static int get_audio_info( void *ih, uint8_t stream_number, audio_sample_info_t *audio_sample_info )
{
    mapi_log( LOG_LV2, "[mpegts_parser] %s()\n", __func__ );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    if( stream_number >= psi_ctx->audio_stream_num )
        return -1;
    tsf_ctx_t              *tsf_ctx      = &(psi_ctx->audio_stream[stream_number].tsf_ctx);
    uint16_t                program_id   =   psi_ctx->audio_stream[stream_number].program_id;
    mpeg_stream_type        stream_type  =   psi_ctx->audio_stream[stream_number].stream_type;
    mpeg_stream_group_type  stream_judge =   psi_ctx->audio_stream[stream_number].stream_judge;
    /* check PES stream id. */
    mpeg_pes_stream_id_type stream_id_type = mpeg_pes_get_steam_id_type( stream_judge );
    /* get timestamp. */
    mpeg_timestamp_t ts = { MPEG_TIMESTAMP_INVALID_VALUE, MPEG_TIMESTAMP_INVALID_VALUE };
    if( mpegts_get_stream_timestamp( tsf_ctx, program_id, stream_id_type, &ts ) )
        return -1;
    int64_t start_position = tsf_ctx->read_position;
    /* check raw data. */
    sample_raw_data_info_t raw_data_info = { 0 };
    if( mpegts_get_sample_raw_data_info( tsf_ctx, program_id, stream_type, stream_judge, &raw_data_info ) )
        return -1;
    mapi_log( LOG_LV4, "[debug] raw_data  size:%u  read_offset:%d\n"
                     , raw_data_info.data_size, raw_data_info.read_offset );
    /* check packets num. */
    //mpegts_file_seek( tsf_ctx, start_position, MPEGTS_SEEK_RESET );
    //tsf_ctx->sync_byte_position = 0;
    uint32_t ts_packet_count = mpegts_get_sample_packets_num( tsf_ctx, program_id, stream_type );
    if( !ts_packet_count )
        return -1;
    /* setup. */
    audio_sample_info->file_position        = start_position;
    audio_sample_info->sample_size          = TS_PACKET_SIZE * ts_packet_count;
    audio_sample_info->raw_data_size        = raw_data_info.data_size;
    audio_sample_info->raw_data_read_offset = raw_data_info.read_offset;
    audio_sample_info->au_size              = tsf_ctx->packet_size;
    audio_sample_info->program_id           = program_id;
    audio_sample_info->pts                  = ts.pts;
    audio_sample_info->dts                  = ts.dts;
    audio_sample_info->sampling_frequency   = raw_data_info.stream_raw_info.sampling_frequency;
    audio_sample_info->bitrate              = raw_data_info.stream_raw_info.bitrate;
    audio_sample_info->channel              = raw_data_info.stream_raw_info.channel;
    audio_sample_info->layer                = raw_data_info.stream_raw_info.layer;
    audio_sample_info->bit_depth            = raw_data_info.stream_raw_info.bit_depth;
    mapi_log( LOG_LV2, "[check] Audio PTS:%" PRId64 " [%" PRId64 "ms]\n", ts.pts, ts.pts / 90 );
    mapi_log( LOG_LV2, "[check] file position:%" PRId64 "\n", start_position );
    /* prepare for next. */
    tsf_ctx->sync_byte_position = -1;
    tsf_ctx->read_position      = mpegts_ftell( tsf_ctx );
    return 0;
}

static inline void release_stream_handle( tss_ctx_t **stream_ctxs, uint8_t *stream_num )
{
    if( !(*stream_ctxs) )
        return;
    for( uint8_t i = 0; i < *stream_num; ++i )
    {
        tss_ctx_t *stream = (*stream_ctxs) + i;
        mpegts_close( &(stream->tsf_ctx) );
        if( stream->stream_parse_info )
            free( stream->stream_parse_info );
    }
    free( *stream_ctxs );
    *stream_ctxs = NULL;
    *stream_num  = 0;
}

static int set_pmt_stream_info( mpegts_info_t *info )
{
    mapi_log( LOG_LV2, "[check] [sub] %s()\n", __func__ );
    int64_t        start_position = mpegts_ftell( &(info->tsf_ctx) );
    tsp_psi_ctx_t *psi_ctx        = NULL;
    tsp_header_t   h;
    /* check stream num. */
    uint8_t video_stream_num = 0, audio_stream_num = 0, caption_stream_num = 0, dsmcc_stream_num = 0;
    for( int32_t pmt_ctx_index = 0; pmt_ctx_index < info->pat_ctx.pid_list_num; ++pmt_ctx_index )
    {
        psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
        /* compare with the detected pmt pid. */
        int new_pmt = 1;
        for( int32_t i = 0; i < pmt_ctx_index && new_pmt; ++i )
        {
            tsp_psi_ctx_t *pre_psi_ctx = &(info->pmt_ctx[i]);
            if( pre_psi_ctx->pmt_program_id == psi_ctx->pmt_program_id )
                new_pmt = 0;
        }
        if( !new_pmt )
            continue;
        /* check pid list. */
        for( int32_t pid_list_index = 0; pid_list_index < psi_ctx->pid_list_num; ++pid_list_index )
        {
            /* compare with the detected pid. */
            int new_pid = 1;
            for( int32_t i = 0; i < pmt_ctx_index && new_pid; ++i )
            {
                tsp_psi_ctx_t *pre_psi_ctx = &(info->pmt_ctx[i]);
                for( int32_t j = 0; j < pre_psi_ctx->pid_list_num && new_pid; ++j )
                    if( psi_ctx->pid_list[pid_list_index].program_id == pre_psi_ctx->pid_list[j].program_id )
                        new_pid = 0;
            }
            if( !new_pid )
                continue;
            /* count by stream type. */
            mpeg_stream_group_type stream_judge = psi_ctx->pid_list[pid_list_index].stream_judge;
            if( stream_judge & STREAM_IS_VIDEO )
                ++video_stream_num;
            else if( stream_judge & STREAM_IS_AUDIO )
                ++audio_stream_num;
            else if( stream_judge & STREAM_IS_CAPTION )
                ++caption_stream_num;
            else if( stream_judge & STREAM_IS_DSMCC )
                ++dsmcc_stream_num;
        }
    }
    if( !video_stream_num && !audio_stream_num && !caption_stream_num && !dsmcc_stream_num )
        return -1;
    /* allocate streams context. */
    tss_ctx_t *video_ctx = NULL, *audio_ctx = NULL, *caption_ctx = NULL, *dsmcc_ctx = NULL;
    if( video_stream_num )
        video_ctx = (tss_ctx_t *)malloc( sizeof(tss_ctx_t) * video_stream_num );
    if( audio_stream_num )
        audio_ctx = (tss_ctx_t *)malloc( sizeof(tss_ctx_t) * audio_stream_num );
    if( caption_stream_num )
        caption_ctx = (tss_ctx_t *)malloc( sizeof(tss_ctx_t) * caption_stream_num );
    if( dsmcc_stream_num )
        dsmcc_ctx = (tss_ctx_t *)malloc( sizeof(tss_ctx_t) * dsmcc_stream_num );
    if( (video_stream_num && !video_ctx)
     || (audio_stream_num && !audio_ctx)
     || (caption_stream_num && !caption_ctx)
     || (dsmcc_stream_num && !dsmcc_ctx) )
        goto fail_set_pmt_stream_info;
    /* initialize context items. */
    for( uint8_t i = 0; i < video_stream_num; ++i )
    {
        tss_ctx_t *stream = &(video_ctx[i]);
        stream->tsf_ctx.fr_ctx    = NULL;
        stream->stream_parse_info = NULL;
    }
    for( uint8_t i = 0; i < audio_stream_num; ++i )
    {
        tss_ctx_t *stream = &(audio_ctx[i]);
        stream->tsf_ctx.fr_ctx    = NULL;
        stream->stream_parse_info = NULL;
    }
    for( uint8_t i = 0; i < caption_stream_num; ++i )
    {
        tss_ctx_t *stream = &(caption_ctx[i]);
        stream->tsf_ctx.fr_ctx    = NULL;
        stream->stream_parse_info = NULL;
    }
    for( uint8_t i = 0; i < dsmcc_stream_num; ++i )
    {
        tss_ctx_t *stream = &(dsmcc_ctx[i]);
        stream->tsf_ctx.fr_ctx    = NULL;
        stream->stream_parse_info = NULL;
    }
    /* check exist. */
    video_stream_num = audio_stream_num = caption_stream_num = dsmcc_stream_num = 0;
    for( int32_t pmt_ctx_index = 0; pmt_ctx_index < info->pat_ctx.pid_list_num; ++pmt_ctx_index )
    {
        tss_ctx_t *v_stream = NULL, *a_stream = NULL, *c_stream = NULL, *d_stream = NULL;
        uint8_t video_count = 0, audio_count = 0, caption_count = 0, dsmcc_count = 0;
        psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
        for( int32_t pid_list_index = 0; pid_list_index < psi_ctx->pid_list_num; ++pid_list_index )
        {
            uint16_t               program_id      = psi_ctx->pid_list[pid_list_index].program_id;
            mpeg_stream_type       stream_type     = psi_ctx->pid_list[pid_list_index].stream_type;
            mpeg_stream_group_type stream_judge    = psi_ctx->pid_list[pid_list_index].stream_judge;
            mpeg_stream_type       reg_stream_type = psi_ctx->pid_list[pid_list_index].reg_stream_type;
            /* check target. */
            static const char *stream_name[4] = { " video ", " audio ", "caption", " dsm-cc" };
            tss_ctx_t  *chk_stream   = NULL;
            tss_ctx_t  *stream       = NULL;
            uint8_t    *stream_num   = NULL;
            uint8_t    *stream_count = NULL;
            tss_ctx_t **stream_ref   = NULL;
            int         index        = 0;
            if( stream_judge & STREAM_IS_VIDEO )
            {
                chk_stream   = video_ctx;
                stream       = &(video_ctx[video_stream_num]);
                stream_num   = &video_stream_num;
                stream_count = &video_count;
                stream_ref   = &v_stream;
                index        = 0;
            }
            else if( stream_judge & STREAM_IS_AUDIO )
            {
                chk_stream   = audio_ctx;
                stream       = &(audio_ctx[audio_stream_num]);
                stream_num   = &audio_stream_num;
                stream_count = &audio_count;
                stream_ref   = &a_stream;
                index        = 1;
            }
            else if( stream_judge & STREAM_IS_CAPTION )
            {
                chk_stream   = caption_ctx;
                stream       = &(caption_ctx[caption_stream_num]);
                stream_num   = &caption_stream_num;
                stream_count = &caption_count;
                stream_ref   = &c_stream;
                index        = 2;
            }
            else if( stream_judge & STREAM_IS_DSMCC )
            {
                chk_stream   = dsmcc_ctx;
                stream       = &(dsmcc_ctx[dsmcc_stream_num]);
                stream_num   = &dsmcc_stream_num;
                stream_count = &dsmcc_count;
                stream_ref   = &d_stream;
                index        = 3;
            }
            else
                continue;
            /* compare with the detected pid. */
            while( chk_stream != stream /* && !(*stream_ref) */ )
            {
                if( chk_stream->program_id == program_id )
                {
                    if( !(*stream_ref) )
                        *stream_ref = chk_stream;
                    stream = NULL;
                    break;
                }
                ++chk_stream;
            }
            if( !stream /* || *stream_ref */ )
            {
                ++(*stream_count);
                continue;
            }
            /* search. */
            int detect_check = (stream_judge & (STREAM_IS_VIDEO | STREAM_IS_AUDIO))
                             ? mpegts_search_program_id_packet( &(info->tsf_ctx), &h, program_id ) : 0;
            if( !detect_check )
            {
                /* prepare parse context. */
                if( !mpegts_open( &(stream->tsf_ctx), info->mpegts, info->buffer_size ) )
                {
                    /* allocate. */
                    void *stream_parse_info;
                    if( mpegts_malloc_stream_parse_ctx( /* stream_type, */ stream_judge, &stream_parse_info ) )
                    {
                        mpegts_close( &(stream->tsf_ctx) );
                        goto fail_allocate_ctxs;
                    }
                    /* setup. */
                    stream->tsf_ctx.packet_size            = info->tsf_ctx.packet_size;
                    stream->tsf_ctx.sync_byte_position     = -1;
                    stream->tsf_ctx.read_position          = 0;
                    stream->tsf_ctx.ts_packet_length       = TS_PACKET_SIZE;
                    stream->tsf_ctx.packet_check_count_num = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
                    stream->program_id                     = program_id;
                    stream->stream_type                    = stream_type;
                    stream->stream_judge                   = stream_judge;
                    stream->stream_parse_info              = stream_parse_info;
                    stream->gop_number                     = -1;
                    stream->header_offset                  = 0;
                    stream->private_info.reg_stream_type   = reg_stream_type;
                    sprintf( stream->private_info.keys[GET_INFO_KEY_ID].info, "PID %x", program_id );
                    mapi_log( LOG_LV2, "[check] %s PID:0x%04X, stream_type:0x%02X\n"
                                     , stream_name[index], program_id, stream_type );
                    mpegts_file_seek( &(stream->tsf_ctx), info->tsf_ctx.read_position, MPEGTS_SEEK_SET );
                    ++(*stream_num);
                    ++(*stream_count);
                }
            }
            else
                mapi_log( LOG_LV2, "[check] Undetected - %s PID:0x%04X, stream_type:0x%02X\n"
                                 , stream_name[index], program_id, stream_type );
            /* reset position. */
            mpegts_file_seek( &(info->tsf_ctx), start_position, MPEGTS_SEEK_RESET );
        }
        /* setup. */
        psi_ctx->video_stream       = v_stream ? v_stream : &(video_ctx[video_stream_num - video_count]);
        psi_ctx->audio_stream       = a_stream ? a_stream : &(audio_ctx[audio_stream_num - audio_count]);;
        psi_ctx->caption_stream     = c_stream ? c_stream : &(caption_ctx[caption_stream_num - caption_count]);
        psi_ctx->dsmcc_stream       = d_stream ? d_stream : &(dsmcc_ctx[dsmcc_stream_num - dsmcc_count]);
        psi_ctx->video_stream_num   = video_count;
        psi_ctx->audio_stream_num   = audio_count;
        psi_ctx->caption_stream_num = caption_count;
        psi_ctx->dsmcc_stream_num   = dsmcc_count;
    }
    if( !video_stream_num )
    {
        free( video_ctx );
        video_ctx = NULL;
    }
    if( !audio_stream_num )
    {
        free( audio_ctx );
        audio_ctx = NULL;
    }
    if( !caption_stream_num )
    {
        free( caption_ctx );
        caption_ctx = NULL;
    }
    if( !dsmcc_stream_num )
    {
        free( dsmcc_ctx );
        dsmcc_ctx = NULL;
    }
    if( !video_ctx && !audio_ctx && !caption_ctx && !dsmcc_ctx )
        return -1;
    /* create pid list for multi service. */
    mpegts_pid_info_t *pid_list = (mpegts_pid_info_t *)malloc( sizeof(mpegts_pid_info_t) * (video_stream_num + audio_stream_num + caption_stream_num + dsmcc_stream_num) );
    if( !pid_list )
        goto fail_allocate_ctxs;
    uint16_t pid_list_num = 0;
    for( int32_t pmt_ctx_index = 0; pmt_ctx_index < info->pat_ctx.pid_list_num; ++pmt_ctx_index )
    {
        psi_ctx = &(info->pmt_ctx[pmt_ctx_index]);
        for( int32_t pid_list_index = 0; pid_list_index < psi_ctx->pid_list_num; ++pid_list_index )
        {
            if( psi_ctx->pid_list[pid_list_index].stream_judge == STREAM_IS_UNKNOWN )
                continue;
            /* compare with the detected pid. */
            int new_pid = 1;
            for( int32_t i = 0; i < pmt_ctx_index && new_pid; ++i )
            {
                tsp_psi_ctx_t *pre_psi_ctx = &(info->pmt_ctx[i]);
                for( int32_t j = 0; j < pre_psi_ctx->pid_list_num && new_pid; ++j )
                    if( psi_ctx->pid_list[pid_list_index].program_id == pre_psi_ctx->pid_list[j].program_id )
                        new_pid = 0;
            }
            if( new_pid )
            {
                /* copy pid list information. */
                pid_list[pid_list_num] = psi_ctx->pid_list[pid_list_index];
                ++pid_list_num;
            }
        }
    }
    /* setup. */
    psi_ctx                     = &(info->pmt_ctx[info->pat_ctx.pid_list_num]);
    psi_ctx->video_stream       = video_ctx;
    psi_ctx->audio_stream       = audio_ctx;
    psi_ctx->caption_stream     = caption_ctx;
    psi_ctx->dsmcc_stream       = dsmcc_ctx;
    psi_ctx->video_stream_num   = video_stream_num;
    psi_ctx->audio_stream_num   = audio_stream_num;
    psi_ctx->caption_stream_num = caption_stream_num;
    psi_ctx->dsmcc_stream_num   = dsmcc_stream_num;
    psi_ctx->pid_list           = pid_list;
    psi_ctx->pid_list_num       = pid_list_num;
    return 0;
fail_allocate_ctxs:
    /* release. */
    release_stream_handle( &video_ctx  , &video_stream_num   );
    release_stream_handle( &audio_ctx  , &audio_stream_num   );
    release_stream_handle( &caption_ctx, &caption_stream_num );
    release_stream_handle( &dsmcc_ctx  , &dsmcc_stream_num   );
    return -1;
fail_set_pmt_stream_info:
    if( video_ctx )
        free( video_ctx );
    if( audio_ctx )
        free( audio_ctx );
    if( caption_ctx )
        free( caption_ctx );
    if( dsmcc_ctx )
        free( dsmcc_ctx );
    return -1;
}

static void release_all_stream_handle( mpegts_info_t *info )
{
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pat_ctx.pid_list_num]);
    release_stream_handle( &(psi_ctx->video_stream), &(psi_ctx->video_stream_num) );
    release_stream_handle( &(psi_ctx->audio_stream), &(psi_ctx->audio_stream_num) );
    release_stream_handle( &(psi_ctx->caption_stream), &(psi_ctx->caption_stream_num) );
    release_stream_handle( &(psi_ctx->dsmcc_stream), &(psi_ctx->dsmcc_stream_num) );
}

static void release_psi_ctx_handle( tsp_psi_ctx_t *psi_ctx )
{
    if( psi_ctx->pid_list )
    {
        free( psi_ctx->pid_list );
        psi_ctx->pid_list = NULL;
    }
    if( psi_ctx->packet_buffer )
    {
        free( psi_ctx->packet_buffer );
        psi_ctx->packet_buffer = NULL;
    }
    if( psi_ctx->section_buffer )
    {
        free( psi_ctx->section_buffer );
        psi_ctx->section_buffer = NULL;
    }
    if( psi_ctx->cache_buffer )
    {
        free( psi_ctx->cache_buffer );
        psi_ctx->cache_buffer = NULL;
    }
}

static void release_all_handle( mpegts_info_t *info )
{
    release_all_stream_handle( info );
    if( info->pmt_ctx )
    {
        for( int32_t i = 0; i < info->pat_ctx.pid_list_num + 1; ++i )
            release_psi_ctx_handle( &(info->pmt_ctx[i]) );
        free( info->pmt_ctx );
        info->pmt_ctx = NULL;
    }
    release_psi_ctx_handle( &(info->cat_ctx) );
    release_psi_ctx_handle( &(info->pat_ctx) );
    info->status = PARSER_STATUS_NON_PARSING;
}

static int parse_pmt_info( mpegts_info_t *info )
{
    mapi_log( LOG_LV2, "[check] [sub] %s()\n", __func__ );
    /* prepare. */
    info->pmt_ctx = (mpegts_psi_ctx_t *)calloc( info->pat_ctx.pid_list_num + 1, sizeof(mpegts_psi_ctx_t) );
    if( !info->pmt_ctx )
        goto fail_parse;
    for( int32_t i = 0; i < info->pat_ctx.pid_list_num + 1; ++i )
    {
        info->pmt_ctx[i].pmt_program_id = TS_PID_ERR;
        info->pmt_ctx[i].pcr_program_id = TS_PID_ERR;
        info->pmt_ctx[i].ecm_program_id = TS_PID_ERR;
        info->pmt_ctx[i].pcr            = MPEG_TIMESTAMP_INVALID_VALUE;
        info->pmt_ctx[i].start_pcr      = MPEG_TIMESTAMP_INVALID_VALUE;
        info->pmt_ctx[i].last_pcr       = MPEG_TIMESTAMP_INVALID_VALUE;
    }
    /* parse. */
    int64_t start_position = mpegts_ftell( &(info->tsf_ctx) );
    for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
    {
        
        if( mpegts_parse_pmt( info, i ) < 0 )
            goto fail_parse;
        if( info->pmt_ctx[i].pcr_program_id && mpegts_parse_pcr( info, i ) )
            goto fail_parse;
        /* reset position. */
        mpegts_file_seek( &(info->tsf_ctx), start_position, MPEGTS_SEEK_RESET );
    }
    /* setup active PMT. */
    int32_t pmt_ctx_index = 0;
    while( pmt_ctx_index < info->pat_ctx.pid_list_num )
    {
        if( info->pat_ctx.pid_list[pmt_ctx_index].program_number )
            break;
        ++pmt_ctx_index;
    }
    if( pmt_ctx_index == info->pat_ctx.pid_list_num )
        goto fail_parse;
    info->pmt_ctx_index = pmt_ctx_index;
    /* check user specified IDs. */
    uint16_t target_pmt_pid = TS_PID_ERR;
    if( info->specified_service_id )
    {
        /* search target PID. */
        target_pmt_pid = mpegts_get_pmt_pid_from_sid( info, info->specified_service_id );
        info->specified_service_id = 0;
    }
    if( target_pmt_pid == TS_PID_ERR && info->specified_pmt_program_id != TS_PID_ERR )
        target_pmt_pid = info->specified_pmt_program_id;
    if( target_pmt_pid != TS_PID_ERR )
    {
        /* setup the specified PMT PID. */
        if( mpegts_set_user_specified_pmt( info, target_pmt_pid ) )
            mapi_log( LOG_LV2, "[check] illegal pmt_PID:0x%04X is specified. using PID in PAT.\n", target_pmt_pid );
        else
            mapi_log( LOG_LV2, "[check] using pmt_PID:0x%04X\n", target_pmt_pid );
        info->specified_pmt_program_id = TS_PID_ERR;
    }
    return 0;
fail_parse:
    release_all_handle( info );
    return -1;
}

static int set_pmt_program_id( mpegts_info_t *info, uint16_t program_id )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    if( info->status == PARSER_STATUS_NON_PARSING )
    {
        /* save the specified PMT PID. */
        info->specified_pmt_program_id = program_id;
        return 0;
    }
    /* check for all. */
    if( program_id == MAPI_ALL_SERVICE_PMT_PID )
        return mpegts_set_user_specified_pmt( info, program_id );
    /* setup the specified PMT PID. */
    int result = mpegts_set_user_specified_pmt( info, program_id );
    if( result )
        mapi_log( LOG_LV2, "[check] illegal pmt_PID:0x%04X is specified. using PID in PAT.\n", program_id );
    else
        mapi_log( LOG_LV2, "[check] using pmt_PID:0x%04X\n", program_id );
    return result;
}

static int set_stream_program_id( mpegts_info_t *info, uint16_t program_id )
{
    mapi_log( LOG_LV2, "[check] %s()\n", __func__ );
    /* search program id and stream type. */
    int result = -1;
#if 0       // FIXME
    tsp_psi_ctx_t *psi_ctx = &(info->pmt_ctx[info->pmt_ctx_index]);
    for( int32_t pid_list_index = 0; pid_list_index < psi_ctx->pid_list_num; ++pid_list_index )
    {
        if( psi_ctx->pid_list[pid_list_index].program_id == program_id )
        {
            mpeg_stream_group_type stream_judge = set_stream_info( info, psi_ctx->pid_list[pid_list_index].program_id
                                                                 , psi_ctx->pid_list[pid_list_index].stream_type
                                                                 , psi_ctx->pid_list[pid_list_index].stream_judge );
            if( (stream_judge & STREAM_IS_VIDEO) || (stream_judge & STREAM_IS_AUDIO) )
                result = 0;
        }
    }
#elif ENABLE_SUPPRESS_WARNINGS
    (void) info;
    (void) program_id;
#endif
    return result;
}

static int set_program_target( void *ih, pmt_target_type pmt_target )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    mapi_log( LOG_LV2, "[mpegts_parser] set_program_target()\n"
                       "[check] pmt_target: %d\n", pmt_target );
    info->pmt_target = pmt_target;
    return 0;
}

static int set_program_id( void *ih, mpegts_select_pid_type pid_type, uint16_t program_id )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    mapi_log( LOG_LV2, "[mpegts_parser] set_program_id()\n"
                       "[check] program_id: 0x%04X\n", program_id );
    if( program_id & MPEGTS_ILLEGAL_PROGRAM_ID_MASK )
    {
        mapi_log( LOG_LV2, "[check] illegal PID is specified. using PID in PAT.\n" );
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

static uint16_t get_program_id( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, uint16_t service_id )
{
    mapi_log( LOG_LV2, "[mpegts_parser] get_program_id()\n"
                       "[check] sample_type: 0x%02X\n", sample_type );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return TS_PID_ERR;
    return mpegts_get_program_id( info, sample_type, stream_number, service_id );
}

static int set_service_id( void *ih, uint16_t service_id )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    mapi_log( LOG_LV2, "[mpegts_parser] set_service_id()\n"
                       "[check] service_id: 0x%04X\n", service_id );
    if( service_id == 0 )
    {
        mapi_log( LOG_LV2, "[check] illegal SID is specified.\n" );
        return 1;
    }
    if( info->status == PARSER_STATUS_NON_PARSING )
    {
        /* save the specified SID. */
        info->specified_service_id = service_id;
        return 0;
    }
    /* search target PMT PID. */
    uint16_t pmt_program_id = mpegts_get_pmt_pid_from_sid( info, service_id );
    if( pmt_program_id == TS_PID_ERR )
        return -1;
    /* setup target PMT PID. */
    int result = set_pmt_program_id( info, pmt_program_id );
    return result;
}

static int32_t get_service_id_num( void *ih )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || info->status == PARSER_STATUS_NON_PARSING )
        return -1;
    return info->pat_ctx.pid_list_num;
}

static int set_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || info->status == PARSER_STATUS_NON_PARSING )
        return -1;
    /* setup disable flag. */
    for( int32_t i = 0; i < info->pat_ctx.pid_list_num; ++i )
    {
        info->pat_ctx.pid_list[i].disable_pid = 1;
        for( int32_t j = 0; j < sid_info_num && info->pat_ctx.pid_list[i].disable_pid; ++j )
            if( info->pat_ctx.pid_list[i].program_number == sid_info[j].service_id )
                info->pat_ctx.pid_list[i].disable_pid = 0;
    }
    return 0;
}

static int32_t get_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num )
{
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info || info->status == PARSER_STATUS_NON_PARSING )
        return -1;
    if( sid_info_num == 0 )
    {
        /* return active PMT info. */
        sid_info[0].service_id     = info->pat_ctx.pid_list[info->pmt_ctx_index].program_number;
        sid_info[0].pmt_program_id = info->pat_ctx.pid_list[info->pmt_ctx_index].program_id;
        sid_info[0].pcr_program_id = info->pmt_ctx[info->pmt_ctx_index].pcr_program_id;
        return 1;
    }
    /* setup. */
    int32_t list_num = (sid_info_num < info->pat_ctx.pid_list_num) ? sid_info_num : info->pat_ctx.pid_list_num;
    for( int32_t i = 0; i < list_num; ++i )
    {
        sid_info[i].service_id     = info->pat_ctx.pid_list[i].program_number;
        sid_info[i].pmt_program_id = info->pat_ctx.pid_list[i].program_id;
        sid_info[i].pcr_program_id = info->pmt_ctx[i].pcr_program_id;
    }
    return list_num;
}

static int parse( void *ih )
{
    mapi_log( LOG_LV2, "[mpegts_parser] %s()\n", __func__ );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return -1;
    if( info->status == PARSER_STATUS_PARSED )
        release_all_handle( info );
    int result = -1;
    int64_t start_position = mpegts_ftell( &(info->tsf_ctx) );
    if( mpegts_parse_pat( info ) )
        goto end_parse;
    if( mpegts_parse_cat( info ) )
        mapi_log( LOG_LV2, "[check] CAT packet was not detected.\n" );
    result = parse_pmt_info( info );
    if( result )
        goto end_parse;
    mpegts_file_seek( &(info->tsf_ctx), start_position, MPEGTS_SEEK_RESET );
    result = set_pmt_stream_info( info );
end_parse:
    if( !result )
        info->status = PARSER_STATUS_PARSED;
    else
        mapi_log( LOG_LV2, "[mpegts_parser] failed to parse.\n" );
    mpegts_file_seek( &(info->tsf_ctx), start_position, MPEGTS_SEEK_RESET );
    return result;
}

static void *initialize( const char *input_file, int64_t buffer_size )
{
    mapi_log( LOG_LV2, "[mpegts_parser] %s()\n", __func__ );
    mpegts_info_t          *info            = (mpegts_info_t *)calloc( 1, sizeof(mpegts_info_t) );
    char                   *mpegts          = strdup( input_file );
    mpeg_descriptor_info_t *descriptor_info = (mpeg_descriptor_info_t *)calloc( 1, sizeof(mpeg_descriptor_info_t) );
    if( !info || !mpegts || !descriptor_info )
        goto fail_initialize;
    if( mpegts_open( &(info->tsf_ctx), mpegts, buffer_size ) )
        goto fail_initialize;
    /* check file size. */
    int64_t file_size = mpegts_get_file_size( &(info->tsf_ctx) );
    /* initialize. */
    info->mpegts                         = mpegts;
    info->file_size                      = file_size;
    info->buffer_size                    = buffer_size;
    info->tsf_ctx.packet_size            = TS_PACKET_SIZE;
    info->tsf_ctx.sync_byte_position     = -1;
    info->tsf_ctx.read_position          = 0;
    info->tsf_ctx.ts_packet_length       = TS_PACKET_SIZE;
    info->tsf_ctx.packet_check_count_num = TS_PACKET_SEARCH_CHECK_COUNT_NUM;
    info->packet_check_retry_num         = TS_PACKET_SEARCH_RETRY_COUNT_NUM;
    info->specified_service_id           = 0;
    info->specified_pmt_program_id       = TS_PID_ERR;
    info->emm_program_id                 = TS_PID_ERR;
    info->descriptor_info                = descriptor_info;
    /* first check. */
    if( mpegts_first_check( &(info->tsf_ctx) ) )
        goto fail_initialize;
    return info;
fail_initialize:
    mapi_log( LOG_LV2, "[mpegts_parser] failed to initialize.\n" );
    if( descriptor_info )
    {
        mpeg_stream_release_descriptor_info( descriptor_info );
        free( descriptor_info );
    }
    if( mpegts )
        free( mpegts );
    if( info )
    {
        mpegts_close( &(info->tsf_ctx) );
        free( info );
    }
    return NULL;
}

static void release( void *ih )
{
    mapi_log( LOG_LV2, "[mpegts_parser] %s()\n", __func__ );
    mpegts_info_t *info = (mpegts_info_t *)ih;
    if( !info )
        return;
    /*  release. */
    mpeg_stream_release_descriptor_info( info->descriptor_info );
    free( info->descriptor_info );
    release_all_handle( info );
    mpegts_close( &(info->tsf_ctx) );
    free( info->mpegts );
    free( info );
}

mpeg_parser_t mpegts_parser = {
    initialize,
    release,
    parse,
    set_service_id,
    get_service_id_num,
    set_service_id_info,
    get_service_id_info,
    set_program_target,
    set_program_id,
    get_program_id,
    get_video_info,
    get_audio_info,
    get_pcr,
    get_stream_num,
    get_stream_data,
    get_specific_stream_data,
    get_sample_position,
    set_sample_position,
    seek_next_sample_position,
    get_sample_data,
    free_sample_buffer,
    get_sample_stream_type,
    get_stream_information
};
