/*****************************************************************************
 * mpegts_def.h
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
#ifndef __MPEGTS_DEF_H__
#define __MPEGTS_DEF_H__

#include <stdint.h>

#define MPEGTS_ILLEGAL_PROGRAM_ID_MASK      (0xE000)

typedef enum {
    TS_PID_PAT = 0x0000,
    TS_PID_CAT = 0x0001,
    TS_PID_NIT = 0x0010,
    TS_PID_SDT = 0x0011,
    TS_PID_BAT = 0x0011,
    TS_PID_EIT = 0x0012,
    /* ERR: 0x1FFF over */
    TS_PID_ERR = 0xFFFF
} mpegts_pid_type;

typedef enum {
    /***************************************************************
     * 0x00     :  program_association_section
     * 0x01     :  conditional_access_section
     * 0x02     :  TS_program_map_section
     * 0x03     :  TS_description_section
     * 0x04     :  ISO_IEC_14496_scene_description_section
     * 0x05     :  ISO_IEC_14496_object_descriptor_section
     * 0x06-0x37:  ITU-T Rec. H.222.0 | ISO/IEC 13818-1 reserved
     * 0x38-0x3F:  Defined in ISO/IEC 13818-6
     * 0x40-0xFE:  User private
     * 0xFF     :  forbidden
     **************************************************************/
    /*******************************************
     * ARIB STD-B10  TS packet
     ******************************************/
    PSI_TABLE_ID_PAT      = 0x00,
    PSI_TABLE_ID_CAT      = 0x01,
    PSI_TABLE_ID_PMT      = 0x02,
    /* 0x3A-0x3F : DSM-CC Section */
    PSI_TABLE_ID_NIT      = 0x40,
    PSI_TABLE_ID_NIT2     = 0x41,
    PSI_TABLE_ID_SDT      = 0x42,
    PSI_TABLE_ID_SDT2     = 0x46,
    PSI_TABLE_ID_BAT      = 0x4A,
    PSI_TABLE_ID_EIT      = 0x4E,
    PSI_TABLE_ID_EIT2     = 0x4F,
    PSI_TABLE_ID_TDT      = 0x70,
    PSI_TABLE_ID_RST      = 0x71,
    PSI_TABLE_ID_ST       = 0x72,
    PSI_TABLE_ID_TOT      = 0x73,
    PSI_TABLE_ID_AIT      = 0x74,
    PSI_TABLE_ID_DIT      = 0x7E,
    PSI_TABLE_ID_SIT      = 0x7F,
    PSI_TABLE_ID_ECM      = 0x82,
    PSI_TABLE_ID_ECM_S    = 0x83,
    PSI_TABLE_ID_EMM      = 0x84,
    PSI_TABLE_ID_EMM_S    = 0x85,
    PSI_TABLE_ID_DCT      = 0xC0,
    PSI_TABLE_ID_DLT      = 0xC1,
    PSI_TABLE_ID_PCAT     = 0xC2,
    PSI_TABLE_ID_SDTT     = 0xC3,
    PSI_TABLE_ID_BIT      = 0xC4,
    PSI_TABLE_ID_NBIT     = 0xC5,
    PSI_TABLE_ID_NBIT_RI  = 0xC6,
    PSI_TABLE_ID_LDT      = 0xC7,
    PSI_TABLE_ID_CDT      = 0xC8,
    PSI_TABLE_ID_LIT      = 0xD0,
    PSI_TABLE_ID_ERT      = 0xD1,
    PSI_TABLE_ID_ITT      = 0xD2,
    /* 0x90-0xBF : User private */
    /*******************************************
     * ARIB STD-B10  TLV packet
     ******************************************/
    PSI_TABLE_ID_TLV_NIT  = 0x40,
    PSI_TABLE_ID_TLV_NIT2 = 0x41,
    PSI_TABLE_ID_TLV_TDT  = 0x70,
    PSI_TABLE_ID_TLV_TOT  = 0x71,
    PSI_TABLE_ID_TLV_AMT  = 0xFE,
    /******************************************/
    PSI_TABLE_ID_INVALID  = 0xFF
} mpegts_psi_section_table_id;

typedef struct {
    uint8_t         sync_byte;
    uint8_t         transport_error_indicator;
    uint8_t         payload_unit_start_indicator;
    uint8_t         transport_priority;
    uint16_t        program_id;
    uint8_t         transport_scrambling_control;
    uint8_t         adaptation_field_control;
    uint16_t        continuity_counter;
} mpegts_packet_header_t;

typedef struct {
    uint8_t         discontinuity_indicator;
    uint8_t         random_access_indicator;
    uint8_t         elementary_stream_priority_indicator;
    uint8_t         pcr_flag;
    uint8_t         opcr_flag;
    uint8_t         splicing_point_flag;
    uint8_t         transport_private_data_flag;
    uint8_t         adaptation_field_extension_flag;
    uint8_t         splice_countdown;
    uint8_t         transport_private_data_length;
    uint8_t         private_data_byte;
    uint8_t         adaptation_field_extension_length;
    uint8_t         ltw_flag;
    uint8_t         piecewise_rate_flag;
    uint8_t         seamless_splice_flag;
    uint8_t         ltw_valid_flag;
    uint8_t         ltw_offset;
    uint16_t        piecewise_rate;
    uint8_t         splice_type;
    uint64_t        dts_next_au;
} mpegts_adaptation_field_header_t;

typedef struct {
    uint8_t         table_id;
    uint8_t         section_syntax_indicator;
    uint16_t        section_length;
    uint16_t        id_number;
    uint8_t         version_number;
    uint8_t         current_next_indicator;
    uint8_t         section_number;
    uint8_t         last_section_number;
    /* ... */
} mpegts_table_common_info_t;

typedef struct {
    uint8_t         table_id;
    uint8_t         section_syntax_indicator;
    uint16_t        section_length;
    uint16_t        transport_stream_id;
    uint8_t         version_number;
    uint8_t         current_next_indicator;
    uint8_t         section_number;
    uint8_t         last_section_number;
} mpegts_pat_section_info_t;

typedef struct {
    uint8_t         table_id;
    uint8_t         section_syntax_indicator;
    uint16_t        section_length;
    uint16_t        program_number;
    uint8_t         version_number;
    uint8_t         current_next_indicator;
    uint8_t         section_number;
    uint8_t         last_section_number;
    uint16_t        pcr_program_id;
    uint16_t        program_info_length;
} mpegts_pmt_section_info_t;

typedef union {
    mpegts_table_common_info_t      common;
    mpegts_pat_section_info_t       pat;
    mpegts_pmt_section_info_t       pmt;
} mpegts_table_info_t;

#endif /* __MPEGTS_DEF_H__ */
