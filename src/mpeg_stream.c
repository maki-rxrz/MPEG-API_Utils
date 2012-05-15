/*****************************************************************************
 * mpeg_stream.c
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

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data )
{
    return (int64_t)(time_stamp_data[0] & 0x0E) << 29
                  |  time_stamp_data[1]         << 22
                  | (time_stamp_data[2] & 0xFE) << 14
                  |  time_stamp_data[3]         << 7
                  | (time_stamp_data[4] & 0xFE) >> 1;
}

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_star_code_type start_code_type )
{
    static const uint8_t pes_start_code_common_head[PES_PACKET_STATRT_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };
    static const struct {
        uint8_t     mask;
        uint8_t     code;
    } pes_stream_id_list[PES_PACKET_START_CODE_MAX] =
        {
            /* MPEG-1/2 stream type. */
            { 0xBD, 0xFF },         /* Private Stream 1             */
            { 0xBE, 0xFF },         /* Padding Stream               */
            { 0xBF, 0xFF },         /* Private Stream 2             */
            { 0xF0, 0xE0 },         /* Video Stream                 */
            { 0xE0, 0xC0 },         /* Audio Stream                 */
            { 0xF3, 0xFF },         /* MHEG Reserved                */
            /* MPEG-2 stream type. */
            { 0xBC, 0xFF },         /* Program Stream Map           */
            { 0xF0, 0xFF },         /* License Management Message 1 */
            { 0xF1, 0xFF },         /* License Management Message 2 */
            { 0xF2, 0xFF },         /* DSM Control Command          */
            { 0xF4, 0xFC },         /* ITU-T Reserved               */
            { 0xF8, 0xFF },         /* ITU-T Reserved               */
            { 0xF9, 0xFF },         /* PS Trasnport on TS           */
            { 0xFF, 0xFF }          /* Program Stream Directory     */
        };
    if( start_code[0] != pes_start_code_common_head[0] )
        return -1;
    if( memcmp( start_code, pes_start_code_common_head, PES_PACKET_STATRT_CODE_SIZE - 1 ) )
        return -1;
    if( (start_code[PES_PACKET_STATRT_CODE_SIZE - 1] & pes_stream_id_list[start_code_type].mask) != pes_stream_id_list[start_code_type].code )
        return -1;
    return 0;
}

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info )
{
    pes_info->packet_length             = ((buf[0] << 8) | buf[1]);
    /* reserved '10'    2bit            = (buf[2] & 0xC0) >> 6; */
    pes_info->scrambe_control           = (buf[2] & 0x30) >> 4;
    pes_info->priority                  = !!(buf[2] & 0x08);
    pes_info->data_alignment            = !!(buf[2] & 0x04);
    pes_info->copyright                 = !!(buf[2] & 0x02);
    pes_info->original_or_copy          = !!(buf[2] & 0x01);
    pes_info->pts_flag                  = !!(buf[3] & 0x80);
    pes_info->dts_flag                  = !!(buf[3] & 0x40);
    pes_info->escr_flag                 = !!(buf[3] & 0x20);
    pes_info->es_rate_flag              = !!(buf[3] & 0x10);
    pes_info->dsm_trick_mode_flag       = !!(buf[3] & 0x08);
    pes_info->additional_copy_info_flag = !!(buf[3] & 0x04);
    pes_info->crc_flag                  = !!(buf[3] & 0x02);
    pes_info->extention_flag            = !!(buf[3] & 0x01);
    pes_info->header_length             = buf[4];
}

extern int mpeg_video_check_start_code( uint8_t *start_code, mpeg_video_star_code_type start_code_type )
{
    static const uint8_t mpeg_video_start_code[MPEG_VIDEO_START_CODE_MAX][MPEG_VIDEO_STATRT_CODE_SIZE] =
        {
            /* Sequence Hedaer */
            { 0x00, 0x00, 0x01, 0xB3 },         /* Sequence Hreader Code    */
            { 0x00, 0x00, 0x01, 0xB5 },         /* Extension Start Code     */
            { 0x00, 0x00, 0x01, 0xB2 },         /* User Data Start Code     */
            { 0x00, 0x00, 0x01, 0xB7 },         /* Sequence End Code        */
            /* Picture Hedaer */
            { 0x00, 0x00, 0x01, 0xB8 },         /* GOP Start Code           */
            { 0x00, 0x00, 0x01, 0x00 },         /* Picture Start Code       */
            /* Slice Hedaer */
            { 0x00, 0x00, 0x01, 0xAF }          /* Slice Start Code         */
        };
    if( start_code[0] != mpeg_video_start_code[start_code_type][0] )
        return -1;
    if( memcmp( start_code, &(mpeg_video_start_code[start_code_type][0]), PES_PACKET_STATRT_CODE_SIZE ) )
        return -1;
    return 0;
}

static void mpeg_video_read_sequence_section( uint8_t *data, mpeg_video_sequence_section_t *sequence )
{
    sequence->horizontal_size          =  (data[0] << 4) | ((data[1] & 0xF0) >> 4);
    sequence->vertical_size            = ((data[1] & 0x0F) << 8) | (data[2]);
    sequence->aspect_ratio_information =  (data[3] & 0xF0) >> 4;
    sequence->frame_rate_code          =   data[3] & 0x0F;
}

static void mpeg_video_read_gop_section( uint8_t *data, mpeg_video_gop_section_t *gop )
{
//    gop->time_code   = (data[0] << 17) | (data[1] << 9) | (data[2] << 1) |  | ((data[3] & 0x80) >> 7);
    gop->closed_gop  = !!(data[3] & 0x40);
    gop->broken_link = !!(data[3] & 0x20);
}

static void mpeg_video_read_picture_section( uint8_t *data, mpeg_video_picture_section_t *picture )
{
    picture->temporal_reference  = (data[0] << 2) | ((data[1] & 0xC0) >> 6);
    picture->picture_coding_type = (data[1] & 0x38) >> 3;
}

extern void mpeg_video_get_section_info( uint8_t *buf, mpeg_video_star_code_type start_code, mpeg_video_info_t *video_info )
{
    switch( start_code )
    {
        case MPEG_VIDEO_START_CODE_SHC :
            mpeg_video_read_sequence_section( buf, &(video_info->sequence) );
            break;
        case MPEG_VIDEO_START_CODE_ESC :
            break;
        case MPEG_VIDEO_START_CODE_UDSC :
            break;
        case MPEG_VIDEO_START_CODE_SEC :
            break;
        case MPEG_VIDEO_START_CODE_GSC :
            mpeg_video_read_gop_section( buf, &(video_info->gop) );
            break;
        case MPEG_VIDEO_START_CODE_PSC :
            mpeg_video_read_picture_section( buf, &(video_info->picture) );
            break;
        case MPEG_VIDEO_START_CODE_SSC :
            break;
        default :
            break;
    }
}
