/*****************************************************************************
 * mpeg_common.h
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
#ifndef __MPEG_COMMON_H__
#define __MPEG_COMMON_H__

#include <stdint.h>

#define MPEG_TIMESTAMP_MAX_VALUE        (0x00000001FFFFFFFFLL)
#define MPEG_TIMESTAMP_WRAPAROUND_VALUE (0x0000000200000000LL)
#define MPEG_TIMESTAMP_INVALID_VALUE    (0x8000000000000000LL)

#define CRC32_SIZE                      (4)

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
    OUTPUT_STREAM_NONE          = 0x00,
    OUTPUT_STREAM_VIDEO         = 0x01,
    OUTPUT_STREAM_AUDIO         = 0x02,
    OUTPUT_STREAM_BOTH_VA       = 0x03,
    OUTPUT_STREAM_NONE_PCR_ONLY = 0x04
} output_stream_type;

typedef enum {
    MPEG_READER_DEALY_NONE               = 0,
    MPEG_READER_DEALY_VIDEO_GOP_KEYFRAME = 1,
    MPEG_READER_DEALY_VIDEO_GOP_TR_ORDER = 2,
    MPEG_READER_DEALY_FAST_VIDEO_STREAM  = 3,
    MPEG_READER_DEALY_FAST_STREAM        = 4,
    MPEG_READER_DEALY_INVALID
} mpeg_reader_delay_type;

typedef enum {
    GET_INFO_KEY_ID = 0,
    GET_INFO_KEY_MAX
} get_information_key_type;

typedef enum {
    PMT_TARGET_MAX = 0,
    PMT_TARGET_MIN = 1
} pmt_target_type;

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
    STREAM_IS_DOLBY_AUDIO   = 0x4000 | STREAM_IS_AUDIO     ,
    STREAM_IS_DTS_AUDIO     = 0x8000 | STREAM_IS_AUDIO
} mpeg_stream_group_type;

typedef enum {
    /* ISO/IEC 13818-1 */
    STREAM_VIDEO_MPEG1      = 0x01,    /* ISO/IEC 11172 Video                                                                            */
    STREAM_VIDEO_MPEG2      = 0x02,    /* ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream */
    STREAM_AUDIO_MP1        = 0x03,    /* ISO/IEC 11172 Audio                                                                            */
    STREAM_AUDIO_MP2        = 0x04,    /* ISO/IEC 13818-3 Audio                                                                          */
    STREAM_PRIVATE_SECTION  = 0x05,    /* ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections                                          */
    STREAM_PES_PRIVATE_DATA = 0x06,    /* ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data                       */
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
    STREAM_AUDIO_DTS_HD     = 0x85,    /* DTS-HD                                                                                         */
    STREAM_AUDIO_DTS_HD_XLL = 0x86,    /* DTS-HD with XLL                                                                                */
    STREAM_AUDIO_DDPLUS     = 0x87,    /* DD+                                                                                            */
    STREAM_AUDIO_DDPLUS_SUB = 0xA1,    /* DD+ for secondary audio                                                                        */
    STREAM_AUDIO_DTS_HD_SUB = 0xA2,    /* DTS-HD LBR for secondary audio                                                                 */
    STREAM_VIDEO_VC1        = 0xFD,    /* VC-1 */
    STREAM_INVALID          = 0xFF
} mpeg_stream_type;

typedef enum {
    MPEG_VIDEO_UNKNOWN_FRAME = 0x00,
    MPEG_VIDEO_I_FRAME       = 0x01,
    MPEG_VIDEO_P_FRAME       = 0x02,
    MPEG_VIDEO_B_FRAME       = 0x03
} mpeg_video_frame_type;

typedef enum {
    MPEG_AUDIO_SPEAKER_FRONT_CENTER  = 0x0001,
    MPEG_AUDIO_SPEAKER_FRONT_LEFT    = 0x0002,
    MPEG_AUDIO_SPEAKER_FRONT_RIGHT   = 0x0004,
    MPEG_AUDIO_SPEAKER_REAR_SRROUND  = 0x0008,
    MPEG_AUDIO_SPEAKER_REAR_LEFT     = 0x0010,
    MPEG_AUDIO_SPEAKER_REAR_RIGHT    = 0x0020,
    MPEG_AUDIO_SPEAKER_OUTSIDE_LEFT  = 0x0040,
    MPEG_AUDIO_SPEAKER_OUTSIDE_RIGHT = 0x0080,
    MPEG_AUDIO_SPEAKER_REAR_CENTER   = 0x0100,
    MPEG_AUDIO_SPEAKER_REAR_LEFT2    = 0x0200,
    MPEG_AUDIO_SPEAKER_REAR_RIGHT2   = 0x0400,
    MPEG_AUDIO_SPEAKER_LFE_CHANNEL   = 0x1000,
    MPEG_AUDIO_SPEAKER_DUAL_MONO     = 0x2000,
    MPEG_AUDIO_SPEAKER_JOINT_STEREO  = 0x4000,
    MPEG_AUDIO_SPEAKER_OVERHEAD      = 0x8000,
    MPEG_AUDIO_SPEAKER_F_2CHANNELS   = MPEG_AUDIO_SPEAKER_FRONT_LEFT   | MPEG_AUDIO_SPEAKER_FRONT_RIGHT  ,
    MPEG_AUDIO_SPEAKER_R_2CHANNELS   = MPEG_AUDIO_SPEAKER_REAR_LEFT    | MPEG_AUDIO_SPEAKER_REAR_RIGHT   ,
    MPEG_AUDIO_SPEAKER_R2_2CHANNELS  = MPEG_AUDIO_SPEAKER_REAR_LEFT2   | MPEG_AUDIO_SPEAKER_REAR_RIGHT2  ,
    MPEG_AUDIO_SPEAKER_O_2CHANNELS   = MPEG_AUDIO_SPEAKER_OUTSIDE_LEFT | MPEG_AUDIO_SPEAKER_OUTSIDE_RIGHT,
    MPEG_AUDIO_SPEAKER_F_3CHANNELS   = MPEG_AUDIO_SPEAKER_FRONT_CENTER | MPEG_AUDIO_SPEAKER_F_2CHANNELS  ,
    MPEG_AUDIO_SPEAKER_R_3CHANNELS   = MPEG_AUDIO_SPEAKER_REAR_SRROUND | MPEG_AUDIO_SPEAKER_R_2CHANNELS
} mpeg_audio_speaker_mapping_type;

typedef struct {
    int64_t             pts;
    int64_t             dts;
} mpeg_timestamp_t;

typedef struct {
    int64_t             pcr;
    int64_t             start_pcr;
    int64_t             last_pcr;
} pcr_info_t;

typedef void (*get_stream_data_cb_func)( void *cb_params, void *cb_ret );

typedef struct {
    get_stream_data_cb_func  func;
    void                    *params;
} get_stream_data_cb_t;

typedef struct {
    mpeg_sample_type  sample_type;
    uint8_t           stream_number;
    uint8_t          *buffer;
    uint32_t          read_size;
    int32_t           read_offset;
    int64_t           progress;
} get_stream_data_cb_ret_t;

#define BYTE_DATA_SHIFT( data, size )           \
do {                                            \
    for( int i = 1; i < size; ++i )             \
        data[i - 1] = data[i];                  \
} while( 0 )

#endif /* __MPEG_COMMON_H__ */
