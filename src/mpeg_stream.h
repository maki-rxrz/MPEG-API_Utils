/*****************************************************************************
 * mpeg_stream.h
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
#ifndef __MPEG_STREAM_H__
#define __MPEG_STREAM_H__

#define PES_PACKET_STATRT_CODE_SIZE                 (4)
#define PES_PACKET_HEADER_CHECK_SIZE                (5)
#define PES_PACKET_PTS_DTS_DATA_SIZE                (10)

typedef enum {
    STREAM_VIDEO_MPEG1 = 0x01,      /* ISO/IEC 11172 Video                                  */
    STREAM_VIDEO_MPEG2 = 0x02,      /* ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC  */
                                    /*           11172-2 constrained parameter video stream */
    STREAM_AUDIO_MP1   = 0x03,      /* ISO/IEC 11172 Audio                                  */
    STREAM_AUDIO_MP2   = 0x04,      /* ISO/IEC 13818-3 Audio                                */
    STREAM_AUDIO_AAC   = 0x0F,      /* ISO/IEC 13818-7 Audio with ADTS transport syntax     */
#if 0
    STREAM_AUDIO_AC3   = 0x81,      /* ... */
    STREAM_AUDIO_DTS   = 0xEF,      /* ... */
#endif
    STREAM_INVAILED    = 0xFF
} mpeg_stream_type;

typedef enum {
    /* MPEG-1/2 stream type. */
    PES_PACKET_START_CODE_PRIVATE_STREAM_1             ,
    PES_PACKET_START_CODE_PADDING_STREAM               ,
    PES_PACKET_START_CODE_PRIVATE_STREAM_2             ,
    PES_PACKET_START_CODE_VIDEO_STREAM                 ,
    PES_PACKET_START_CODE_AUDIO_STREAM                 ,
    PES_PACKET_START_CODE_MHEG_RESRVED                 ,
    /* MPEG-2 stream type. */
    PES_PACKET_START_CODE_PROGRAM_STERAM_MAP           ,
    PES_PACKET_START_CODE_LICENSE_MANAGEMENT_MAMESSAGE1,
    PES_PACKET_START_CODE_LICENSE_MANAGEMENT_MAMESSAGE2,
    PES_PACKET_START_CODE_DSM_CONTROL_COMAND           ,
    PES_PACKET_START_CODE_ITU_T_RESERVED1              ,
    PES_PACKET_START_CODE_ITU_T_RESERVED2              ,
    PES_PACKET_START_CODE_PS_TRANSPORT_ON_TS           ,
    PES_PACKET_START_CODE_PROGRAM_STREAM_DIRECTORY     ,
    PES_PACKET_START_CODE_MAX
} mpeg_pes_packet_star_code_type;

typedef struct {
    uint16_t        packet_length;
    /* reserved '10'    2bit */
    uint8_t         scrambe_control;
    uint8_t         priority;
    uint8_t         data_alignment;
    uint8_t         copyright;
    uint8_t         original_or_copy;
    uint8_t         pts_flag;
    uint8_t         dts_flag;
    uint8_t         escr_flag;
    uint8_t         es_rate_flag;
    uint8_t         dsm_trick_mode_flag;
    uint8_t         additional_copy_info_flag;
    uint8_t         crc_flag;
    uint8_t         extention_flag;
    uint8_t         header_length;
} mpeg_pes_header_info_t;

#define MPEG_VIDEO_STATRT_CODE_SIZE                 (4)

#define MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE     (4)
#define MPEG_VIDEO_GOP_SECTION_HEADER_SIZE          (4)
#define MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE      (12)

typedef enum {
    MPEG_VIDEO_START_CODE_SHC  = 0,         /* Sequence Header Code */
    MPEG_VIDEO_START_CODE_ESC  = 1,         /* Extension Start Code */
    MPEG_VIDEO_START_CODE_UDSC = 2,         /* User Data Start Code */
    MPEG_VIDEO_START_CODE_SEC  = 3,         /* Sequence End Code    */
    MPEG_VIDEO_START_CODE_GSC  = 4,         /* Group Start Code     */
    MPEG_VIDEO_START_CODE_PSC  = 5,         /* Picture Start Code   */
    MPEG_VIDEO_START_CODE_SSC  = 6,         /* Slice Start Code     */
    MPEG_VIDEO_START_CODE_MAX
} mpeg_video_star_code_type;

typedef struct {
    uint16_t        horizontal_size;
    uint16_t        vertical_size;
    uint8_t         aspect_ratio_information;
    uint8_t         frame_rate_code;
    uint32_t        bit_rate;
    uint8_t         marker_bit;
    uint16_t        vbv_buffer_size;
    uint8_t         constrained_parameter_flag;
//    uint8_t         load_intra_quantizer_matrix;
//    uint8_t         load_non_intra_quantizer_matrix;
    uint8_t         profile_and_level_indication;
    uint8_t         progressive_sequence;
    uint8_t         chroma_format;
    uint8_t         horizontal_size_extension;
    uint8_t         vertical_size_extension;
    uint8_t         bit_rate__extension;
    uint8_t         vbv_buffer_extension;
    uint8_t         low_delay;
    uint8_t         frame_rate_extension_n;
    uint8_t         frame_rate_extension_d;
//    uint16_t        display_horizontal_size;
//    uint16_t        display_vertical_size;
} mpeg_video_sequence_section_t;

typedef struct {
    uint32_t        time_code;
    uint8_t         closed_gop;
    uint8_t         broken_link;
} mpeg_video_gop_section_t;

typedef struct {
    uint16_t        temporal_reference;
    uint8_t         picture_coding_type;
    uint16_t        vbv_delay;
//    uint8_t         full_pel_forward_evctor;
//    uint8_t         forward_f_code;
//    uint8_t         full_pel_backword_evctor;
//    uint8_t         backward_f_code;
//    uint8_t         extra_bit_picutre;
//    uint8_t         extension_start_code_identifier;
//    uint8_t         forward_horizontal_f_code;
//    uint8_t         forward_vertical_f_code;
//    uint8_t         backward_horizontal_f_code;
//    uint8_t         backward_vertical_f_code;
    uint8_t         intra_dc_precision;
    uint8_t         picture_structure;
    uint8_t         top_field_first;
    uint8_t         frame_predictive_frame_dct;
    uint8_t         concealment_motion_vectors;
    uint8_t         q_scale_type;
    uint8_t         intra_vlc_format;
    uint8_t         alternate_scan;
    uint8_t         repeat_first_field;
    uint8_t         chroma_420_type;
    uint8_t         composite_display_flag;
    uint8_t         progressive_frame;
//    uint8_t         load_intra_quantizer_matrix;
//    uint8_t         load_non_intra_quantizer_matrix;
//    uint8_t         load_chroma_intra_quantizer_matrix;
//    uint8_t         load_chroma_non_intra_quantizer_matrix;
//    uint16_t        frame_center_horizontal_offset;
//    uint16_t        frame_center_vertical_offset;
} mpeg_video_picture_section_t;

#if 0
typedef struct {
    uint8_t         slice_vertical_position_extension;
    uint8_t         priority_break_point;
    uint8_t         quantize_scale_code;
    uint8_t         intra_slice;
} mpeg_video_slice_section_t;

typedef struct {
} mpeg_video_macroblock_section_t;
#endif

typedef struct {
    mpeg_video_sequence_section_t       sequence;
    mpeg_video_gop_section_t            gop;
    mpeg_video_picture_section_t        picture;
#if 0
    mpeg_video_slice_section_t          slice;
    mpeg_video_macroblock_section_t     mb;
#endif
} mpeg_video_info_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data );

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_star_code_type start_code_type );

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info );

extern int mpeg_video_check_start_code( uint8_t *start_code, mpeg_video_star_code_type start_code_type );

extern void mpeg_video_get_section_info( uint8_t *buf, mpeg_video_star_code_type start_code, mpeg_video_info_t *video_info );

#ifdef __cplusplus
}
#endif

#endif /* __MPEG_STREAM_H__ */
