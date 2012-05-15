/*****************************************************************************
 * mpegts_utils.h
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
#ifndef __MPEGTS_UTILS_H__
#define __MPEGTS_UTILS_H__

#include "mpeg_common.h"

typedef enum {
    MPEG_VIDEO_I_FRAME = 0x01,
    MPEG_VIDEO_P_FRAME = 0x10,
    MPEG_VIDEO_B_FRAME = 0x11
} mpeg_video_frame_type;

typedef struct {
    uint8_t         stream_type;
    uint16_t        program_id;
} mpegts_pid_in_pmt_t;

typedef struct {
    FILE                   *input;
    uint8_t                 video_stream_type;
    uint8_t                 audio_stream_type;
    int32_t                 packet_size;
    int32_t                 sync_byte_position;
    int64_t                 read_position;
    int32_t                 pid_list_num_in_pat;
    uint16_t               *pid_list_in_pat;
    int32_t                 pid_list_num_in_pmt;
    mpegts_pid_in_pmt_t    *pid_list_in_pmt;
    uint32_t                packet_check_count_num;
    uint32_t                packet_check_retry_num;
    uint16_t                pmt_program_id;
    uint16_t                pcr_program_id;
    int64_t                 pcr;
    int64_t                 video_key_pts;
    int64_t                 video_pts;
    int64_t                 video_dts;
    int64_t                 audio_pts;
    int64_t                 audio_dts;
    mpeg_video_frame_type   video_frame_type;
    int8_t                  video_order_in_gop;
} mpegts_info_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int mpegts_api_get_info( mpegts_info_t *info );

extern void mpegts_api_initialize_info( mpegts_info_t *info, FILE *input );

extern void mpegts_api_release_info( mpegts_info_t *info );

#ifdef __cplusplus
}
#endif

#endif /* __MPEGTS_UTILS_H__ */
