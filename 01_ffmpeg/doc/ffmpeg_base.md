# 0. 结构体
## AVFormatContext
(https://blog.csdn.net/leixiaohua1020/article/details/14214705)

在使用FFMPEG进行开发的时候，AVFormatContext是一个贯穿始终的数据结构，很多函数都要用到它作为参数。它是FFMPEG解封装（flv，mp4，rmvb，avi）功能的结构体。下面看几个主要变量的作用（在这里考虑解码的情况）：
- `struct AVInputFormat *iformat`：输入数据的封装格式，由`avformat_open_input()`设置
- `struct AVOutputFormat *oformat`，由`avformat_alloc_output_context2`设置
- AVIOContext *pb：输入数据的缓存
- unsigned int nb_streams：视音频流的个数（老版本通过遍历nb_streams，streams得到流信息）
- AVStream **streams：视音频流（数组个数为nb_streams，详见AVStream结构体分析）
- char filename[1024]：文件名
- int64_t duration：时长（单位：微秒us，转换为秒需要除以1000000）
- int bit_rate：比特率（单位bps，转换为kbps需要除以1000）
- AVDictionary *metadata：元数据

## AVStream
(https://blog.csdn.net/leixiaohua1020/article/details/14215821)

AVStream 重要的变量如下所示：
- int index：标识该视频/音频流
- AVCodecContext *codec：指向该视频/音频流的AVCodecContext（它们是一一对应的关系）
- AVCodecParamter *codecpar: 
- AVRational time_base：时基。通过该值可以把PTS，DTS转化为真正的时间。FFMPEG其他结构体中也有这个字段，但是根据我的经验，只有AVStream中的time_base是可用的。PTS*time_base=真正的时间
- int64_t duration：该视频/音频流长度
- AVDictionary *metadata：元数据信息
- AVRational avg_frame_rate：帧率（注：对视频来说，这个挺重要的）
- AVPacket attached_pic：附带的图片。比如说一些MP3，AAC音频文件附带的专辑封面。


## AVPakcet
(https://blog.csdn.net/leixiaohua1020/article/details/14215755)
重要参数
- uint8_t *data：压缩编码的数据。例如对于H.264来说。1个AVPacket的data通常对应一个NAL。注意：在这里只是对应，而不是一模一样。他们之间有微小的差别：使用FFMPEG类库分离出多媒体文件中的H.264码流因此在使用FFMPEG进行视音频处理的时候，常常可以将得到的AVPacket的data数据直接写成文件，从而得到视音频的码流文件。
- int   size：data的大小
- int64_t pts：显示时间戳
- int64_t dts：解码时间戳
- int   stream_index：标识该AVPacket所属的视频/音频流。

## AVFrame
(https://blog.csdn.net/leixiaohua1020/article/details/14214577)  

AVFrame结构体一般用于存储原始数据（即非压缩数据，例如对视频来说是YUV，RGB，对音频来说是PCM），此外还包含了一些相关的信息。比如说，解码的时候存储了宏块类型表，QP表，运动矢量表等数据。编码的时候也存储了相关的数据。因此在使用FFMPEG进行码流分析的时候，AVFrame是一个很重要的结构体。  

主要变量的作用（在这里考虑解码的情况）:
- uint8_t *data[AV_NUM_DATA_POINTERS]：解码后原始数据（对视频来说是YUV，RGB，对音频来说是PCM）
    > 对于packed格式的数据（例如RGB24），会存到data[0]里面。  
    > 对于planar格式的数据（例如YUV420P），则会分开成data[0]，data[1]，data[2]...（YUV420P中data[0]存Y，data[1]存U，data[2]存V）

- int linesize[AV_NUM_DATA_POINTERS]：data中“一行”数据的大小。注意：未必等于图像的宽，一般大于图像的宽。
- int width, height：视频帧宽和高（1920x1080,1280x720...）
- int nb_samples：音频的一个AVFrame中可能包含多个音频帧，在此标记包含了几个
- int format：解码后原始数据类型（YUV420，YUV422，RGB24...）
- int key_frame：是否是关键帧
- enum AVPictureType pict_type：帧类型（I,B,P...）
- AVRational sample_aspect_ratio：宽高比（16:9，4:3...）
- int64_t pts：显示时间戳
- int coded_picture_number：编码帧序号
- int display_picture_number：显示帧序号
- int8_t *qscale_table：QP表
- uint8_t *mbskip_table：跳过宏块表
- int16_t (*motion_val[2])[2]：运动矢量表
- uint32_t *mb_type：宏块类型表
- short *dct_coeff：DCT系数，这个没有提取过
- int8_t *ref_index[2]：运动估计参考帧列表（貌似H.264这种比较新的标准才会涉及到多参考帧）
- int interlaced_frame：是否是隔行扫描
- uint8_t motion_subsample_log2：一个宏块中的运动矢量采样个数，取log的


## AVCodecID
解码器枚举

## AVCodec 
(https://blog.csdn.net/leixiaohua1020/article/details/14215833)  

这是一个静态的结构体，代表 FFmpeg 库中注册的一种编解码器能力（例如 H.264 视频解码器 h264， AAC 音频编码器 aac）。它描述了编解码器本身的基本信息和支持的操作。

最主要的几个变量：
- const char *name：编解码器的名字，比较短
- const char *long_name：编解码器的名字，全称，比较长
- enum AVMediaType type：指明了类型，是视频（AVMEDIA_TYPE_VIDEO），音频（AVMEDIA_TYPE_AUDIO），还是字幕（AVMEDIA_TYPE_SUBTITLE），空（AVMEDIA_TYPE_UNKNOW）
- enum AVCodecID id：ID，不重复
- `const AVRational *supported_framerates`：支持的帧率（仅视频）
- `const enum AVPixelFormat *pix_fmts`：支持的像素格式（仅视频）例如：AV_PIX_FMT_YUV420p，AV_PIX_FMT_YUVV422 等
- `const int *supported_samplerates`：支持的采样率（仅音频）
- `const enum AVSampleFormat *sample_fmts`：支持的采样格式（仅音频）。源码仅有 AV_SAMPLE_FMT_FLTP 
- const uint64_t *channel_layouts：支持的声道数（仅音频）
- int priv_data_size：私有数据的大小

例如，H264解码器
```c
AVCodec ff_h264_decoder = {
    .name           = "h264",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H264,
    .priv_data_size = sizeof(H264Context),
    .init           = ff_h264_decode_init,
    .close          = ff_h264_decode_end,
    .decode         = decode_frame,
    .capabilities   = /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 | CODEC_CAP_DELAY |
                      CODEC_CAP_SLICE_THREADS | CODEC_CAP_FRAME_THREADS,
    .flush= flush_dpb,
    .long_name = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(decode_update_thread_context),
    .profiles = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class     = &h264_class,
};
```

## AVCodecContext
(https://blog.csdn.net/leixiaohua1020/article/details/14214859) 

这是一个动态的结构体，代表一个具体的、正在使用的编解码器实例的运行时状态和配置。它存储了特定于当前解码或编码任务的所有信息：配置参数、内部状态、输入/输出设置、缓冲区管理等。它是操作编解码器的核心句柄。

关键的变量(解码)：
- enum AVMediaType codec_type：编解码器的类型（视频AVMEDIA_TYPE_VIDEO，音频AVMEDIA_TYPE_AUDIO...）
- struct AVCodec  *codec：采用的解码器AVCodec（H.264,MPEG2...）
- int bit_rate：平均比特率
- uint8_t *extradata; int extradata_size：针对特定编码器包含的附加信息（例如对于H.264解码器来说，存储SPS，PPS等）
- AVRational time_base：根据该参数，可以把PTS转化为实际的时间（单位为秒s）
- int width, height：如果是视频的话，代表宽和高
- int refs：运动估计参考帧的个数（H.264的话会有多帧，MPEG2这类的一般就没有了）
- int sample_rate：采样率（音频）
- int channels：声道数（音频）
- enum AVSampleFormat sample_fmt：采样格式
- int profile：型（H.264里面就有，其他编码标准应该也有）
- int level：级（和profile差不太多）

## AVCodecParser
AVCodecParser 是一个静态结构体，它定义了一种解析特定编码格式原始字节流的能力。

## AVCodecParserContext

这是一个动态的结构体，代表一个码流解析器 (Bitstream Parser) 的运行时状态。它的核心任务是将输入的原始字节流（const uint8_t *buf, int buf_size）分割成有意义的、完整的、可以被解码器 (AVCodecContext) 正确处理的数据包 (AVPacket)。它解决了码流边界对齐、帧/包起始码查找、帧长度解析、时间戳预测/计算等问题。对于许多容器格式（如 MP4, MKV）中的包，av_read_frame 通常已经处理好了这些，但对于原始流（如 H.264 Elementary Stream）、某些网络流或需要精确控制解析的情况，AVCodecParser 非常有用。

主要变量:
- `const struct AVCodecParser *parser`: 指向关联的 AVCodecParser 结构体的指针。AVCodecParser 类似于 AVCodec，是一个静态描述符，定义了特定编解码器（如 h264）的解析器实现（ff_h264_parser）。它包含指向核心解析函数 parser_parse 的指针。`在 av_parser_init() 初始化`

- void *priv_data: 指向解析器私有数据的指针。类似于 AVCodecContext 的 priv_data，它指向一个特定于当前 parser 的结构体（例如，H264ParseContext），存储该解析器实现所需的内部状态和历史信息（如前一帧的未完成数据、时间戳状态、SPS/PPS 缓存等）。

- int64_t pts, int64_t dts: 当前解析上下文预测/计算出的下一个数据包的时间戳。这些值是基于解析器对码流的分析（如从码流中提取的 PTS/DTS 信息、帧率推算等）得出的，是提供给后续生成的 AVPacket 的重要信息。

- int64_t last_pts, int64_t last_dts: 上一个输出的数据包的时间戳。

- int duration: 预测/计算出的下一个数据包的持续时间。

- int offset: 输入缓冲区 (buf) 中当前解析位置的偏移量。

- int64_t cur_offset: 文件或流中的当前总偏移量（累积）。

- int flags: 解析器状态标志。

- int key_frame: 预测下一个帧是否为关键帧 (I 帧)。

- int64_t pos: 下一个数据包在文件/流中的位置（字节偏移）。

- int64_t next_frame_offset: 下一帧的起始位置（如果可知）。

- AVCodecContext *avctx: 一个可选的、指向关联的 AVCodecContext 的指针。这个链接有时是必要的，因为解析器可能需要从 AVCodecContext 获取配置信息（如 width, height, extradata）才能正确解析码流（例如，H.264 解析器需要 SPS/PPS 信息来正确计算帧大小和类型）。这个链接通常在创建解析器上下文 (`av_parser_init`) 时建立。

## AVCodec & AVCodecContext & AVCodecParserContext
1. AVCodec 是蓝图：  
- 它定义了库中存在哪些编解码器（如 H.264 解码器）。

- 它为创建 AVCodecContext 提供模板（avcodec_alloc_context3(codec)）。

- 它为创建 AVCodecParserContext 提供依据（av_parser_init(codec_id)，codec_id 通常从 AVCodec 获取）。

2. AVCodecContext 是引擎：

- 它代表一个具体的编解码任务实例（如解码这个特定的 H.264 视频流）。

- 它通过 codec 成员关联到它使用的 AVCodec（蓝图）。

- 它通过 priv_data 成员拥有并依赖特定编解码器的私有状态数据。

- 它可以被关联到 AVCodecParserContext (通过 AVCodecParserContext.avctx)。这种关联允许解析器访问解码器所需的配置信息（如 extradata）来辅助解析。

- 它接收由 AVCodecParserContext (或 av_read_frame) 产生的 AVPacket 作为输入（解码时）。

3. AVCodecParserContext 是预处理器：

- 它代表一个针对特定编解码格式（如 H.264）的码流解析任务实例。

- 它通过 parser 成员关联到它使用的 `AVCodecParser`（静态解析器描述）。

- 它通过 priv_data 成员拥有并依赖特定解析器的私有状态和历史数据。

- 它可以关联到一个 AVCodecContext (通过 avctx 成员)。这种关联通常是可选的，但在解析需要额外参数（如 extradata）的码流时是必要的。

- 它的核心任务是将原始字节流分割成完整的 AVPacket。

- 它计算并输出 AVPacket 所需的关键元数据，特别是 pts 和 dts。这些时间戳信息是它分析码流得出的，是 AVPacket 的重要组成部分。

- 它为 AVCodecContext 准备输入数据 (AVPacket)。解析器的输出直接喂给解码器的输入 (avcodec_send_packet)。

## AVIOContext 自定义IO上下文
[AVIOContext 使用案例](./ffmpeg_mutex_decode.md/#8-avio-内存输入模式)



## AVInputFormat 
AVFormatContext 结构体的成员 


## AVOutputFormat
​角色​：​封装器（Muxer）的接口，定义如何将数据包写入容器（如生成MP4文件）。
​用途​：仅用于输出，提供格式封装的静态方法集合。
​关键方法​：
write_header：写入文件头
write_packet：写入数据包
write_trailer：写入文件尾


# 1. 函数

