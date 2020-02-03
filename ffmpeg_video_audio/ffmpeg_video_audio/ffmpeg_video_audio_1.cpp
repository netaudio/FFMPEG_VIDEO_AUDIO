//����Ƶ���Ž���ѧϰ1
//ffmpeg_video_audio_1����FFMPEG��SDLʵ����Ƶ������Ƶ����ͬʱ����(��ͬ��)
//��һ��ʵ����Ƶ������Ƶ���Ĳ��ŵ�ͬ������Ƶ�ز����ο�FFMPE�ṩ��example��resampling_audio.c

#include "stdafx.h"
#include "ffmpeg_video_audio.h"

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

//���ݰ�����
typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

PacketQueue audioq; //��Ƶ����

int quit = 0;

//���г�ʼ������
void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

//��Ӻ���
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;               //���ݰ�������
	if (av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;


	SDL_LockMutex(q->mutex);   //����

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;

	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);        //�����ȴ��źŵı���

	SDL_UnlockMutex(q->mutex);    //����
	return 0;
}

//���Ӻ���
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pkt1;         //���ݰ�������
	int ret;

	SDL_LockMutex(q->mutex);    //����

	for (;;) {

		if (quit) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);      //�ȴ���������������
		}//end if
	}//end for
	SDL_UnlockMutex(q->mutex);
	return ret;
}


//��Ƶ���뺯��
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	static AVFrame *frame = av_frame_alloc();
	int got_frame = 0;

	int len1, data_size = 0;

	FILE *pFile = fopen("output.pcm", "a+");

	uint8_t **dst_data = NULL;
	int dst_linesize = 0;

	//test
	//int out_nb_samples;
	int64_t out_channel_layout;    //double channels
	AVSampleFormat out_sample_fmt;    //Audio sample formats
	int out_sample_rate;
	int out_channels;  //channel numbers
	struct SwrContext *aud_convert_ctx;
	static int pframm = 0;
	int ret = 0;

	//Audio out parameter:
	//nb_samples: AAC-1024 MP3-1152
	//out_nb_samples = aCodecCtx->frame_size;
	printf("%d\n",aCodecCtx->channels);
	out_channel_layout = AV_CH_LAYOUT_STEREO;
	out_sample_fmt = AV_SAMPLE_FMT_S16;    //Audio sample formats
	out_sample_rate = aCodecCtx->sample_rate;
	out_channels = av_get_channel_layout_nb_channels(out_channel_layout);  //channel numbers
	printf("%d\n", out_channels);
	//��Ϊ���������������4λ�����������ŵ���16λ��Ҫ�ز���
	aud_convert_ctx = swr_alloc_set_opts(NULL, out_channel_layout, out_sample_fmt, out_sample_rate,
		av_get_default_channel_layout(aCodecCtx->channels), aCodecCtx->sample_fmt, aCodecCtx->sample_rate, 0, NULL);
	swr_init(aud_convert_ctx);   //��ʼ��


	for (;;) {
		while (audio_pkt_size > 0) {			
			len1 = avcodec_decode_audio4(aCodecCtx, frame, &got_frame, &pkt);
			if (len1 < 0) {
				//�������
				audio_pkt_size = 0;
				break;
			}
			audio_pkt_data += len1;
			audio_pkt_size -= len1;


			ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, out_channels,
				out_sample_rate, (AVSampleFormat)out_sample_fmt, 0);

			if (got_frame)
			{
				//printf("Decode %d Frames\n", pframm++);
				ret = swr_convert(aud_convert_ctx, dst_data, out_sample_rate,(const uint8_t **)frame->data, frame->nb_samples);
				if (ret < 0)
				{
					continue;
				}
				//���ظ�����Ƶ�������ֽ���
				data_size = av_samples_get_buffer_size(&dst_linesize, out_channels,ret, (AVSampleFormat)out_sample_fmt, 1);			
				if (data_size <= 0) {
					continue;
				}		
			
			}
			memcpy(audio_buf, dst_data[0], data_size);
			/* We have data, return it and come back for more later */

			if (dst_data)
			{
				av_freep(&dst_data[0]);
			}
			av_freep(&dst_data);
			dst_data = NULL;

			swr_free(&aud_convert_ctx);
			return data_size;
		}//end while

		if (pkt.data)
			av_free_packet(&pkt);

		if (quit) {
			return -1;
		}

		if (packet_queue_get(&audioq, &pkt, 1) < 0) {        //�ڶ�����ȡ��һ������
			return -1;
		}
		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}//end for
}


//��Ƶ�Ļص�����
void audio_callback(void *userdata, Uint8 *stream, int len) {
	//SDL 2.0
	SDL_memset(stream, 0, len);

	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int len1, audio_size;

	//audio_buf_index �� audio_buf_size ��ʾ�����Լ��������ý�����������ݵĻ�������
	//��Щ���ݴ�copy��SDL�������� ��audio_buf_index >= audio_buf_size��ʱ����ζ����
	//�ǵĻ���Ϊ�գ�û�����ݿɹ�copy����ʱ����Ҫ����audio_decode_frame������������������ 
	static uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE*3/2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;
	
	while (len > 0) {
		if (audio_buf_index >= audio_buf_size) {
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);  //������Ƶ֡����
			
			//��ʾû�ܽ�������ݣ�Ĭ�ϲ��ž���
			if (audio_size < 0) {
				/* If error, output silence */
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
			}
			else {
				audio_buf_size = audio_size;
			}//end if
			audio_buf_index = 0;
		}//end if

		// �鿴stream���ÿռ䣬����һ��copy�������ݣ�ʣ�µ��´μ���copy 
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;		

		memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	} //end while
}

int main(int argc, char *argv[]) {
	AVFormatContext *pFormatCtx = NULL;
	int             i, index_video = -1, index_audio = -1;
	AVCodecContext  *pCodecCtx = NULL;
	AVCodec         *pCodec = NULL;
	AVFrame         *pFrame = NULL,*pFrameYUV = NULL;
	uint8_t         *out_buffer = NULL;
	AVPacket        *pkt;
	int             frameFinished;
	struct SwsContext *img_convert_ctx = NULL;

	//��Ƶ�����������
	AVCodecContext  *aCodecCtx = NULL;
	AVCodec         *aCodec = NULL;


	//SDL2.0
	SDL_Window      *screen;
	SDL_Renderer    *sdlrenderer;
	SDL_Texture     *sdltexture;
	SDL_Rect        sdlrect;
	SDL_Event       event;
	SDL_AudioSpec   desired,spec;
	
	//��Ƶ
	int out_nb_samples;
	int64_t out_channel_layout;    //double channels
	AVSampleFormat out_sample_fmt;    //Audio sample formats
	int out_sample_rate;
	int out_channels;  //channel numbers
	uint8_t *out_buffer_audio;
	int out_buffer_size;
	struct SwrContext *aud_convert_ctx;



	char * filename = "forrest.mp4";

	// Register all formats and codecs
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context(); //�����ڴ�

	if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) < 0) //��������Ƶ�ļ�
	{
		printf("Can't open the input stream.\n");
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL)<0)     //����Ƶ�ļ��еõ���ý����Ϣ
	{
		printf("Can't find the stream information!\n");
		return -1;
	}
	//��������Ϣ
	av_dump_format(pFormatCtx, 0, filename, 0);

	//��������Ƶ������
	for (i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)      //�������Ƶ�������¼��������
		{
			index_video = i;
		}
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)      //�������Ƶ�������¼��������
		{
			index_audio = i;
			break;
		}
	}
	if (index_video == -1)
	{
		printf("Can't find a video stream;\n");
		return -1;
	}
	if (index_audio == -1)
	{
		printf("Can't find a audio stream;\n");
		return -1;
	}
	//������Ƶ��������Ϣ
	aCodecCtx = pFormatCtx->streams[index_audio]->codec;

	//������Ƶ��������Ϣ
	pCodecCtx = pFormatCtx->streams[index_video]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);     //���ҽ�����
	if (pCodec == NULL)
	{
		printf("Can't find a decoder!\n");
		return -1;
	}

	//�򿪱�����
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		printf("Can't open the decoder!\n");
		return -1;
	}
	//�洢���������
	pFrame = av_frame_alloc();  //this only allocates the AVFrame itself, not the data buffers
	pFrameYUV = av_frame_alloc();
	out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));  //���ٻ�����
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);//֡��������ڴ���

	//�洢����ǰ����
	pkt = (AVPacket *)av_malloc(sizeof(AVPacket));;
	av_init_packet(pkt);
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//SDL2.0
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		exit(1);
	}
	int screen_w = pCodecCtx->width, screen_h = pCodecCtx->height;
	screen = SDL_CreateWindow("SDL EVENT TEST", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE); //������ʾ����
	if (screen == NULL)
	{
		printf("Can't creat a window:%s\n", SDL_GetError());
		return -1;
	}
	sdlrenderer = SDL_CreateRenderer(screen, -1, 0);//������Ⱦ��
	sdltexture = SDL_CreateTexture(sdlrenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);//��������


	desired.freq = aCodecCtx->sample_rate;        //��Ƶ���ݵĲ�����
	desired.format = AUDIO_S16SYS;		          //��Ƶ���ݵĸ�ʽ
	desired.channels = aCodecCtx->channels;       //������
	desired.silence = 0;                          //����ʱ��ֵ
	desired.samples = SDL_AUDIO_BUFFER_SIZE;      //��Ƶ�������еĲ�������
	desired.callback = audio_callback;            //�ص�����
	desired.userdata = aCodecCtx;
	
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);     //���ҽ�����
	if (aCodec == NULL)
	{
		printf("Can't find a decoder!\n");
		return -1;
	}
	if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0)   //�򿪱�����
	{
		printf("Can't open the decoder!\n");
		return -1;
	}

	if (SDL_OpenAudio(&desired, &spec)<0){
		printf("can't open audio.\n");
		return -1;
	}
	packet_queue_init(&audioq);
	SDL_PauseAudio(0);


	while (av_read_frame(pFormatCtx, pkt) >= 0) {
		// �ж��Ƿ�����Ƶ��
		if (pkt->stream_index == index_video) {
			// ������Ƶ������
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, pkt);
			// �õ�֡����
			if (frameFinished) {
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
					pFrameYUV->data, pFrameYUV->linesize);

				SDL_UpdateTexture(sdltexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]); //�������������

				//���ô��ڴ�С
				sdlrect.x = 0;
				sdlrect.y = 0;
				sdlrect.w = screen_w;
				sdlrect.h = screen_h;

				SDL_RenderCopy(sdlrenderer, sdltexture, NULL, &sdlrect); //����������Ϣ����Ⱦ��Ŀ��
				SDL_RenderPresent(sdlrenderer);//��Ƶ��Ⱦ��ʾ
				av_free_packet(pkt);
			}
		}else if (pkt->stream_index == index_audio) {
			packet_queue_put(&audioq, pkt);
		}
		else
		{
			av_free_packet(pkt);
		}
		SDL_Delay(10);

		SDL_PollEvent(&event);
		switch (event.type) {
		case SDL_QUIT:
			quit = 1;
			SDL_Quit();
			exit(0);
			break;
		default:
			break;
		}//end switch

	}//end while
	
	sws_freeContext(img_convert_ctx);

	// Free the YUV frame
	av_free(pFrame);
	av_frame_free(&pFrameYUV);
	// Close the codec
	avcodec_close(pCodecCtx);

	// Close the video file
	avformat_close_input(&pFormatCtx);

	return 0;
}
