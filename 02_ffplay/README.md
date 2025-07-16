# 1 struct VideoState 播放器封装
```c
typedef struct VideoState {
    SDL_Thread *read_tid;     // 读线程的线程ID
    const AVInputFormat *iformat;     // 输入格式
    int abort_request;    // 中断请求标志 =1 退出播放
    int force_refresh;     // 强制刷新标志  =1 刷新画面
    int paused;           // 播放是否暂停标志 =1 暂停
    int last_paused;      // 上一次的暂停状态
    int queue_attachments_req; // 是否请求附带图片（如MP3或AAC文件的专辑封面等）
    int seek_req;         // 是否请求跳转
    int seek_flags;       // 跳转标志，比如 AVSEEK_FLAG_BYTE 等
    int64_t seek_pos;     // 跳转位置，绝对位置（当前+增量）
    int64_t seek_rel;     // 跳转 增量
    int read_pause_return; // 发送暂停请求的结果（例如RTSP暂停消息是否成功）
    AVFormatContext *ic;     // 格式上下文
    int realtime;         // 是否为实时播放
  
    Clock audclk;         // 音频时钟
    Clock vidclk;         // 视频时钟
    Clock extclk;         // 外部时钟
  
    FrameQueue pictq;     // 图像帧队列
    FrameQueue subpq;     // 字幕帧队列
    FrameQueue sampq;     // 音频采样帧队列
  
    Decoder auddec;       // 音频解码器
    Decoder viddec;       // 视频解码器
    Decoder subdec;       // 字幕解码器
  
     /*******************音视频同步相关************************/
    int audio_stream;     // 音频流的索引
    int av_sync_type;     // 音视频同步类型
    double audio_clock;   // 当前音频时钟值
    int audio_clock_serial; // 音频时钟序列号
    double audio_diff_cum; // 音频差异累计值（用于计算平均差异）
    double audio_diff_avg_coef; // 音频差异平均系数
    double audio_diff_threshold; // 音频差异阈值
    int audio_diff_avg_count;    // 音频差异平均计数
  
  
    AVStream *audio_st;  // 音频流
    PacketQueue audioq;  // 音频包队列
    int audio_hw_buf_size;  // 音频硬件缓冲区大小
    uint8_t *audio_buf;     // 音频缓冲区
    uint8_t *audio_buf1;    // 重采样音频缓冲区
    unsigned int audio_buf_size; // 音频缓冲区大小（字节）
    unsigned int audio_buf1_size; // 重采样音频缓冲区大小（字节）
    int audio_buf_index;   // 音频缓冲区播放位置
    int audio_write_buf_size; // 当前音频缓冲区中未播放的数据大小
    int audio_volume;      // 音频音量
    int muted;             // 是否静音
    struct AudioParams audio_src; // 音频源参数
    struct AudioParams audio_filter_src; // 音频滤波源参数
    struct AudioParams audio_tgt; // 音频目标参数
    struct SwrContext *swr_ctx;  // 音频重采样上下文
    int frame_drops_early; // 解码器队列中由于同步问题而提前丢弃的帧
    int frame_drops_late;  // 由于播放延迟而丢弃的帧
    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;          // 显示模式
  
    /*******************音频可视化相关************************/
    int16_t sample_array[SAMPLE_ARRAY_SIZE]; // 音频可视化数据
    int sample_array_index;  // 音频可视化数据索引
    int last_i_start; // 上一次计算的音频显示起始索引
    AVTXContext *rdft;     // 快速傅里叶变换上下文
    av_tx_fn rdft_fn;     // 快速傅里叶变换函数指针
    int rdft_bits;       // 快速傅里叶变换的位数
    float *real_data;    // 实部数据
    AVComplexFloat *rdft_data; // 复数数据
    int xpos;            // x 位置
    double last_vis_time; // 上一次可视化时间
  
    /*******************渲染纹理相关************************/
    SDL_Texture *vis_texture; // 音频可视化纹理
    SDL_Texture *sub_texture; // 字幕纹理
    SDL_Texture *vid_texture; // 视频纹理
  
  
    int subtitle_stream; // 字幕流的索引
    AVStream *subtitle_st; // 字幕流
    PacketQueue subtitleq; // 字幕包队列
    double frame_timer;  // 帧定时器
    double frame_last_returned_time; // 上一帧的显示时间戳
    double frame_last_filter_delay; // 上一帧的滤镜延迟
    int video_stream;    // 视频流的索引
    AVStream *video_st;  // 视频流
    PacketQueue videoq;  // 视频包队列
    double max_frame_duration; // 最大帧持续时间
    struct SwsContext *sub_convert_ctx; // 字幕转换上下文
    int eof;             // 文件结束标志
    char *filename;      // 文件名
  
    int width;          // 播放窗口宽度
    int height;         // 播放窗口高度
  
    int xleft;          // 显示区域的左偏移
    int ytop;           // 显示区域的上偏移
    int step;           // 步进模式（单帧模式）
    int vfilter_idx;    // 视频滤镜索引
    AVFilterContext *in_video_filter; // 视频输入滤镜上下文
    AVFilterContext *out_video_filter; // 视频输出滤镜上下文
    AVFilterContext *in_audio_filter; // 音频输入滤镜上下文
    AVFilterContext *out_audio_filter; // 音频输出滤镜上下文
    AVFilterGraph *agraph; // 音频滤镜图
    int last_video_stream; // 上一次的视频流索引
    int last_audio_stream; // 上一次的音频流索引
    int last_subtitle_stream; // 上一次的字幕流索引
    SDL_cond *continue_read_thread; // 读线程条件变量
} VideoState;
```

# 2 struct Clock 时钟封装
```c
// 这里讲的系统时钟 是通过av_gettime_relative()获取到的时钟，单位为微妙
typedef struct Clock {
    double	pts;            // 时钟基础, 当前帧(待播放)显示时间戳，播放后，当前帧变成上一帧
    // 当前pts与当前系统时钟的差值, audio、video对于该值是独立的
    double	pts_drift;      // clock base minus time at which we updated the clock
    // 当前时钟(如视频时钟)最后一次更新时间，也可称当前时钟时间
    double	last_updated;   // 最后一次更新的系统时钟
    double	speed;          // 时钟速度控制，用于控制播放速度
    // 播放序列，所谓播放序列就是一段连续的播放动作，一个seek操作会启动一段新的播放序列
    int	serial;             // clock is based on a packet with this serial
    int	paused;             // = 1 说明是暂停状态
    // 指向packet_serial
    int *queue_serial;      /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;
```

# 3 struct MyAVPacketList和PacketQueue队列
ffplay⽤PacketQueue保存解封装后的数据，即保存AVPacket
ffplay⾸先定义了⼀个结构体 MyAVPacketList ：

```c
typedef struct MyAVPacketList {
    AVPacket		pkt;    //解封装后的数据
    struct MyAVPacketList	*next;  //下一个节点
    int			serial;     //播放序列，（每次seek，serial会更新）
} MyAVPacketList;
```
> serial字段主要⽤于标记当前节点的播放序列号，ffplay中多处⽤到serial的概念，主要⽤来区分是否连续数据，每做⼀次seek，该serial都会做+1的递增，以区分不同的播放序列。serial字段在我们ffplay的分析中应⽤⾮常⼴泛，谨记他是⽤来区分数据否连续先。

接着定义另⼀个结构体PacketQueue：
```c
typedef struct PacketQueue {
    MyAVPacketList	*first_pkt, *last_pkt;  // 队首，队尾指针
    int		nb_packets;   // 包数量，也就是队列元素数量
    int		size;         // 队列所有元素的数据大小总和（加上了MyAVPacketList）
    int64_t		duration; // 队列所有元素的数据播放持续时间
    int		abort_request; // 用户退出请求标志 =1 唤醒等待读/写线程
    int		serial;         // 播放序列号，和MyAVPacketList的serial作用相同，但改变的时序稍微有点不同 （MyAVPacketList的serial字段的赋值来自PacketQueue的serial）
    SDL_mutex	*mutex;     // 用于维持PacketQueue的多线程安全(SDL_mutex可以按pthread_mutex_t理解）
    SDL_cond	*cond;      // 用于读、写线程相互通知(SDL_cond可以按pthread_cond_t理解)
} PacketQueue;
```
音频，视频，字幕流 都有自己独立的 PacketQueue

## 3.1 packet_queue_init()
初始化⽤于初始各个字段的值，并创建mutex和cond
```c
/* packet queue handling */
static int packet_queue_init(PacketQueue *q){
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}
```

## 3.2 packet_queue_destroy()
相应的，packet_queue_destroy()销毁过程负责清理mutex和cond:
```c
static void packet_queue_destroy(PacketQueue *q){
    packet_queue_flush(q); //先清除所有的节点
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}
```

## 3.3 packet_queue_start()
```c
static void packet_queue_start(PacketQueue *q){
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt); //这里放入了一个flush_pkt
    SDL_UnlockMutex(q->mutex);
}
```
`flush_pkt`定义是 static AVPacket flush_pkt; ，是⼀个特殊的packet，主要⽤来作为⾮连续的两端数据的“分界”标记：
- 插⼊ flush_pkt 触发PacketQueue其对应的serial，加1操作
- 触发解码器清空⾃身缓存 `avcodec_flush_buffers()`，以备新序列的数据进⾏新解码 (特别是seek，要把解码的缓存清空) 

## 3.4 packet_queue_abort()
中⽌队列：
```c
static void packet_queue_abort(PacketQueue *q) {
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;       // 请求退出

    SDL_CondSignal(q->cond);    //释放一个条件信号

    SDL_UnlockMutex(q->mutex);
}
```

## 3.5 packet_queue_put()
读、写是PacketQueue的主要⽅法。
先看写，往队列中放⼊⼀个节点：
```c
static int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);//主要实现
    SDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);       //放入失败，释放AVPacket

    return ret;
}
```
### packet_queue_put_private 
```c
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt) {
    MyAVPacketList *pkt1;

    if (q->abort_request)   //如果已中止，则放入失败
        return -1;

    pkt1 = av_malloc(sizeof(MyAVPacketList));   //分配节点内存
    if (!pkt1)  //内存不足，则放入失败
        return -1;
    // 没有做引用计数，那这里也说明av_read_frame不会释放替用户释放buffer。
    pkt1->pkt = *pkt; //拷贝AVPacket(浅拷贝，AVPacket.data等内存并没有拷贝)
    pkt1->next = NULL;
    if (pkt == &flush_pkt)//如果放入的是flush_pkt，需要增加队列的播放序列号，以区分不连续的两段数据
    {
        q->serial++;
        printf("q->serial = %d\n", q->serial);
    }
    pkt1->serial = q->serial;   //用队列序列号标记节点
    /* 队列操作：如果last_pkt为空，说明队列是空的，新增节点为队头；
     * 否则，队列有数据，则让原队尾的next为新增节点。 最后将队尾指向新增节点
     */
    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;

    //队列属性操作：增加节点数、cache大小、cache总时长, 用来控制队列的大小
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;

    /* XXX: should duplicate packet data in DV case */
    //发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
    SDL_CondSignal(q->cond);
    return 0;
}
```
对于`packet_queue_put_private`主要完成3件事：
- 计算serial。serial标记了这个节点内的数据是何时的。⼀般情况下新增节点与上⼀个节点的serial是⼀样的，但当队列中加⼊⼀个flush_pkt后，后续节点的serial会⽐之前⼤1，⽤来区别不同播放序列的packet.
- 节点⼊队列操作。
- 队列属性操作。更新队列中节点的数⽬、占⽤字节数（含AVPacket.data的⼤⼩）及其时⻓。主要⽤来控制Packet队列的⼤⼩，我们PacketQueue链表式的队列，在内存充⾜的条件下我们可以⽆限put⼊packet，如果我们要控制队列⼤⼩，则需要通过其变量size、duration、nb_packets三者单⼀或者综合去约束队列的节点的数量，具体在read_thread进⾏分析。

## 3.6 packet_queue_get()
```c
/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
/**
 * @brief packet_queue_get
 * @param q 队列
 * @param pkt 输出参数，即MyAVPacketList.pkt
 * @param block 调用者是否需要在没节点可取的情况下阻塞等待
 * @param serial 输出参数，即MyAVPacketList.serial
 * @return <0: aborted; =0: no packet; >0: has packet
 */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial) {
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);    // 加锁

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;    //MyAVPacketList *pkt1; 从队头拿数据
        if (pkt1) {     //队列中有数据
            q->first_pkt = pkt1->next;  //队头移到第二个节点
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;    //节点数减1
            q->size -= pkt1->pkt.size + sizeof(*pkt1);  //cache大小扣除一个节点
            q->duration -= pkt1->pkt.duration;  //总时长扣除一个节点
            //返回AVPacket，这里发生一次AVPacket结构体拷贝，AVPacket的data只拷贝了指针
            *pkt = pkt1->pkt;
            if (serial) //如果需要输出serial，把serial输出
                *serial = pkt1->serial;
            av_free(pkt1);      //释放节点内存,只是释放节点，而不是释放AVPacket
            ret = 1;
            break;
        } else if (!block) {    //队列中没有数据，且非阻塞调用
            ret = 0;
            break;
        } else {    //队列中没有数据，且阻塞调用
            //这里没有break。for循环的另一个作用是在条件变量满足后重复上述代码取出节点
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);  // 释放锁
    return ret;
}
```

## 3.7 packet_queue_put_nullpacket() 
放⼊“空包”（nullpacket）。放⼊空包意味着流的结束，⼀般在媒体数据读取完成的时候放⼊空包。放⼊空包，⽬的是为了冲刷解码器，将编码器⾥⾯所有frame都读取出来:
```c
static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index) {
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}
```
> 文件数据读取完毕刷入空包

## 3.8 packet_queue_flush()
packet_queue_flush⽤于将packet队列中的所有节点清除，包括节点对应的AVPacket。⽐如⽤于退出播放和seek播放：
- 退出播放，则要清空packet queue的节点
- seek播放，要清空seek之前缓存的节点数据，以便插⼊新节点数据
```c
static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt); // 释放 AVPacket数据
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}
```

## 3.9 PacketQueue总结
MyAVPacketList的内存是完全由PacketQueue维护的，在put的时候malloc，在get的时候free。
AVPacket分两块：
⼀部分是AVPacket结构体的内存，这部分从MyAVPacketList的定义可以看出是和MyAVPacketList共存亡的。
另⼀部分是AVPacket字段指向的内存，这部分⼀般通过 av_packet_unref 函数释放。⼀般情况下，是在get后由调⽤者负责⽤ av_packet_unref 函数释放。特殊的情况是当碰到packet_queue_flush 或put失败时，这时需要队列⾃⼰处理。

# 2 struct Frame 和 FrameQueue队列
```c
/* Common struct for handling all types of decoded data and allocated render buffers. */
// 用于缓存解码后的数据
typedef struct Frame {
    AVFrame *frame;         // 指向数据帧
    AVSubtitle sub;            // 用于字幕
    int serial;             // 帧序列，在seek的操作时serial会变化
    double pts;            // 时间戳，单位为  秒
    double duration;       // 该帧持续时间，单位为  秒
    int64_t pos;            // 该帧在输入文件中的  字节位置
    int width;              // 图像宽度
    int height;             // 图像高读
    int format;             // 对于图像为(enum AVPixelFormat)，
    // 对于声音则为(enum AVSampleFormat)
    AVRational sar;            // 图像的宽高比（16:9，4:3...），如果未知或未指定则为0/1
    int uploaded;           // 用来记录该帧是否已经显示过？
    int flip_v;             // =1则旋转180， = 0则正常播放
} Frame;
```
- 真正存储解码后⾳视频数据的结构体为AVFrame ，存储字幕则使⽤AVSubtitle，该Frame的设计是为了⾳频、视频、字幕帧通⽤，所以Frame结构体的设计类似AVFrame，部分成员变量只对不同类型有作⽤，⽐如sar只对视频有作⽤。
- ⾥⾯也包含了serial播放序列（每次seek时都切换serial），sar（图像的宽⾼⽐（16:9，4:3…），该值来⾃AVFrame结构体的sample_aspect_ratio变量）。



```c
/* 这是一个循环队列，windex是指其中的首元素，rindex是指其中的尾部元素. */
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];        // FRAME_QUEUE_SIZE  最大size, 数字太大时会占用大量的内存，需要注意该值的设置
    int rindex;                         // 读索引。待播放时读取此帧进行播放，播放后此帧成为上一帧
    int windex;                         // 写索引
    int size;                           // 当前总帧数
    int max_size;                       // 可存储最大帧数
    int keep_last;                      // = 1说明要在队列里面保持最后一帧的数据不释放，只在销毁队列的时候才将其真正释放
    int rindex_shown;                   // 初始化为0，配合keep_last=1使用
    SDL_mutex *mutex;                     // 互斥量
    SDL_cond *cond;                      // 条件变量
    PacketQueue *pktq;                      // 数据包缓冲队列
} FrameQueue;
```
- FrameQueue是⼀个环形缓冲区(ring buffer)，是⽤`数组实现的⼀个FIFO`。数组⽅式的环形缓冲区适合于事先明确了缓冲区的最⼤容量的情形。
- ffplay中创建了三个frame_queue：⾳频frame_queue，视频frame_queue，字幕frame_queue。每⼀个frame_queue⼀个写端⼀个读端，写端位于解码线程，读端位于播放线程。

- FrameQueue的设计⽐如PacketQueue复杂，引⼊了读取节点但节点不出队列的操作、读取下⼀节点也不出队列等等的操作，FrameQueue操作提供以下⽅法：
## 2.1 frame_queue_unref_item：释放Frame⾥⾯的AVFrame和 AVSubtitle
## 2.2 frame_queue_init：初始化队列
- 队列初始化函数确定了队列⼤⼩，将为队列中每⼀个节点的frame( f->queue[i].frame )分配内存，注意只是分配Frame对象本身，⽽不关注Frame中的数据缓冲区。Frame中的数据缓冲区是AVBuffer，使⽤引⽤计数机制。
- f->max_size 是队列的⼤⼩，此处值为16（由FRAME_QUEUE_SIZE定义），实际分配的时候视频为3，⾳频为9，字幕为16，因为这⾥存储的是解码后的数据，不宜设置过⼤，⽐如视频当为1080p时，如果为YUV420p格式，⼀帧就有3110400字节。
    ```c
    #define VIDEO_PICTURE_QUEUE_SIZE 3 // 图像帧缓存数量
    #define SUBPICTURE_QUEUE_SIZE 16 // 字幕帧缓存数量
    #define SAMPLE_QUEUE_SIZE 9 // 采样帧缓存数量
    #define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE,FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))
    ```
- f->keep_last 是队列中是否保留最后⼀次播放的帧的标志。 f->keep_last = !!keep_last 是将int取值的keep_last转换为boot取值(0或1)。

## 2.3 frame_queue_destory：销毁队列

队列销毁函数对队列中的每个节点作了如下处理：
- frame_queue_unref_item(vp) 释放本队列对vp->frame中AVBuffer的引⽤
- av_frame_free(&vp->frame) 释放vp->frame对象本身

# FrameQueue写队列
FrameQueue写队列的步骤和PacketQueue不同，分了3步进⾏：
- 调⽤frame_queue_peek_writable获取可写的Frame，如果队列已满则等待
- 获取到Frame后，设置Frame的成员变量
- 再调⽤frame_queue_push更新队列的写索引，真正将Frame⼊队列

## frame_queue_peek_writable：获取⼀个可写Frame，可以以阻塞或⾮阻塞⽅式进⾏
```c
// 获取可写指针
static Frame *frame_queue_peek_writable(FrameQueue *f) {
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&      
           !f->pktq->abort_request) {    /* 检查是否需要退出 */
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)             /* 检查是不是要退出 */
        return NULL;

    return &f->queue[f->windex];
}
```
- 向队列尾部申请⼀个可写的帧空间，若⽆空间可写，则等待。
- 这⾥最需要体会到的是abort_request的使⽤，在等待时如果播放器需要退出则将abort_request = 1，那frame_queue_peek_writable函数可以知道是正常frame可写唤醒，还是其他唤醒。
- `这里是 f->size 没有用 frame_queue_nb_remaining。考虑了前一帧，算上了last。才不会被覆盖`

## frame_queue_push：更新写索引，此时Frame才真正⼊队列，队列节点Frame个数加1
```c
// 更新写指针
static void frame_queue_push(FrameQueue *f) {
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);    // 当_readable在等待时则可以唤醒
    SDL_UnlockMutex(f->mutex);
}
```
# FrameQueue读队列
- 写队列中，应⽤程序写⼊⼀个新帧后通常总是将写索引加1。⽽读队列中，“读取”和“更新读索引(同时删除旧帧)”⼆者是独⽴的，可以只读取⽽不更新读索引，也可以只更新读索引(只删除)⽽不读取（只有更新读索引的时候才真正释放对应的Frame数据）。⽽且读队列引⼊了是否保留已显示的最后⼀帧的机制，导致读队列⽐写队列要复杂很多。
- 读队列和写队列步骤是类似的，基本步骤如下：
    - 调⽤frame_queue_peek_readable获取可读Frame；
    - 如果需要更新读索引（出队列该节点）则调⽤frame_queue_peek_next；
- 读队列涉及如下函数
```c
Frame *frame_queue_peek_readable(FrameQueue *f); // 获取可读Frame指针(若读空则等待)
Frame *frame_queue_peek(FrameQueue *f); // 获取当前Frame指针
Frame *frame_queue_peek_next(FrameQueue *f); // 获取下⼀Frame指针
Frame *frame_queue_peek_last(FrameQueue *f); // 获取上⼀Frame指针
void frame_queue_next(FrameQueue *f); // 更新读索引(同时删除旧frame)
```

## frame_queue_peek_readable() 获取可读Frame
```c
static Frame *frame_queue_peek_readable(FrameQueue *f) {
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
```
- `这里是 f->size - f->rindex_shown。考虑了前一帧，算上了last。`

## frame_queue_nb_remaining：获取队列Frame节点个数
```c
/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f) {
    return f->size - f->rindex_shown;    // 注意这里为什么要减去f->rindex_shown
}
```
- rindex_shown为1时，队列中总是保留了最后⼀帧lastvp。需要注意的时候rindex_shown的值就是0或1，不存在变为2，3等的可能。在计算队列当前Frame数量是不包含lastvp
- rindex_shown的引⼊增加了读队列操作的理解难度。⼤多数读操作函数都会⽤到这个变量。
- 通过 FrameQueue.keep_last 和 FrameQueue.rindex_shown 两个变量实现了保留最后⼀次播放帧的机制。
- 是否启⽤keep_last机制是由全局变量 keep_last 值决定的，在队列初始化函数frame_queue_init() 中有 f->keep_last = !!keep_last; ，⽽在更新读指针函数frame_queue_next() 中如果启⽤keep_last机制，则 f->rindex_shown 值为1。

## frame_queue_next
```c
/* 释放当前frame，并更新读索引rindex，
 * 当keep_last为1, rindex_show为0时不去更新rindex,也不释放当前frame */
static void frame_queue_next(FrameQueue *f) {
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1; // 第一次进来没有更新，对应的frame就没有释放
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}
```
- 在启⽤keeplast时，如果rindex_shown为0则将其设置为1，并返回。此时并不会更新读索引。也就是说keeplast机制实质上也会占⽤着队列Frame的size，当调⽤frame_queue_nb_remaining()获取size时并不能将其计算⼊size；
- 释放Frame对应的数据（⽐如AVFrame的数据），但不释放Frame本身更新读索引
- 释放唤醒信号，以唤醒正在等待写⼊的线程。

## frame_queue_peek_last()获取上⼀帧
```c
/* 获取last Frame：
 * 当rindex_shown=0时，和frame_queue_peek效果一样
 * 当rindex_shown=1时，读取的是已经显示过的frame
 */
static Frame *frame_queue_peek_last(FrameQueue *f) {
    return &f->queue[f->rindex];    // 这时候才有意义
}
```

## rame_queue_peek()获取当前帧
```c
/* 获取队列当前Frame, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读 */
static Frame *frame_queue_peek(FrameQueue *f) {
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
```

## frame_queue_peek_next()获取下⼀帧
```c
/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
// 不管你什么时候调用，返回来肯定不是 NULL
static Frame *frame_queue_peek_next(FrameQueue *f) {
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}
```

# struct AudioParams ⾳频参数
```c
typedef struct AudioParams {
    int			freq;                   // 采样率
    int			channels;               // 通道数
    int64_t		channel_layout;         // 通道布局，比如2.1声道，5.1声道等
    enum AVSampleFormat	fmt;            // 音频采样格式，比如AV_SAMPLE_FMT_S16表示为有符号16bit深度，交错排列模式。
    int			frame_size;             // 一个采样单元占用的字节数（比如2通道时，则左右通道各采样一次合成一个采样单元）
    int			bytes_per_sec;          // 一秒时间的字节数，比如采样率48Khz，2 channel，16bit，则一秒48000*2*16/8=192000
} AudioParams;
```

# struct Decoder解码器封装
```c
typedef struct Decoder {
    AVPacket *pkt;
    PacketQueue *queue;             //数据包队列
    AVCodecContext *avctx;          //解码器上下文
    int pkt_serial;                 //包序列
    int finished;                   //等于0， 编码器处于工作状态
    int packet_pending;             //等于0， 编码器处于异常状态
    SDL_cond *empty_queue_cond;     //检查到队列为空时， 发送信号给read_thread读取数据
    int64_t start_pts;              //初始化是stream的start time
    AVRational start_pts_tb;        //初始化是stream的time base
    int64_t next_pts;               // 记录最近⼀次解码后的frame的pts，当解出来的部分帧没有有效的pts时使⽤next_pts进⾏推算
    AVRational next_pts_tb;         //next_pts的单位
    SDL_Thread *decoder_tid;        //线程句柄
} Decoder;
```


# 数据读取线程
## 准备工作
1. avformat_alloc_context 创建上下⽂
2. ic->interrupt_callback.callback = decode_interrupt_cb;
3. avformat_open_input打开媒体⽂件
4. avformat_find_stream_info 读取媒体⽂件的包获取更多的stream信息
5. 检测是否指定播放起始时间，如果指定时间则seek到指定位置avformat_seek_file
6. 查找AVStream，将对应的index值记录到st_index[AVMEDIA_TYPE_NB]；
    1. 根据⽤户指定来查找流avformat_match_stream_specifier
    2. 使⽤av_find_best_stream查找流
7. 从待处理流中获取相关参数，设置显示窗⼝的宽度、⾼度及宽⾼⽐
8. stream_component_open打开⾳频、视频、字幕解码器，并创建相应的解码线程以及进⾏对应输出参数的初始化



## for循环读取数据


## 退出线程处理