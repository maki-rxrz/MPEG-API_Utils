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

#include "mpeg_common.h"
#include "mpeg_stream.h"

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data )
{
    return (int64_t)(time_stamp_data[0] & 0x0E) << 29
                  |  time_stamp_data[1]         << 22
                  | (time_stamp_data[2] & 0xFE) << 14
                  |  time_stamp_data[3]         << 7
                  | (time_stamp_data[4] & 0xFE) >> 1;
}

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_start_code_type start_code_type )
{
    static const uint8_t pes_start_code_common_head[PES_PACKET_START_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };
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
            { 0xFF, 0xFF },         /* Program Stream Directory     */
            /* ... */
            { 0xFF, 0xE2 },         /* MPEG-4 AVC Stream            */
            { 0xFF, 0x0F },         /* VC-1 Video Stream 1          */
            { 0xFF, 0x0D },         /* VC-1 Video Stream 2          */
            { 0xFF, 0xFD },         /* AC-3/DTS Audio Stream        */
        };
    if( memcmp( start_code, pes_start_code_common_head, PES_PACKET_START_CODE_SIZE - 1 ) )
        return -1;
    if( (start_code[PES_PACKET_START_CODE_SIZE - 1] & pes_stream_id_list[start_code_type].mask) != pes_stream_id_list[start_code_type].code )
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

extern mpeg_stream_group_type mpeg_stream_judge_type( uint8_t stream_type )
{
    mpeg_stream_group_type judge = STREAM_IS_UNKNOWN;
    switch( stream_type )
    {
        case STREAM_VIDEO_MPEG1 :
            judge = STREAM_IS_MPEG1_VIDEO;
            break;
        case STREAM_VIDEO_MPEG2 :
        case STREAM_VIDEO_MPEG2_A :
        case STREAM_VIDEO_MPEG2_B :
        case STREAM_VIDEO_MPEG2_C :
        case STREAM_VIDEO_MPEG2_D :
            judge = STREAM_IS_MPEG2_VIDEO;
            break;
        case STREAM_VIDEO_MP4 :
        case STREAM_VIDEO_AVC :
            judge = STREAM_IS_MPEG4_VIDEO;
            break;
        case STREAM_AUDIO_MP1 :
        case STREAM_AUDIO_MP2 :
        case STREAM_AUDIO_AAC :
            judge = STREAM_IS_MPEG_AUDIO;
            break;
        case STREAM_AUDIO_AC3 :
        case STREAM_AUDIO_DTS :
            judge = STREAM_IS_DOLBY_AUDIO;
            break;
        default :
            break;
    }
    return judge;
}

extern int32_t mpeg_stream_check_start_point( mpeg_stream_type stream_type, uint8_t *buffer, uint32_t buffer_size )
{
    int32_t start_point = -1;
    switch( stream_type )
    {
        case STREAM_AUDIO_MP1 :
        case STREAM_AUDIO_MP2 :
            for( int i = 0; i < buffer_size - STREAM_MPA_HEADER_CHECK_SIZE; ++i )
            {
                if( buffer[i+0] != 0xFF || (buffer[i+1] & 0xE0) != 0xE0 )
                    continue;
                int version_id = (buffer[i+1] & 0x18) >> 3;
                if( version_id == 0x01 )                    /* MPEG Audio version ID : '01' reserved            */
                    continue;
                int layer = (buffer[i+1] & 0x06) >> 1;
                if( !layer )                                /* layer : '00' reserved                            */
                    continue;
                /* protection_bit = buffer[i+1] & 0x01);            */
                int bitrate_index = buffer[i+2] >> 4;
                if( bitrate_index == 0x0F )                 /* bitrate index : '1111' bad                       */
                    continue;
                if( (buffer[i+2] & 0x0C) == 0x0C )          /* Sampling rate frequency index : '11' reserved    */
                    continue;
                /* check bitrate_index & channel_mode matrix. */
                static const uint8_t ng_matrix[2][4] =
                    {
                        {  1,  2,  3,  5 },                 /* stereo :  32 /  48 /  56 /  80 */
                        { 11, 12, 13, 14 }                  /* mono   : 224 / 256 / 320 / 384 */
                    };
                int channel_mode = (buffer[i+3] & 0x60) >> 5;
                int index = (channel_mode == 3) ? 1 : 0;    /* '11' single channel  */
                if( bitrate_index == ng_matrix[index][0] || bitrate_index == ng_matrix[index][1]
                 || bitrate_index == ng_matrix[index][2] || bitrate_index == ng_matrix[index][3] )
                    continue;
                /* detect start point. */
                start_point = i;
                dprintf( LOG_LV4, "[debug] [MPEG-Audio] check_buffer_size:%d  start_point:%d  len:%d\n", buffer_size, start_point );
                break;
            }
            break;
        case STREAM_AUDIO_AAC :
            for( int i = 0; i < buffer_size - STREAM_AAC_HEADER_CHECK_SIZE; ++i )
            {
                if( buffer[i+0] != 0xFF || (buffer[i+1] & 0xF0) != 0xF0 )
                    continue;
                if( buffer[i+1] & 0x06 )                    /* layer : allways 0                            */
                    continue;
                /* protection absent = buffer[i+1] & 0x01;          */
                if( (buffer[i+2] & 0xC0) == 0xC0 )          /* profile : '11' reserved                      */
                    continue;
                if( ((buffer[i+2] & 0x3C) >> 2) > 12 )      /* MPEG-4 sampling frequency index : 0-12       */
                    continue;
                int aac_frame_length = ((buffer[i+3] & 0x03) << 11) | (buffer[i+4] << 3) | (buffer[i+5] >> 5);
                if( !aac_frame_length )                     /* aac frame length                             */
                    continue;
                /* detect start point. */
                start_point = i;
                dprintf( LOG_LV4, "[debug] [ADTS-AAC] check_buffer_size:%d  start_point:%d  len:%d\n", buffer_size, start_point, aac_frame_length );
                break;
            }
            break;
        case STREAM_AUDIO_AC3 :
        case STREAM_AUDIO_DTS :
            start_point = 0;        // FIXME
            break;
        default :
            start_point = 0;
            break;
    }
    return start_point;
}
