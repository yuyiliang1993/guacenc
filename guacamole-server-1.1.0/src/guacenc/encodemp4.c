#include "config.h"
#include "buffer.h"
#include "ffmpeg-compat.h"
#include "log.h"
#include "video.h"

#include <guacamole/client.h>
#include <guacamole/timestamp.h>

#include <cairo/cairo.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int guacenc_video_flush_frame(guacenc_video* video);
int guacenc_video_write_frame(guacenc_video* video, AVFrame* frame);

//codec_name为libx264,使用mpeg4无法转mp4
guacenc_video* guacenc_video_alloc(const char* path, const char* codec_name,
        int width, int height, int bitrate) {

    //注册所有编码器
    avcodec_register_all();
    av_register_all();
    
    /* Pull codec based on name */
    AVCodec* codec = avcodec_find_encoder_by_name(codec_name);
    if (codec == NULL) {
        guacenc_log(GUAC_LOG_ERROR, "Failed to locate codec \"%s\".",
                codec_name);
        goto fail_codec;
    }

    //添加封装格式的ffmpeg接口
#if 1
    AVFormatContext *ofmt_ctx = NULL;
    int ret;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, path);
    if (!ofmt_ctx){
        guacenc_log(GUAC_LOG_ERROR, "Could not deduce output format from file extension: \
                                                using %s", codec_name);
        avformat_alloc_output_context2(&ofmt_ctx, NULL, codec_name, path);
    }
    if (!ofmt_ctx)
        goto fail_codec;

    AVStream *st;
    st = avformat_new_stream(ofmt_ctx, NULL);
    st->time_base = (AVRational) { 1, GUACENC_VIDEO_FRAMERATE };
#endif

    /* Retrieve encoding context */
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (context == NULL) {
        guacenc_log(GUAC_LOG_ERROR, "Failed to allocate context for "
                "codec \"%s\".", codec_name);
        goto fail_context;
    }
    
    /* Init context with encoding parameters */
    context->bit_rate = bitrate;
    context->width = width;
    context->height = height;
    context->time_base = st->time_base;
    context->framerate = (AVRational){25, 1};
    context->gop_size = 10;
    context->max_b_frames = 1;
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(context->priv_data, "preset", "slow", 0);//预设编码速度设置
    if (context->codec_id == AV_CODEC_ID_H264)
    {
        context->refs = 3;
        context->qmin = 10; //最小量化因子,值越小视频质量越高(0-69)
        context->qmax = 51; //最大量化因子,想控制输出品质可以调低此值(13-69)
        context->qcompress = 0.6; //量化值曲线因子,0.0趋向于恒定比特率,1.0 趋向于恒定量化值
    }
   
    /* Open codec for use */
    if (avcodec_open2(context, codec, NULL) < 0) {
        guacenc_log(GUAC_LOG_ERROR, "Failed to open codec \"%s\".", codec_name);
        goto fail_codec_open;
    }

#if 1
    //给stream视频流赋值
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = context->codec_id;
    st->codecpar->bit_rate = context->bit_rate;
    st->codecpar->format   = context->pix_fmt;
    st->codecpar->width = context->width;
    st->codecpar->height = context->height;
    //打印视频流信息
    av_dump_format(ofmt_ctx, 0, path, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
         ret = avio_open(&ofmt_ctx->pb, path, AVIO_FLAG_WRITE);
         if (ret < 0) {
                 guacenc_log(GUAC_LOG_ERROR, "Could not open output file '%s'", path);
                 goto fail_context;
         }
    }
    //写入视频格式头
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        guacenc_log(GUAC_LOG_ERROR, "Error occurred when opening output file: %s\n",
                        av_err2str(ret));
        goto fail_context;
    }
#endif
    /* Allocate corresponding frame */
    AVFrame* frame = av_frame_alloc();
    if (frame == NULL) {
	    goto fail_frame;
    }

    /* Copy necessary data for frame from context */
    frame->format = context->pix_fmt;
    frame->width = context->width;
    frame->height = context->height;

    /* Allocate actual backing data for frame */
    if (av_image_alloc(frame->data, frame->linesize, frame->width,
                frame->height, frame->format, 32) < 0) {
        goto fail_frame_data;
    }

    /* Allocate video structure */
    guacenc_video* video = malloc(sizeof(guacenc_video));
                
    /* Init properties of video */
    //video->output = output;
    video->context = context;
    video->next_frame = frame;
    video->width = width;
    video->height = height;
    video->bitrate = bitrate;

    /* No frames have been written or prepared yet */
    video->last_timestamp = 0;
    video->next_pts = 0;
    video->ofmt_ctx = ofmt_ctx;
    video->st = st;

    return video;

fail_frame_data:
    av_frame_free(&frame);

fail_frame:
fail_codec_open:
    avcodec_free_context(&context);

fail_context:
fail_codec:
    return NULL;
}

int guacenc_video_free(guacenc_video* video) {
    /* Ignore NULL video */
    if (video == NULL)
        return 0;

    /* Write final frame */
    guacenc_video_flush_frame(video);

    /* Init video packet for final flush of encoded data */
    AVPacket packet;
    av_init_packet(&packet);

    /* Flush any unwritten frames */
    int retval;
    do {
        retval = guacenc_video_write_frame(video, NULL);
    } while (retval > 0);

    /* Free frame encoding data */
    av_freep(&video->next_frame->data[0]);
    av_frame_free(&video->next_frame);
    //添加文件结尾，以及关闭视频文件，释放AVformat
#if 1
    av_write_trailer(video->ofmt_ctx);
    if (video->ofmt_ctx && !(video->ofmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_close(video->ofmt_ctx->pb);
    avformat_free_context(video->ofmt_ctx);
#endif
    /* Clean up encoding context */
    avcodec_close(video->context);
    avcodec_free_context(&(video->context));

    free(video);
    return 0;
}


//添加的打印信息
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}


int guacenc_avcodec_encode_video(guacenc_video* video, AVFrame* frame) {

/* For libavcodec < 54.1.0: packets were handled as raw malloc'd buffers */
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54,1,0)

    AVCodecContext* context = video->context;

    /* Calculate appropriate buffer size */
    int length = FF_MIN_BUFFER_SIZE + 12 * context->width * context->height;

    /* Allocate space for output */
    uint8_t* data = malloc(length);
    if (data == NULL)
        return -1;

    /* Encode packet of video data */
    int used = avcodec_encode_video(context, data, length, frame);
    if (used < 0) {
        guacenc_log(GUAC_LOG_WARNING, "Error encoding frame #%" PRId64,
                video->next_pts);
        free(data);
        return -1;
    }

    /* Report if no data needs to be written */
    if (used == 0) {
        free(data);
        return 0;
    }

    /* Write data, logging any errors */
    guacenc_write_packet(video, data, used);
    free(data);
    return 1;

#else

    /* Init video packet */
    AVPacket packet;
    av_init_packet(&packet);

    /* Request that encoder allocate data for packet */
    packet.data = NULL;
    packet.size = 0;
    
    /* For libavcodec < 57.37.100: input/output was not decoupled */
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57,37,100)
    /* Write frame to video */
    int got_data;
    if (avcodec_encode_video2(video->context, &packet, frame, &got_data) < 0) {
        guacenc_log(GUAC_LOG_WARNING, "Error encoding frame #%" PRId64,
                video->next_pts);
        return -1;
    }

    /* Write corresponding data to file */
    if (got_data) {
        guacenc_write_packet(video, packet.data, packet.size);
        av_packet_unref(&packet);
    }
#else
    /* Write frame to video */
    int result = avcodec_send_frame(video->context, frame);

    /* Stop once encoded has been flushed */
    if (result == AVERROR_EOF)
        return 0;
    /* Abort on error */
    else if (result < 0) {
        guacenc_log(GUAC_LOG_WARNING, "Error encoding frame #%" PRId64,
                video->next_pts);
        return -1;
    }

    /* Flush all available packets */
    int got_data = 0;
    while (avcodec_receive_packet(video->context, &packet) == 0) {

        /* Data was received */
        got_data = 1;

        /* Attempt to write data to output file */
        //更新流时间戳
        av_packet_rescale_ts(&packet, video->context->time_base, video->st->time_base);
        packet.stream_index = video->st->index;
        packet.pos = -1;
        //打印packet数据信息
        log_packet(video->ofmt_ctx, &packet);
	//写入视频文件
        result = av_interleaved_write_frame(video->ofmt_ctx, &packet);
        if (result < 0) {
              guacenc_log(GUAC_LOG_DEBUG, "Error while writing output packet: %s\n", av_err2str(result));
              break;
        }
        av_packet_unref(&packet);
    }
#endif

    /* Frame may have been queued for later writing / reordering */
    if (!got_data)
        guacenc_log(GUAC_LOG_DEBUG, "Frame #%08" PRId64 ": queued for later",
                video->next_pts);

    return got_data;
#endif
}

