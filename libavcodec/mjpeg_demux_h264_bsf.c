/*
 * MJPEG APP4 H264 demux bitstream filter
 * Copyright (c) 2020 Artem Pylypchuk
 *
 * Parts of this file are copied from guvcview project
 * guvcview              http://guvcview.sourceforge.net
 * Copyright Paulo Assis <pj.assis@gmail.com>                                    #
 *           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * MJPEG APP4 H264 demux bitstream filter
 * demuxes H264 video from UVC webcamera MJPEG stream
 */

#include "avcodec.h"
#include "bsf.h"
#include "bytestream.h"
#include "mjpeg.h"


static int mjpeg_demux_h264(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = av_new_packet(out, in->size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    //original args
    uint8_t *h264_data = out->data;
    uint8_t *buff = in->data;
    int size = in->size;

    // original vars
    uint8_t *sp = NULL;
    uint8_t *spl= NULL;
    uint8_t *epl= NULL;
    uint8_t *header = NULL;
    uint8_t *ph264 = h264_data;

    //search for first APP4 marker
    for(sp = buff; sp < buff + size - 2; ++sp)
    {
      if(sp[0] == 0xFF &&
         sp[1] == APP4)
      {
        spl = sp + 2; //exclude APP4 marker
        break;
      }

//      av_log(ctx, AV_LOG_ERROR, "could not find APP4 marker in bitstream\n");
//goto fail
    }

    /*(in big endian)
    *includes payload size + header + 6 bytes(2 length + 4 payload size)
    */
    uint16_t length = 0;
    length  = (uint16_t) spl[0] << 8;
    length |= (uint16_t) spl[1];

    header = spl + 2;
    /*in litle endian*/
    uint16_t header_length = header[2];
    header_length |= header[3] << 8;

    spl = header + header_length;
    /*in litle endian*/
    uint32_t payload_size = 0;
    payload_size =  ((uint32_t) spl[0]) << 0;
    payload_size |= ((uint32_t) spl[1]) << 8;
    payload_size |= ((uint32_t) spl[2]) << 16;
    payload_size |= ((uint32_t) spl[3]) << 24;

    spl += 4; /*start of payload*/
    epl = spl + payload_size; /*end of payload*/

    if(epl > buff + size)
    {
      av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: payload size bigger than buffer, clipped to buffer size (demux_uvcH264)\n");
      epl = buff + size;
    }

    sp = spl;

    uint32_t max_seg_size = 64*1024;

    /*copy first segment*/
    length -= header_length + 6;

    if(length <= max_seg_size)
    {
      /*copy the segment to h264 buffer*/
      memcpy(ph264, sp, length);
      ph264 += length;
      sp += length;
    }

    /*copy other segments*/
    while( epl > sp)
    {
      if(sp[0] != 0xFF ||
         sp[1] != APP4)
      {
        av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: expected APP4 marker but none found (demux_uvcH264)\n");
        goto exit;
      }
      else
      {
        length  = (uint16_t) sp[2] << 8;
        length |= (uint16_t) sp[3];

        length -= 2; /*remove the 2 bytes from length*/
      }

      sp += 4; /*APP4 marker + length*/

      if(length != max_seg_size) {
        av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: segment length is %i (demux_uvcH264)\n", length);
      }

      /*copy the segment to h264 buffer*/
      memcpy(ph264, sp, length);
      ph264 += length;
      sp += length;

      if((epl-sp) > 0 && (epl-sp < 4)) {
        av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: payload ended unexpectedly (demux_uvcH264)\n");
        goto exit;
      }
    }

    if(epl-sp > 0) {
      av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: copy segment with %i bytes (demux_uvcH264)\n", (int) (epl-sp));
      /*copy the remaining data*/
      memcpy(ph264, sp, epl-sp);
      ph264 += epl-sp;
    }

  exit:
    out->size = (ph264 - h264_data);
    av_packet_free(&in);
    return 0;

  fail:
    av_packet_unref(out);
    av_packet_free(&in);
    return AVERROR_INVALIDDATA;
}

static int mjpeg_demux_h264_init(AVBSFContext *bsf)
{
//  int err, i;
//
//  err = ff_cbs_init(&ctx->input, AV_CODEC_ID_MJPEG, bsf);
//  if (err < 0)
//    return err;
//
//  err = ff_cbs_init(&ctx->output, AV_CODEC_ID_H264, bsf);
//  if (err < 0)
//    return err;

  bsf->par_out->codec_type = AVMEDIA_TYPE_VIDEO;
  bsf->par_out->codec_tag = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV420P); //TODO fix hardcode
  bsf->par_out->codec_id = AV_CODEC_ID_H264;
  bsf->par_out->format = AV_PIX_FMT_YUV420P; //TODO fix hardcode

  return 0;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_MJPEG, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_mjpeg_demux_h264_bsf = {
    .name      = "mjpeg_demux_h264",
    .init      = &mjpeg_demux_h264_init,
    .filter    = &mjpeg_demux_h264,
    .codec_ids = codec_ids,
};
