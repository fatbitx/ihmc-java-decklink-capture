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

#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#include "DeckLinkAPI.h"
#include <jni.h>
#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
}

static int sws_flags = SWS_BICUBIC;

class DeckLinkCaptureDelegate : public IDeckLinkInputCallback
{
public:
    DeckLinkCaptureDelegate(std::string filename, double quality, IDeckLink*, IDeckLinkInput*decklinkInput, JavaVM* vm, jobject obj, jmethodID methodID, jmethodID stop);

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE  Release(void);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*);

    virtual int64_t getHardwareTime();

    virtual ~DeckLinkCaptureDelegate();

    virtual void Stop();
    JavaVM* vm;
    jobject obj;

    bool valid;
private:
	int32_t				m_refCount;

    IDeckLink* decklink;
    IDeckLinkInput* decklinkInput;


    jmethodID methodID;
    jmethodID stop;
    int quality;

    AVCodec *codec;
    AVCodecContext *c= NULL;
    AVPacket pkt;

    AVFrame *pictureUYVY;
    AVFrame *pictureYUV420;

    AVFormatContext *oc;
    AVStream *video_st;

    struct SwsContext *img_convert_ctx;

    int64_t initial_video_pts;

};

#endif
