#include "audioencoder.h"

AudioEncoder::AudioEncoder()
{
}
AudioEncoder::~AudioEncoder()
{
    if (codec_ctx_) {
        DeInit();
    }
}
int AudioEncoder::InitAAC(int channels, int sample_rate, int bit_rate)
{
    channels_ = channels;
    sample_rate_ = sample_rate;
    bit_rate_ = bit_rate;

    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        printf("avcodec_find_encoder AV_CODEC_ID_AAC failed\n");
        return -1;
    }
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        printf("avcodec_alloc_context3 AV_CODEC_ID_AAC failed\n");
        return -1;
    }

    codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codec_ctx_->bit_rate = bit_rate_;
    codec_ctx_->sample_rate = sample_rate_;
    codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codec_ctx_->channels = channels_;
    codec_ctx_->channel_layout = av_get_default_channel_layout(codec_ctx_->channels);

    // 使用avcodec_alloc_context3初始化的AVCodecContext，avcodec_oepn2第二参数为NULL即可。
    int ret = avcodec_open2(codec_ctx_, NULL, NULL);
    if (ret != 0) {
        char errbuf[1024] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf) - 1);
        printf("avcodec_open2 failed:%s\n", errbuf);
        return -1;
    }
    printf("InitAAC success\n");
    return 0;
}
void AudioEncoder::DeInit()
{
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_); // codec_ctx_被设置为NULL
                                           //        codec_ctx_ = NULL;  // 不需要再写
    }
}
AVPacket *AudioEncoder::Encode(AVFrame *frame, int stream_index, int64_t pts, int64_t time_base)
{
    if (!codec_ctx_) {
        printf("codec_ctx_ null\n");
        return NULL;
    }

    pts = av_rescale_q(pts, AVRational{1, (int)time_base}, codec_ctx_->time_base);
    if (frame) {
        frame->pts = pts;
    }
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret != 0) {
        char errbuf[1024] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf) - 1);
        printf("avcodec_send_frame failed:%s\n", errbuf);
        return NULL;
    }

    AVPacket *packet = av_packet_alloc();
    ret = avcodec_receive_packet(codec_ctx_, packet);
    if (ret != 0) {
        char errbuf[1024] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf) - 1);
        printf("aac avcodec_receive_packet failed:%s\n", errbuf);
        av_packet_free(&packet);
        return NULL;
    }
    packet->stream_index = stream_index;
    return packet;
}

int AudioEncoder::Encode(AVFrame *frame, int stream_index, int64_t pts, int64_t time_base,
                         std::vector<AVPacket *> &packets)
{
    if (!codec_ctx_) {
        printf("codec_ctx_ null\n");
        return -1;
    }

    pts = av_rescale_q(pts, AVRational{1, (int)time_base}, codec_ctx_->time_base);
    if (frame) {
        frame->pts = pts;
    }
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret != 0) {
        char errbuf[1024] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf) - 1);
        printf("avcodec_send_frame failed:%s\n", errbuf);
        return -1;
    }
    while (1) {
        AVPacket *packet = av_packet_alloc();
        ret = avcodec_receive_packet(codec_ctx_, packet);
        packet->stream_index = stream_index;
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            av_packet_free(&packet);
            break;
        } else if (ret < 0) {
            char errbuf[1024] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf) - 1);
            printf("aac avcodec_receive_packet failed:%s\n", errbuf);
            av_packet_free(&packet);
            ret = -1;
        }
        packets.push_back(packet);
    }
    return ret;
}

int AudioEncoder::GetFrameSize()
{
    if (codec_ctx_)
        return codec_ctx_->frame_size;
    return 0;
}
int AudioEncoder::GetSampleFormat()
{
    if (codec_ctx_)
        return codec_ctx_->sample_fmt;

    return AV_SAMPLE_FMT_NONE;
}
AVCodecContext *AudioEncoder::GetCodecContext()
{
    return codec_ctx_;
}
