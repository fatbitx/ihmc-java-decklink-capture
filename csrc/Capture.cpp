/* -LICENSE-START-
** Copyright (c) 2013 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
**
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <jni.h>
#include <string>



#include "DeckLinkAPI.h"
#include "Capture.h"
#include "Util.hpp"
#include "us_ihmc_javadecklink_Capture.h"

#include <time.h>



class ThreadJNIEnv {
public:
    DeckLinkCaptureDelegate *delegate;
    JNIEnv *env;

    ThreadJNIEnv(DeckLinkCaptureDelegate* delegate) :
        delegate(delegate)
    {
        std::cout << "Attaching thread" << std::endl;
        delegate->vm->AttachCurrentThread((void **) &env, NULL);
    }

    ~ThreadJNIEnv() {
        std::cout << "Finalizing capture" << std::endl;
        env->DeleteGlobalRef(delegate->obj);
        JavaVM* vm = delegate->vm;
        delete delegate;
        vm->DetachCurrentThread();

    }
};

static boost::thread_specific_ptr<ThreadJNIEnv> envs;

inline JNIEnv* registerDecklinkDelegate(DeckLinkCaptureDelegate* delegate)
{
    ThreadJNIEnv *ret = envs.get();
    if(ret)
    {
        return ret->env;
    }
    else
    {
        ret = new ThreadJNIEnv(delegate);
        envs.reset(ret);
        return ret->env;
    }
}


DeckLinkCaptureDelegate::DeckLinkCaptureDelegate(std::string filename, double quality, IDeckLink* decklink, IDeckLinkInput* decklinkInput, JavaVM* vm, jobject obj, jmethodID methodID) :
    vm(vm),
    obj(obj),
    m_refCount(1),
    decklink(decklink),
    decklinkInput(decklinkInput),
    methodID(methodID),
    initial_video_pts(AV_NOPTS_VALUE),
    quality(quality)
{
    av_register_all();
    avcodec_register_all();

    oc = avformat_alloc_context();
    oc->oformat = av_guess_format("mp4", NULL, NULL);


    if(oc->oformat == NULL)
    {
        fprintf(stderr, "AV Format mp4 not found\n");
        exit(1);
    }

    oc->oformat->video_codec = AV_CODEC_ID_MJPEG;
    snprintf(oc->filename, sizeof(oc->filename), "%s", filename.c_str());
    oc->oformat->audio_codec = AV_CODEC_ID_NONE;




}

ULONG DeckLinkCaptureDelegate::AddRef(void)
{
	return __sync_add_and_fetch(&m_refCount, 1);
}

ULONG DeckLinkCaptureDelegate::Release(void)
{
	int32_t newRefValue = __sync_sub_and_fetch(&m_refCount, 1);
	if (newRefValue == 0)
	{
        delete this;
		return 0;
	}
	return newRefValue;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioFrame)
{
    void* frameBytes;


    JNIEnv* env = registerDecklinkDelegate(this);
    if(env == 0)
    {
        // Cannot throw a runtime exception because we don't have an env
        std::cerr << "Cannot load env" << std::endl;
        return S_OK;
    }

	// Handle Video Frame
    if (videoFrame)
    {


		if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
		{
            printf("Frame received - No input signal detected\n");

            //env->CallVoidMethod(obj, methodID, false, 0, 0, NULL);
		}
		else
        {
				videoFrame->GetBytes(&frameBytes);
                //write(g_videoOutputFile, frameBytes, videoFrame->GetRowBytes() * videoFrame->GetHeight());


                av_init_packet(&pkt);
                pkt.data = NULL;    // packet data will be allocated by the encoder
                pkt.size = 0;

                BMDTimeValue frameTime;
                BMDTimeValue frameDuration;
                videoFrame->GetStreamTime(&frameTime, &frameDuration, video_st->time_base.den);
                int64_t pts;
                pts = frameTime / video_st->time_base.num;




                if (initial_video_pts == AV_NOPTS_VALUE) {
                    initial_video_pts = pts;
                }

                pts -= initial_video_pts;

                pictureUYVY->pts = pts;
                pictureYUV420->pts = pts;
                pkt.pts = pkt.dts = pts;

                avpicture_fill((AVPicture*)pictureUYVY, (uint8_t*) frameBytes, AV_PIX_FMT_UYVY422, pictureUYVY->width, pictureUYVY->height);
                sws_scale(img_convert_ctx, pictureUYVY->data, pictureUYVY->linesize, 0, c->height, pictureYUV420->data, pictureYUV420->linesize);


                pictureYUV420->quality = quality;
                /* encode the image */
                int got_output;
                int ret = avcodec_encode_video2(c, &pkt, pictureYUV420, &got_output);
                if (ret < 0) {
                    fprintf(stderr, "error encoding frame\n");
                }
                else if (got_output) {
                    av_write_frame(oc, &pkt);
                    av_free_packet(&pkt); //depreacted, use av_packet_unref(&pkt); after Ubuntu 16.04 comes out


                    videoFrame->GetHardwareReferenceTimestamp(1000000000, &frameTime, &frameDuration);
                    env->CallVoidMethod(obj, methodID, frameTime, pts);
                }


		}
	}



	return S_OK;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags formatFlags)
{
    // This only gets called if bmdVideoInputEnableFormatDetection was set
    // when enabling video input
    HRESULT	result;
    char*	displayModeName = NULL;
    BMDPixelFormat	pixelFormat = bmdFormat8BitYUV;

    JNIEnv* env = registerDecklinkDelegate(this);
    if (formatFlags & bmdDetectedVideoInputRGB444)
    {
        throwRuntimeException(env, "Unsupported input format: RGB444");
        decklinkInput->DisableVideoInput();
        return S_OK;
    }

    mode->GetName((const char**)&displayModeName);
    printf("Video format changed to %s %s\n", displayModeName, formatFlags & bmdDetectedVideoInputRGB444 ? "RGB" : "YUV");



    if (displayModeName)
        free(displayModeName);

    if (decklinkInput)
    {
        decklinkInput->StopStreams();

        if(codec)
        {
            throwRuntimeException(env, "Cannot change video resolution while capturing. Stopping capture.");
            decklinkInput->DisableVideoInput();
            return S_OK;
        }

        codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

        if (!codec) {
            throwRuntimeException(env, "codec not found\n");
            decklinkInput->DisableVideoInput();
            return S_OK;
        }

        video_st = avformat_new_stream(oc, codec);
        if(!video_st)
        {
            throwRuntimeException(env, "Cannot allocate video stream");
            decklinkInput->DisableVideoInput();
            return S_OK;

        }

        c = video_st->codec;




        /* put sample parameters */
        /* resolution must be a multiple of two */
        c->width = mode->GetWidth();
        c->height = mode->GetHeight();
        /* frames per second */

        BMDTimeValue numerator;
        BMDTimeScale denumerator;

        mode->GetFrameRate(&numerator, &denumerator);


        c->time_base.den = denumerator;
        c->time_base.num = numerator;
        c->pix_fmt = AV_PIX_FMT_YUVJ420P;

        if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        {
            c->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }

        c->flags |= CODEC_FLAG_QSCALE;
        c->qmin = c->qmax = quality;

        /* open it */
        if (avcodec_open2(c, codec, NULL) < 0) {
            throwRuntimeException(env, "Could not open codec");
            decklinkInput->DisableVideoInput();
            return S_OK;
        }

        if(avio_open(&oc->pb, oc->filename, AVIO_FLAG_WRITE) < 0)
        {
            throwRuntimeException(env, "Could not open file");
            decklinkInput->DisableVideoInput();
            return S_OK;

        }

        pictureYUV420 = avcodec_alloc_frame();
       int ret = av_image_alloc(pictureYUV420->data, pictureYUV420->linesize, c->width, c->height,
                             c->pix_fmt, 32);
        if (ret < 0) {
            throwRuntimeException(env, "could not alloc raw picture buffer\n");
            decklinkInput->DisableVideoInput();
            return S_OK;

        }
        pictureYUV420->format = c->pix_fmt;
        pictureYUV420->width  = c->width;
        pictureYUV420->height = c->height;


        pictureUYVY = avcodec_alloc_frame();
        pictureUYVY->width = c->width;
        pictureUYVY->height = c->height;
        pictureUYVY->format = AV_PIX_FMT_UYVY422;


        img_convert_ctx = sws_getContext(c->width, c->height,
        AV_PIX_FMT_UYVY422,
        c->width, c->height,
        c->pix_fmt,
        sws_flags, NULL, NULL, NULL);

        avformat_write_header(oc, NULL);




        result = decklinkInput->EnableVideoInput(mode->GetDisplayMode(), pixelFormat, bmdVideoInputFlagDefault | bmdVideoInputEnableFormatDetection);
        if (result != S_OK)
        {
            throwRuntimeException(env, "Failed to switch to new video mode");
            decklinkInput->DisableVideoInput();
            return S_OK;

        }

        decklinkInput->StartStreams();
    }

    std::cout << "Detected new mode " << mode->GetWidth() << "x" << mode->GetHeight() << std::endl;
	return S_OK;
}

int64_t DeckLinkCaptureDelegate::getHardwareTime()
{
    if(decklinkInput)
    {
        BMDTimeValue hardwareTime;
        BMDTimeValue timeInFrame;
        BMDTimeValue ticksPerFrame;
        if(decklinkInput->GetHardwareReferenceClock(1000000000, &hardwareTime, &timeInFrame, &ticksPerFrame) == S_OK)
        {
            return (int64_t) hardwareTime;
        }
    }

    return -1;
}

void DeckLinkCaptureDelegate::Stop()
{
    printf("Stopping capture\n");
    decklinkInput->StopStreams();
    decklinkInput->DisableVideoInput();
}

DeckLinkCaptureDelegate::~DeckLinkCaptureDelegate()
{
    if(oc)
    {
        if(oc->pb->write_flag)
        {
            av_write_trailer(oc);
            avio_close(oc->pb);
        }
        avformat_free_context(oc);
    }

    if(c != NULL)
    {
        avcodec_close(c);
        av_free(c);
    }

    if(pictureYUV420 != NULL)
    {
        av_freep(&pictureYUV420->data[0]);
        avcodec_free_frame(&pictureYUV420);
    }

    if(pictureUYVY != NULL)
    {
        avcodec_free_frame(&pictureUYVY);
    }


    if(video_st != NULL)
    {
        av_freep(video_st);
    }


    if(oc != NULL)
    {
        av_free(oc);
    }

    sws_freeContext(img_convert_ctx);


    if (decklinkInput != NULL)
    {
        decklinkInput->Release();
        decklinkInput = NULL;
    }

    if (decklink != NULL)
    {
        decklink->Release();
        decklink = NULL;
    }
}


JNIEXPORT jlong JNICALL Java_us_ihmc_javadecklink_Capture_getHardwareTime
  (JNIEnv *, jobject, jlong ptr)
{
    return ((DeckLinkCaptureDelegate*) ptr)->getHardwareTime();
}


JNIEXPORT void JNICALL Java_us_ihmc_javadecklink_Capture_stopCaptureNative
  (JNIEnv *, jobject, jlong delegatePtr)
{
    DeckLinkCaptureDelegate* delegate = (DeckLinkCaptureDelegate*) delegatePtr;
    delegate->Stop();
}


JNIEXPORT jlong JNICALL Java_us_ihmc_javadecklink_Capture_startCaptureNative
  (JNIEnv *env, jobject obj, jstring filename, jint idx, jint quality)
{

	IDeckLinkIterator*				deckLinkIterator = NULL;
	IDeckLink*						deckLink = NULL;

	IDeckLinkDisplayModeIterator*	displayModeIterator = NULL;
	IDeckLinkDisplayMode*			displayMode = NULL;
    char*							displayModeName = NULL;
	BMDDisplayModeSupport			displayModeSupported;

	DeckLinkCaptureDelegate*		delegate = NULL;

    HRESULT                                                 result;

    IDeckLinkInput*	g_deckLinkInput = NULL;

    int displayModeId;

    JavaVM* vm;
    JNIassert(env, env->GetJavaVM(&vm) == 0);

    const char* str = env->GetStringUTFChars(filename,0);
    std::string cfilename(str);
    env->ReleaseStringUTFChars(filename, str);

    jclass cls = env->GetObjectClass(obj);
    jmethodID method = env->GetMethodID(cls, "receivedFrameAtHardwareTimeFromNative", "(JJ)V");
    if(!method)
    {
        throwRuntimeException(env, "Cannot find method receivedFrameAtHardwareTimeFromNative");
        goto bail;
    }



	// Get the DeckLink device
	deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (!deckLinkIterator)
	{
		fprintf(stderr, "This application requires the DeckLink drivers installed.\n");
		goto bail;
	}


	while ((result = deckLinkIterator->Next(&deckLink)) == S_OK)
	{
		if (idx == 0)
			break;
		--idx;

		deckLink->Release();
	}

	if (result != S_OK || deckLink == NULL)
	{
        fprintf(stderr, "Unable to get DeckLink device %u\n", idx);
		goto bail;
	}

	// Get the input (capture) interface of the DeckLink device
    result = deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&g_deckLinkInput);
	if (result != S_OK)
		goto bail;

    result = g_deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK)
		goto bail;

    displayModeId = 0;
	while ((result = displayModeIterator->Next(&displayMode)) == S_OK)
	{
        if (displayModeId == 0)
			break;
        --displayModeId;

		displayMode->Release();
	}

	if (result != S_OK || displayMode == NULL)
	{
        fprintf(stderr, "Unable to get display mode %d\n", displayModeId);
		goto bail;
	}

	// Get display mode name
	result = displayMode->GetName((const char**)&displayModeName);
	if (result != S_OK)
	{
		displayModeName = (char *)malloc(32);
        snprintf(displayModeName, 32, "[index %d]", displayModeId);
	}

	// Check display mode is supported with given options
    result = g_deckLinkInput->DoesSupportVideoMode(displayMode->GetDisplayMode(), bmdFormat8BitYUV, bmdVideoInputFlagDefault, &displayModeSupported, NULL);
	if (result != S_OK)
		goto bail;

	if (displayModeSupported == bmdDisplayModeNotSupported)
	{
		fprintf(stderr, "The display mode %s is not supported with the selected pixel format\n", displayModeName);
		goto bail;
	}

	// Configure the capture callback
    delegate = new DeckLinkCaptureDelegate(cfilename, quality, deckLink, g_deckLinkInput, vm, env->NewGlobalRef(obj), method);



	g_deckLinkInput->SetCallback(delegate);

    // Start capturing
    result = g_deckLinkInput->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, bmdVideoInputFlagDefault | bmdVideoInputEnableFormatDetection);
    if (result != S_OK)
    {
        fprintf(stderr, "Failed to enable video input. Is another application using the card?\n");
        delete delegate;
        goto bail;
    }

    result = g_deckLinkInput->StartStreams();
    if (result != S_OK)
    {
        delete delegate;
        goto bail;

    }

bail:

	if (displayModeName != NULL)
		free(displayModeName);

	if (displayMode != NULL)
		displayMode->Release();

	if (displayModeIterator != NULL)
		displayModeIterator->Release();


    if (deckLinkIterator != NULL)
		deckLinkIterator->Release();

    return (jlong) delegate;
}
