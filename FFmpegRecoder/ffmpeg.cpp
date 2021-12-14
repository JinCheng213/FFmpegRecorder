#include "framework.h"
#include "FFmpegRecoder.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define STREAM_PIX_FMT		AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS			SWS_BICUBIC
#define MAX_WSTREAM_COUNT	2

#define STREAM_DURATION   1000.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */

typedef struct OutputStream {
	AVStream *st;
	AVCodecContext *enc;

	/* pts of the next frame that will be generated */
	int64_t next_pts;
	int samples_count;

	AVFrame *frame;
	AVFrame *tmp_frame;

	AVPacket *tmp_pkt;

	float t, tincr, tincr2;

	struct SwsContext *sws_ctx;
	struct SwrContext *swr_ctx;
} OutputStream; 

char		szFile[MAX_PATH];
char		errTxt[256];
bool		bFoundRecDevice;
bool		bStopFlag = false;
bool		bAudioEnabled = true;
bool		encode_video = false;
bool		encode_audio = false;

HWND		hMainDlg;

int			ScreenDPI = USER_DEFAULT_SCREEN_DPI;
double		DPIScaleFactorX = 1;

HANDLE		hBusy = NULL;

OutputStream video_st, audio_st;
AVOutputFormat *fmt;
AVFormatContext *oc;
AVCodec *audio_codec, *video_codec;
AVDictionary *opt = NULL;

static int select_sample_rate(const AVCodec *codec);
static int select_sample_rate(const AVCodec *codec);
static int select_channel_layout(const AVCodec *codec);
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);
static bool add_stream(OutputStream *ost, AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id);
static bool open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg);
static bool open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg);
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height);
static void close_stream(AVFormatContext *oc, OutputStream *ost);
static int write_video_frame(AVFormatContext *oc, OutputStream *ost);
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost);

void Warn(HWND hwnd, char *msg) {
	MessageBoxA(hwnd, msg, "Error", MB_ICONEXCLAMATION | MB_OK);
}

char *getErrorString(char *buf, int err) {
	char errStr[256];
	char errStr1[256];

	memset(errStr, 0, 256);
	memset(errTxt, 0, 256);
	memset(errStr1, 0, 256);

	av_strerror(err, errStr1, 256);

	switch (err)
	{
	case AVERROR_BSF_NOT_FOUND:
		strcpy(errStr, "BSF Not Found!");
		break;
	case AVERROR_BUFFER_TOO_SMALL:
		strcpy(errStr, "Buffer Too Small!");
		break;
	case AVERROR_BUG:
		strcpy(errStr, "BUG!");
		break;
	case AVERROR_BUG2:
		strcpy(errStr, "BUG2!");
		break;
	case AVERROR_DECODER_NOT_FOUND:
		strcpy(errStr, "Decoder Not Found!");
		break;
	case AVERROR_DEMUXER_NOT_FOUND:
		strcpy(errStr, "Demuxer Not Found!");
		break;
	case AVERROR_ENCODER_NOT_FOUND:
		strcpy(errStr, "Encoder Not Found!");
		break;
	case AVERROR_EOF:
		strcpy(errStr, "EOF!");
		break;
	case AVERROR_EXIT:
		strcpy(errStr, "Exit!");
		break;
	case AVERROR_EXPERIMENTAL:
		strcpy(errStr, "Experimental!");
		break;
	case AVERROR_EXTERNAL:
		strcpy(errStr, "External!");
		break;
	case AVERROR_FILTER_NOT_FOUND:
		strcpy(errStr, "BFilter Not Found!");
		break;
	case AVERROR_HTTP_BAD_REQUEST:
		strcpy(errStr, "HTTP Bad Request!");
		break;
	case AVERROR_HTTP_FORBIDDEN:
		strcpy(errStr, "HTTP Forbiden!");
		break;
	case AVERROR_HTTP_NOT_FOUND:
		strcpy(errStr, "HTTP Not Found!");
		break;
	case AVERROR_HTTP_OTHER_4XX:
		strcpy(errStr, "HTTP Other 4XX!");
		break;
	case AVERROR_INPUT_CHANGED:
		strcpy(errStr, "Input Changed!");
		break;
	case AVERROR_INVALIDDATA:
		strcpy(errStr, "Invalid Data!");
		break;
	case AVERROR_MUXER_NOT_FOUND:
		strcpy(errStr, "Muxer Not Found!");
		break;
	case AVERROR_OPTION_NOT_FOUND:
		strcpy(errStr, "Option Not Found!");
		break;
	case AVERROR_OUTPUT_CHANGED:
		strcpy(errStr, "Output Changed!");
		break;
	case AVERROR_PATCHWELCOME:
		strcpy(errStr, "Patch Welcome!");
		break;
	case AVERROR_PROTOCOL_NOT_FOUND:
		strcpy(errStr, "Protocol Not Found!");
		break;
	case AVERROR_STREAM_NOT_FOUND:
		strcpy(errStr, "Stream Not Found!");
		break;
	case AVERROR_UNKNOWN:
		strcpy(errStr, "Unknown!");
		break;
	default:
		strcpy(errStr, "Unknown!");
		break;
	}

	sprintf(errTxt, "%s-(%s)", buf, errStr1);
	return errTxt;
}

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c,
	AVStream *st, AVFrame *frame, AVPacket *pkt)
{
	int ret;

	// send the frame to the encoder
	ret = avcodec_send_frame(c, frame);
	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Error sending a frame to the encoder", ret));
		return -1;
	}

	while (ret >= 0) {
		ret = avcodec_receive_packet(c, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0) {
			Warn(hMainDlg, getErrorString((char *)"Error encoding a frame: %s\n", ret));
			return -1;
		}

		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(pkt, c->time_base, st->time_base);
		pkt->stream_index = st->index;

		/* Write the compressed frame to the media file. */
		//log_packet(fmt_ctx, pkt);
		ret = av_interleaved_write_frame(fmt_ctx, pkt);
		/* pkt is now blank (av_interleaved_write_frame() takes ownership of
		 * its contents and resets pkt), so that no unreferencing is necessary.
		 * This would be different if one used av_write_frame(). */
		if (ret < 0) {
			Warn(hMainDlg, getErrorString((char *)"Error while writing output packet: %s\n", ret));
			return -1;
		}
	}

	return ret == AVERROR_EOF ? 1 : 0;
}

static void fill_yuv_image(AVFrame *pict, int width, int height)
{
	int x, y;

	/* Y */
	for (y = 0; y < height; y++) {
		BYTE *ptr = vmm.pYuvBuffer + y * width;
		for (x = 0; x < width; x++) {
			pict->data[0][y * pict->linesize[0] + x] = ptr[x];
		}
	}

	/* Cb and Cr */
	for (y = 0; y < height / 2; y++) {
		BYTE *ptr = vmm.pYuvBuffer + width * height + y * width;
		for (x = 0; x < width / 2; x++) {
			pict->data[1][y * pict->linesize[1] + x] = ptr[x * 2];
			pict->data[2][y * pict->linesize[2] + x] = ptr[x * 2 + 1];
		}
	}
}

static AVFrame *get_video_frame(OutputStream *ost)
{
	AVCodecContext *c = ost->enc;
	AVRational avr;

	avr.den = 1;
	avr.num = 1;
	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, c->time_base,
		(int)STREAM_DURATION, avr) > 0)
		return NULL;

	/* when we pass a frame to the encoder, it may keep a reference to it
	 * internally; make sure we do not overwrite it here */
	if (av_frame_make_writable(ost->frame) < 0) {
		Warn(hMainDlg, (char *)"Unable to make writable frame.");
		return NULL;
	}

	//fill_bgr_image(ost->frame, c->width, c->height, frm);
	
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		// as we only generate a YUV420P picture, we must convert it
		// to the codec pixel format if needed 
		if (!ost->sws_ctx) {
			ost->sws_ctx = sws_getContext(c->width, c->height,
				AV_PIX_FMT_YUV420P,
				c->width, c->height,
				c->pix_fmt,
				SCALE_FLAGS, NULL, NULL, NULL);
			if (!ost->sws_ctx) {
				Warn(hMainDlg, getErrorString((char *)"Could not initialize the conversion context\n", AVERROR_UNKNOWN));
				return NULL;
			}
		}
		fill_yuv_image(ost->tmp_frame, c->width, c->height);
		sws_scale(ost->sws_ctx, (const uint8_t * const *)ost->tmp_frame->data,
			ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
			ost->frame->linesize);
	}
	else {
		fill_yuv_image(ost->frame, c->width, c->height);
	}
	
	ost->frame->pts = ost->next_pts++;

	return ost->frame;
}

bool InitMedia() {
	int ret;

	memset((void *)&video_st, 0, sizeof(OutputStream));
	memset((void *)&audio_st, 0, sizeof(OutputStream));
	/* Initialize libavcodec, and register all codecs and formats. */
	//av_register_all();
	//AVOutputFormat avo;
	av_dict_set(&opt, "fs", "100M", 0);

	/* allocate the output media context */
	avformat_alloc_output_context2(&oc, NULL, NULL, szFile);
	if (!oc) {
		avformat_alloc_output_context2(&oc, NULL, "mpeg", szFile);
	}
	if (!oc) {
		Warn(hMainDlg, (char *)"Could not deduce output format from file extension!");
		avformat_alloc_output_context2(&oc, NULL, "mpeg", szFile);
	}
	
	if (!oc) return false;

	fmt = oc->oformat;
	/* Add the audio and video streams using the default format codecs
	* and initialize the codecs. */
	if (fmt->video_codec != AV_CODEC_ID_NONE) {
		if (!add_stream(&video_st, oc, &video_codec, fmt->video_codec)) return false;
		encode_video = true;
	}
	if (fmt->audio_codec != AV_CODEC_ID_NONE && bAudioEnabled && bFoundRecDevice) {
		if (!add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec)) return false;
		encode_audio = true;
	}
	/* Now that all the parameters are set, we can open the audio and
	* video codecs and allocate the necessary encode buffers. */
	if (!open_video(oc, video_codec, &video_st, opt)) return false;
	if (bFoundRecDevice && bAudioEnabled)
		if (!open_audio(oc, audio_codec, &audio_st, opt)) return false;
	av_dump_format(oc, 0, szFile, 1);
	/* open the output file, if needed */
	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&oc->pb, szFile, AVIO_FLAG_WRITE);
		if (ret < 0) {
			Warn(hMainDlg, (char *)"Could not open MP4 file!");
			return false;
		}
	}
	/* Write the stream header, if any. */
	ret = avformat_write_header(oc, &opt);
	if (ret < 0) {
		Warn(hMainDlg, (char *)"Could not open MP4 file!");
		return false;
	}
	/* Write the trailer, if any. The trailer must be written before you
	* close the CodecContexts open when you wrote the header; otherwise
	* av_write_trailer() may try to use memory that was freed on
	* av_codec_close(). */

	return true;
}


int WriteMedia() {
	if (encode_video &&
		(!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
			audio_st.next_pts, audio_st.enc->time_base) <= 0)) {
		encode_video = !write_video_frame(oc, &video_st);
	}
	else if((int)amm.dwbufLength >= audio_st.frame->nb_samples) {
		encode_audio = !write_audio_frame(oc, &audio_st);
	}

	return 0;
}


void CloseMedia() {
	WaitForSingleObject(hBusy, INFINITE);
	av_write_trailer(oc);
	/* Close each codec. */
	close_stream(oc, &video_st);
	if (bFoundRecDevice && bAudioEnabled)
		close_stream(oc, &audio_st);
	if (!(fmt->flags & AVFMT_NOFILE))
		/* Close the output file. */
		avio_closep(&oc->pb);
	/* free the stream */
	avformat_free_context(oc);
}

void CloseMediaImmediate() {
	av_write_trailer(oc);
	/* Close each codec. */
	close_stream(oc, &video_st);
	if (bFoundRecDevice && bAudioEnabled)
		close_stream(oc, &audio_st);
	if (!(fmt->flags & AVFMT_NOFILE))
		/* Close the output file. */
		avio_closep(&oc->pb);
	/* free the stream */
	avformat_free_context(oc);
}

static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
	return write_frame(oc, ost->enc, ost->st, get_video_frame(ost), ost->tmp_pkt);
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
	avcodec_free_context(&ost->enc);
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
	av_packet_free(&ost->tmp_pkt);
	sws_freeContext(ost->sws_ctx);
	swr_free(&ost->swr_ctx);
}

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
	uint64_t channel_layout,
	int sample_rate, int nb_samples)
{
	AVFrame *frame = av_frame_alloc();
	int ret;

	if (!frame) {
		Warn(hMainDlg, getErrorString((char *)"Error allocating an audio frame\n", AVERROR_UNKNOWN));
		return NULL;
	}

	frame->format = sample_fmt;
	frame->channel_layout = channel_layout;
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;

	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			Warn(hMainDlg, getErrorString((char *)"Error allocating an audio buffer\n", ret));
			return NULL;
		}
	}

	return frame;
}

static bool open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	AVCodecContext *c;
	int nb_samples;
	int ret;
	AVDictionary *opt = NULL;

	c = ost->enc;

	/* open it */
	av_dict_copy(&opt, opt_arg, 0);
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Could not open audio codec: %s\n", ret));
		return false;
	}

	/* init signal generator */
	ost->t = 0;
	ost->tincr = (float)(2 * M_PI * 110.0 / c->sample_rate);
	/* increment frequency by 110 Hz per second */
	ost->tincr2 = (float)(2 * M_PI * 110.0 / c->sample_rate / c->sample_rate);

	if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
		nb_samples = 10000;
	else
		nb_samples = c->frame_size;

	ost->frame = alloc_audio_frame(c->sample_fmt, c->channel_layout,
		c->sample_rate, nb_samples);
	ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
		c->sample_rate, nb_samples);

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Could not copy the stream parameters\n", ret));
		return false;
	}

	/* create resampler context */
	ost->swr_ctx = swr_alloc();
	if (!ost->swr_ctx) {
		Warn(hMainDlg, getErrorString((char *)"Could not allocate resampler context\n", AVERROR_UNKNOWN));
		return false;
	}

	/* set options */
	av_opt_set_int(ost->swr_ctx, "in_channel_count", c->channels, 0);
	av_opt_set_int(ost->swr_ctx, "in_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	av_opt_set_int(ost->swr_ctx, "out_channel_count", c->channels, 0);
	av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);

	/* initialize the resampling context */
	if ((ret = swr_init(ost->swr_ctx)) < 0) {
		Warn(hMainDlg, getErrorString((char *)"Failed to initialize the resampling context\n", ret));
		return false;
	}

	return true;
}

static AVFrame *get_audio_frame(OutputStream *ost)
{
	AVFrame *frame = ost->tmp_frame;
	int j, i;
	int16_t *q = (int16_t*)frame->data[0];
	AVRational avr;

	avr.den = 1;
	avr.num = 1;
	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, ost->enc->time_base,
		(int64_t)STREAM_DURATION, avr) > 0)
		return NULL;


	for (j = 0; j < frame->nb_samples; j++)
	{
		//int n = (int)((float)j * den);
		//int v = 10000 - (int)(sin(ost->t * 10) * 10000);
		for (i = 0; i < ost->enc->channels; i++) {
			*q++ = amm.pAudioBuffer[j] * 500;// lpAudioData[j] * 10;
		}
		ost->t += ost->tincr;
		ost->tincr += ost->tincr2;
	}

	int rest = amm.dwbufLength - frame->nb_samples;
	memcpy(amm.pAudioBuffer, amm.pAudioBuffer + frame->nb_samples, rest);
	amm.dwbufLength = rest;

	frame->pts = ost->next_pts;
	ost->next_pts += frame->nb_samples;

	return frame;
}
/*
* encode one audio frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
	AVCodecContext *c;
	AVFrame *frame;
	AVRational avr;
	int ret;
	int dst_nb_samples;

	c = ost->enc;

	frame = get_audio_frame(ost);

	if (frame) {
		/* convert samples from native format to destination codec format, using the resampler */
		/* compute destination number of samples */
		dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
			c->sample_rate, c->sample_rate, AV_ROUND_UP);
		av_assert0(dst_nb_samples == frame->nb_samples);

		/* when we pass a frame to the encoder, it may keep a reference to it
		 * internally;
		 * make sure we do not overwrite it here
		 */
		ret = av_frame_make_writable(ost->frame);
		if (ret < 0)
			return -1;

		/* convert to destination format */
		ret = swr_convert(ost->swr_ctx,
			ost->frame->data, dst_nb_samples,
			(const uint8_t **)frame->data, frame->nb_samples);
		if (ret < 0) {
			Warn(hMainDlg, getErrorString((char *)"Error while converting\n", ret));
			return -1;
		}
		frame = ost->frame;
		avr.num = 1;
		avr.den = c->sample_rate;
		frame->pts = av_rescale_q(ost->samples_count, avr, c->time_base);
		ost->samples_count += dst_nb_samples;
	}

	return write_frame(oc, c, ost->st, frame, ost->tmp_pkt);
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;

	picture = av_frame_alloc();
	if (!picture)
		return NULL;

	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;

	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 0);
	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Could not allocate frame data.\n", ret));
		return NULL;
	}

	return picture;
}

static bool open_video(AVFormatContext *oc, AVCodec *codec,
	OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	AVCodecContext *c = ost->enc;
	AVDictionary *opt = NULL;

	av_dict_copy(&opt, opt_arg, 0);

	/* open the codec */
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);

	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Could not open video codec", ret));
		return false;
	}

	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		Warn(hMainDlg, getErrorString((char *)"Could not allocate video frame\n", AVERROR_UNKNOWN));
		return false;
	}

	/* If the output format is not YUV420P, then a temporary YUV420P
	 * picture is needed too. It is then converted to the required
	 * output format. */
	ost->tmp_frame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame) {
			Warn(hMainDlg, getErrorString((char *)"Could not allocate temporary picture\n", AVERROR_UNKNOWN));
			return false;
		}
	}

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		Warn(hMainDlg, getErrorString((char *)"Could not copy the stream parameters\n", ret));
		return false;
	}

	return true;
}

static bool add_stream(OutputStream *ost, AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id)
{
	AVCodecContext *c;
	int i;
	/* find the encoder */

	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		Warn(hMainDlg, (char *)"Could not find encoder!");
		return false;
	}

	ost->tmp_pkt = av_packet_alloc();
	if (!ost->tmp_pkt) {
		Warn(hMainDlg, (char *)"Could not allocate AVPacket");
		exit(1);
	}

	ost->st = avformat_new_stream(oc, NULL);
	if (!ost->st) {
		Warn(hMainDlg, (char *)"Could not allocate stream");
		return false;
	}
	ost->st->id = oc->nb_streams - 1;
	c = avcodec_alloc_context3(*codec);
	if (!c) {
		Warn(hMainDlg, (char *)"Could not alloc an encoding context");
		return false;
	}
	ost->enc = c;
	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate = 60000;
		c->sample_rate = 11025;
		if ((*codec)->supported_samplerates) {
			c->sample_rate = (*codec)->supported_samplerates[0];
			for (i = 0; (*codec)->supported_samplerates[i]; i++) {
				if ((*codec)->supported_samplerates[i] == 11025)
					c->sample_rate = 11025;
			}
		}
		c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
		c->channel_layout = AV_CH_LAYOUT_STEREO;
		if ((*codec)->channel_layouts) {
			c->channel_layout = (*codec)->channel_layouts[0];
			for (i = 0; (*codec)->channel_layouts[i]; i++) {
				if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
					c->channel_layout = AV_CH_LAYOUT_STEREO;
			}
		}
		c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
		ost->st->time_base.num = 1;// (AVRational){ 1, c->sample_rate };
		ost->st->time_base.den = c->sample_rate;
		break;
	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;
		c->bit_rate = 10000000;// 400000;
		/* Resolution must be a multiple of two. */
		c->width = width;
		c->height = height;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		* of which frame timestamps are represented. For fixed-fps content,
		* timebase should be 1/framerate and timestamp increments should be
		* identical to 1. */

		AVRational avr;

		avr.den = STREAM_FRAME_RATE;
		avr.num = 1;
		ost->st->time_base = avr;
		c->time_base = ost->st->time_base;
		c->gop_size = 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = STREAM_PIX_FMT;

		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B-frames */
			c->max_b_frames = 2;
		}

		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			* This does not happen with normal video, it just happens here as
			* the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
		break;
	default:
		break;
	}
	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	return true;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec)
{
	const int *p;
	int best_samplerate = 0;

	if (!codec->supported_samplerates)
		return 44100;

	p = codec->supported_samplerates;
	while (*p) {
		if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
			best_samplerate = *p;
		p++;
	}
	return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec)
{
	const uint64_t *p;
	uint64_t best_ch_layout = 0;
	int best_nb_channels = 0;

	if (!codec->channel_layouts)
		return AV_CH_LAYOUT_STEREO;

	p = codec->channel_layouts;
	while (*p) {
		int nb_channels = av_get_channel_layout_nb_channels(*p);

		if (nb_channels > best_nb_channels) {
			best_ch_layout = *p;
			best_nb_channels = nb_channels;
		}
		p++;
	}
	return (int)best_ch_layout;
}

static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat *p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}
