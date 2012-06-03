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

#define MPEG_TIMESTAMP_MAX_VALUE        (0x1FFFFFFFFLL)

typedef enum {
    SAMPLE_TYPE_VIDEO,
    SAMPLE_TYPE_AUDIO,
    SAMPLE_TYPE_MAX
} mpeg_sample_type;

typedef enum {
    GET_SAMPLE_DATA_CONTAINER,
    GET_SAMPLE_DATA_PES_PACKET,
    GET_SAMPLE_DATA_RAW
} get_sample_data_mode;

typedef enum {
    STREAM_IS_UNKNOWN       = 0x0000                       ,
    STREAM_IS_VIDEO         = 0x0001                       ,
    STREAM_IS_MPEG_VIDEO    = 0x0002 | STREAM_IS_VIDEO     ,
    STREAM_IS_MPEG1_VIDEO   =          STREAM_IS_MPEG_VIDEO,
    STREAM_IS_MPEG2_VIDEO   = 0x0004 | STREAM_IS_MPEG_VIDEO,
    STREAM_IS_MPEG4_VIDEO   = 0x0008 | STREAM_IS_VIDEO     ,
    STREAM_IS_PRIVATE_VIDEO = 0x0010 | STREAM_IS_VIDEO     ,
    STREAM_IS_AUDIO         = 0x0100                       ,
    STREAM_IS_MPEG_AUDIO    = 0x0200 | STREAM_IS_AUDIO     ,
    STREAM_IS_MPEG1_AUDIO   = 0x0400 | STREAM_IS_MPEG_AUDIO,
    STREAM_IS_MPEG2_AUDIO   = 0x0800 | STREAM_IS_MPEG_AUDIO,
    STREAM_IS_AAC_AUDIO     = 0x1000 | STREAM_IS_MPEG_AUDIO,
    STREAM_IS_PCM_AUDIO     = 0x2000 | STREAM_IS_AUDIO     ,
    STREAM_IS_DOLBY_AUDIO   = 0x8000 | STREAM_IS_AUDIO
} mpeg_stream_group_type;

typedef enum {
    /* ISO/IEC 13818-1 */
    STREAM_VIDEO_MPEG1      = 0x01,    /* ISO/IEC 11172 Video                                                                            */
    STREAM_VIDEO_MPEG2      = 0x02,    /* ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream */
    STREAM_AUDIO_MP1        = 0x03,    /* ISO/IEC 11172 Audio                                                                            */
    STREAM_AUDIO_MP2        = 0x04,    /* ISO/IEC 13818-3 Audio                                                                          */
    STREAM_VIDEO_MPEG2_A    = 0x0A,    /* ISO/IEC 13818-6 type A                                                                         */
    STREAM_VIDEO_MPEG2_B    = 0x0B,    /* ISO/IEC 13818-6 type B                                                                         */
    STREAM_VIDEO_MPEG2_C    = 0x0C,    /* ISO/IEC 13818-6 type C                                                                         */
    STREAM_VIDEO_MPEG2_D    = 0x0D,    /* ISO/IEC 13818-6 type D                                                                         */
    STREAM_AUDIO_AAC        = 0x0F,    /* ISO/IEC 13818-7 Audio with ADTS transport syntax                                               */
    /* User Private */
    STREAM_VIDEO_MP4        = 0x10,    /* ISO/IEC 14496-2 Visual                                                                         */
    STREAM_AUDIO_MP4        = 0x11,    /* ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1     */
    STREAM_VIDEO_AVC        = 0x1B,    /* ISO/IEC 14496-10                                                                               */
    STREAM_VIDEO_PRIVATE    = 0x80,    /* Private Video or Linear PCM                                                                    */
    STREAM_AUDIO_LPCM       = 0x80,
    STREAM_AUDIO_AC3_DTS    = 0x81,    /* AC-3 or DTS                                                                                    */
    STREAM_AUDIO_AC3        = 0x81,
    STREAM_AUDIO_DTS        = 0x82,    /* DTS                                                                                            */
    STREAM_AUDIO_MLP        = 0x83,    /* MLP                                                                                            */
    STREAM_AUDIO_DDPLUS     = 0x84,    /* DD+                                                                                            */
    STREAM_AUDIO_DTS_HD     = 0x85,    /* DTS-HD                                                                                         */
    STREAM_AUDIO_DTS_HD_XLL = 0x86,    /* DTS-HD with XLL                                                                                */
    STREAM_AUDIO_DDPLUS_SUB = 0xA1,    /* DD+ for secondary audio                                                                        */
    STREAM_AUDIO_DTS_HD_SUB = 0xA2,    /* DTS-HD LBR for secondary audio                                                                 */
    STREAM_VIDEO_VC1        = 0xFD,    /* VC-1 */
    STREAM_INVAILED         = 0xFF
} mpeg_stream_type;

typedef enum {
    MPEG_VIDEO_UNKNOWN_FRAME = 0x00,
    MPEG_VIDEO_I_FRAME       = 0x01,
    MPEG_VIDEO_P_FRAME       = 0x02,
    MPEG_VIDEO_B_FRAME       = 0x03
} mpeg_video_frame_type;

#endif /* __MPEG_COMMON_H__ */
