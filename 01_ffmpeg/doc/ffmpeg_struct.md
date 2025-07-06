# 0. 结构体
## AVFormatContext
(https://blog.csdn.net/leixiaohua1020/article/details/14214705)

在使用FFMPEG进行开发的时候，AVFormatContext是一个贯穿始终的数据结构，很多函数都要用到它作为参数。它是FFMPEG解封装（flv，mp4，rmvb，avi）功能的结构体。下面看几个主要变量的作用（在这里考虑解码的情况）：
- struct AVInputFormat *iformat：输入数据的封装格式
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


## AVCodecContext
(https://blog.csdn.net/leixiaohua1020/article/details/14214859) 

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

## AVPakcet
(https://blog.csdn.net/leixiaohua1020/article/details/14215755)
重要参数
- uint8_t *data：压缩编码的数据。例如对于H.264来说。1个AVPacket的data通常对应一个NAL。注意：在这里只是对应，而不是一模一样。他们之间有微小的差别：使用FFMPEG类库分离出多媒体文件中的H.264码流因此在使用FFMPEG进行视音频处理的时候，常常可以将得到的AVPacket的data数据直接写成文件，从而得到视音频的码流文件。
- int   size：data的大小
- int64_t pts：显示时间戳
- int64_t dts：解码时间戳
- int   stream_index：标识该AVPacket所属的视频/音频流。