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


DeckLinkCaptureDelegate::DeckLinkCaptureDelegate(IDeckLink* decklink, IDeckLinkInput* decklinkInput, JavaVM* vm, jobject obj, jmethodID methodID) :
    m_refCount(1),
    decklink(decklink),
    decklinkInput(decklinkInput),
    vm(vm),
    obj(obj),
    methodID(methodID)
{
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
    void*								frameBytes;

	// Handle Video Frame
	if (videoFrame)
    {

        JNIEnv* env = getEnv(vm);
        if(env == 0)
        {
            // Cannot throw a runtime exception because we don't have an env
            std::cerr << "Cannot load env" << std::endl;
            return S_OK;
        }

		if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
		{
            printf("Frame received - No input signal detected\n");

            env->CallVoidMethod(obj, methodID, false, 0, 0);
		}
		else
        {

            printf("Frame received - Size: %li bytes\n",
				videoFrame->GetRowBytes() * videoFrame->GetHeight());

				videoFrame->GetBytes(&frameBytes);
                //write(g_videoOutputFile, frameBytes, videoFrame->GetRowBytes() * videoFrame->GetHeight());
            env->CallVoidMethod(obj, methodID, true, videoFrame->GetWidth(), videoFrame->GetHeight(), videoFrame->GetRowBytes());

		}

	}


	return S_OK;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags formatFlags)
{
	return S_OK;
}

void DeckLinkCaptureDelegate::Stop()
{
    decklinkInput->StopStreams();
    decklinkInput->DisableVideoInput();

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


    JNIEnv* env = getEnv(vm);
    env->DeleteGlobalRef(obj);
    releaseEnv(vm);


}

JNIEXPORT void JNICALL Java_us_ihmc_javadecklink_Capture_stopCaptureNative
  (JNIEnv *, jobject, jlong delegatePtr)
{
    DeckLinkCaptureDelegate* delegate = (DeckLinkCaptureDelegate*) delegatePtr;
    delegate->Stop();
}

JNIEXPORT jlong JNICALL Java_us_ihmc_javadecklink_Capture_startCaptureNative
  (JNIEnv *env, jobject obj, jint idx, jint displayModeId)
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

    JavaVM* vm;
    JNIassert(env, env->GetJavaVM(&vm) == 0);

    jobject globalObj = env->NewGlobalRef(obj);
    jclass target = env->GetObjectClass(globalObj);
    JNIassert(env, target != NULL);

    jmethodID methodID = env->GetMethodID(target, "receivedFrameFromNative", "(ZII)V");
    JNIassert(env, methodID != NULL);


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
    delegate = new DeckLinkCaptureDelegate(deckLink, g_deckLinkInput, vm, globalObj, methodID);
	g_deckLinkInput->SetCallback(delegate);

    // Start capturing
    result = g_deckLinkInput->EnableVideoInput(displayMode->GetDisplayMode(), bmdFormat8BitYUV, bmdVideoInputFlagDefault);
    if (result != S_OK)
    {
        fprintf(stderr, "Failed to enable video input. Is another application using the card?\n");
        goto bail;
    }

    result = g_deckLinkInput->StartStreams();
    if (result != S_OK)
        goto bail;



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
