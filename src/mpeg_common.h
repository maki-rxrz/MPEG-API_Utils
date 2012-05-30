/*****************************************************************************
 * mpeg_common.h
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
#ifndef __MPEG_COMMON_H__
#define __MPEG_COMMON_H__

#define MPEG_TIMESTMAP_MAX_VALUE        (0x1FFFFFFFFLL)

typedef enum {
    SAMPLE_TYPE_VIDEO,
    SAMPLE_TYPE_AUDIO
} mpeg_sample_type;

typedef enum {
    GET_SAMPLE_DATA_CONTAINER,
    GET_SAMPLE_DATA_PES_PACKET,
    GET_SAMPLE_DATA_RAW,
    GET_SAMPLE_DATA_RAW_SEARCH_HEAD
} get_sample_data_mode;

enum {
    STREAM_IS_UNKNOWN     = 0x00,
    STREAM_IS_VIDEO       = 0x01,
    STREAM_IS_MPEG_VIDEO  = 0x03,
    STREAM_IS_MPEG1_VIDEO = 0x03,
    STREAM_IS_MPEG2_VIDEO = 0x07,
    STREAM_IS_MPEG4_VIDEO = 0x09,
    STREAM_IS_AUDIO       = 0x10,
    STREAM_IS_MPEG_AUDIO  = 0x30,
    STREAM_IS_DOLBY_AUDIO = 0x50
};

typedef enum {
    STREAM_VIDEO_MPEG1   = 0x01,    /* ISO/IEC 11172 Video                                                                            */
    STREAM_VIDEO_MPEG2   = 0x02,    /* ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream */
    STREAM_AUDIO_MP1     = 0x03,    /* ISO/IEC 11172 Audio                                                                            */
    STREAM_AUDIO_MP2     = 0x04,    /* ISO/IEC 13818-3 Audio                                                                          */
    STREAM_VIDEO_MPEG2_A = 0x0A,    /* ISO/IEC 13818-6 type A                                                                         */
    STREAM_VIDEO_MPEG2_B = 0x0B,    /* ISO/IEC 13818-6 type B                                                                         */
    STREAM_VIDEO_MPEG2_C = 0x0C,    /* ISO/IEC 13818-6 type C                                                                         */
    STREAM_VIDEO_MPEG2_D = 0x0D,    /* ISO/IEC 13818-6 type D                                                                         */
    STREAM_AUDIO_AAC     = 0x0F,    /* ISO/IEC 13818-7 Audio with ADTS transport syntax                                               */
    STREAM_VIDEO_MP4     = 0x10,    /* ISO/IEC 14496-2 Visual                                                                         */
    STREAM_AUDIO_MP4     = 0x11,    /* ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1     */
    STREAM_VIDEO_AVC     = 0x1B,    /* ISO/IEC 14496-10 */
    STREAM_AUDIO_AC3     = 0x81,    /* ... */
    STREAM_AUDIO_DTS     = 0xEF,    /* ... */
    STREAM_VIDEO_VC1     = 0xFD,    /* ... */
    STREAM_INVAILED      = 0xFF
} mpeg_stream_type;

typedef enum {
    MPEG_VIDEO_UNKNOWN_FRAME = 0x00,
    MPEG_VIDEO_I_FRAME       = 0x01,
    MPEG_VIDEO_P_FRAME       = 0x02,
    MPEG_VIDEO_B_FRAME       = 0x03
} mpeg_video_frame_type;

#endif /* __MPEG_COMMON_H__ */
