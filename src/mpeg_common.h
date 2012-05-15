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

#endif /* __MPEG_COMMON_H__ */
