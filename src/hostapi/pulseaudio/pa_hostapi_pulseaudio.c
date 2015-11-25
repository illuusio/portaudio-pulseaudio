/*
 * $Id: pa_hostapi_pulseaudio.c 1668 2011-05-02 17:07:11Z rossb $
 * PulseAudio host to play natively in Linux based systems without
 * ALSA emulation
 *
 * Copyright (c) 2014 Tuukka Pasanen <tuukka.pasanen@ilmi.fi>
 *
 * Based on the Open Source API proposed by Ross Bencina
 * Copyright (c) 1999-2002 Ross Bencina, Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

/** @file
 @ingroup common_src

 @brief PulseAudio implementation of support for a host API.

 This host API implements PulseAudio support for portaudio
 it has callbackmode and normal write mode support
*/


#include <string.h> /* strlen() */


#include "pa_hostapi_pulseaudio.h"
#include "pa_hostapi_pulseaudio_cb.h"
#include "pa_hostapi_pulseaudio_block.h"


/* PulseAudio headers */
#include <stdio.h>
#include <string.h>
#include <pulse/pulseaudio.h>

/* This is used to identify process name for PulseAudio. */
extern char *__progname;

/* PulseAudio specific functions */
int PulseAudioCheckConnection(PaPulseAudioHostApiRepresentation *ptr)
{
    pa_context_state_t state;

    assert(ptr);

    /* Sanity check if pre if NULL don't go anywhere or
       it will SIGSEGV
     */
    if(!ptr)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "Host API is NULL! Can't do anything about it");
        return -1;
    }

    if (!ptr->context || !ptr->mainloop)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio context or mainloop are NULL");
        return -1;
    }

    state = pa_context_get_state(ptr->context);

    if (!PA_CONTEXT_IS_GOOD(state))
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio has general error");
        return -1;
    }

    return 0;
}

PaPulseAudioHostApiRepresentation *PulseAudioNew(void)
{
    PaPulseAudioHostApiRepresentation *ptr;
    int fd[2] = { -1, -1 };
    char proc[PATH_MAX];
    char buf[PATH_MAX + 20];

    ptr = (PaPulseAudioHostApiRepresentation*)PaUtil_AllocateMemory(sizeof(PaPulseAudioHostApiRepresentation));

    /* prt is NULL if runs out of memory or pointer to allocated memory */
    if (!ptr)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio can't alloc memory");
	return NULL;
    }

    /* Make sure we have NULL all struct first */
    memset(ptr, 0x00, sizeof(PaPulseAudioHostApiRepresentation));

    ptr->mainloop = pa_threaded_mainloop_new();

    if (!ptr->mainloop)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio can't alloc mainloop");
        goto fail;
    }

    memset(buf, 0x00, PATH_MAX + 20);
    snprintf(buf, sizeof(buf), "%s", __progname);

    ptr->context =
        pa_context_new(pa_threaded_mainloop_get_api(ptr->mainloop), buf);

    if (!ptr->context)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio can't alloc context");
        goto fail;
    }

    pa_context_set_state_callback(ptr->context, PulseAudioCheckContextStateCb, ptr);


    if (pa_threaded_mainloop_start(ptr->mainloop) < 0)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudio can't start mainloop");
        goto fail;
    }

    ptr->deviceCount = 0;

    return ptr;

fail:
    PulseAudioFree(ptr);
    return NULL;
}

void PulseAudioFree(PaPulseAudioHostApiRepresentation *ptr)
{
    assert(ptr);

    /* Sanity check if pre if NULL don't go anywhere or
       it will SIGSEGV
     */
    if(!ptr)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "Host API is NULL! Can't do anything about it");
        return;
    }

    if (ptr->mainloop)
    {
        pa_threaded_mainloop_stop(ptr->mainloop);
    }

    if (ptr->context)
    {
        pa_context_disconnect(ptr->context);
        pa_context_unref(ptr->context);
    }

    if (ptr->mainloop)
    {
        pa_threaded_mainloop_free(ptr->mainloop);
    }

    PaUtil_FreeMemory(ptr);
}


static void PulseAudioCheckContextStateCb(pa_context * c, void *userdata)
{
    PaPulseAudioHostApiRepresentation *ptr = userdata;
    assert(c);

    /* If this is null we have big problems and we probably are out of memory */
    if(!c)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudioCheckContextStateCb: Out of memory");
        pa_threaded_mainloop_signal(ptr->mainloop, 0);
        return;
    }

    ptr->state = pa_context_get_state(c);
    pa_threaded_mainloop_signal(ptr->mainloop, 0);
}


void PulseAudioSinkListCb(pa_context *c, const pa_sink_info *l, int eol, void *userdata)
{
    PaPulseAudioHostApiRepresentation *l_ptrHostApi = (PaPulseAudioHostApiRepresentation *)userdata;
    PaError result = paNoError;
    char *l_strName = NULL;

    assert(c);

    /* If this is null we have big problems and we probably are out of memory */
    if(!c)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudioSinkListCb: Out of memory");
        goto error;
    }

    /* If eol is set to a positive number, you're at the end of the list */
    if (eol > 0)
    {
        goto error;
    }

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].structVersion = 2;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].hostApi = l_ptrHostApi->hostApiIndex;

    l_strName = (char *)l->name;

    if(l->description != NULL)
    {
        l_strName = (char *)l->description;
    }

    PA_UNLESS(l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount] = (char*)PaUtil_GroupAllocateMemory(l_ptrHostApi->allocations,
              strnlen(l->name, 1024) + 1), paInsufficientMemory);

    memset(l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount], 0x00, strnlen(l->name, 1024) + 1);
    strncpy((char *)l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount], l->name, strnlen(l->name, 1024));

    PA_UNLESS(l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name = (char*)PaUtil_GroupAllocateMemory(l_ptrHostApi->allocations,
              strnlen(l_strName, 1024) + 1), paInsufficientMemory);

    memset((char *)l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name, 0x00, strnlen(l_strName, 1024) + 1);
    strncpy((char *)l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name, l_strName, strnlen(l_strName, 1024));

    // l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name = l->name;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].maxInputChannels = 0;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].maxOutputChannels = l->sample_spec.channels;

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultLowInputLatency = 0.;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultLowOutputLatency = l->latency;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultHighInputLatency = 0.;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultHighOutputLatency = l->configured_latency;

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultSampleRate = l->sample_spec.rate;

    l_ptrHostApi->deviceCount ++;

error:
    pa_threaded_mainloop_signal(l_ptrHostApi->mainloop, 0);
}


void PulseAudioSourceListCb(pa_context *c, const pa_source_info *l, int eol, void *userdata)
{
    PaPulseAudioHostApiRepresentation *l_ptrHostApi = (PaPulseAudioHostApiRepresentation *)userdata;
    PaError result = paNoError;
    char *l_strName = NULL;

    assert(c);

    /* If this is null we have big problems and we probably are out of memory */
    if(!c)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudioSourceListCb: Out of memory");
        goto error;
    }

    /* If eol is set to a positive number, you're at the end of the list */
    if (eol > 0)
    {
        goto error;
    }

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].structVersion = 2;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].hostApi = l_ptrHostApi->hostApiIndex;

    l_strName = (char *)l->name;

    if(l->description != NULL)
    {
        l_strName = (char *)l->description;
    }

    printf("%s Source name: %s (%s)\n", __FUNCTION__, l->name, l->description);

    PA_UNLESS(l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount] = (char*)PaUtil_GroupAllocateMemory(l_ptrHostApi->allocations,
              strnlen(l->name, 1024) + 1), paInsufficientMemory);

    memset(l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount], 0x00, strnlen(l->name, 1024) + 1);
    strncpy((char *)l_ptrHostApi->pulseaudioDeviceNames[l_ptrHostApi->deviceCount], l->name, strnlen(l->name, 1024));

    PA_UNLESS(l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name = (char*)PaUtil_GroupAllocateMemory(l_ptrHostApi->allocations,
              strnlen(l_strName, 1024) + 1), paInsufficientMemory);

    memset((char *)l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name, 0x00, strnlen(l_strName, 1024) + 1);
    strncpy((char *)l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].name, l_strName, strnlen(l_strName, 1024));

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].maxInputChannels = l->sample_spec.channels;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].maxOutputChannels = 0;

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultLowInputLatency = (double)l->latency;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultLowOutputLatency = 0;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultHighInputLatency = (double)l->configured_latency;
    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultHighOutputLatency = 0;

    l_ptrHostApi->deviceInfoArray[l_ptrHostApi->deviceCount].defaultSampleRate = l->sample_spec.rate;

    l_ptrHostApi->deviceCount ++;

error:
    PA_DEBUG(("Portaudio %s: End of list\n",__FUNCTION__));

    pa_threaded_mainloop_signal(l_ptrHostApi->mainloop, 0);
}

/* This routine is called whenever the stream state changes */
void PulseAudioStreamStateCb(pa_stream *s, void *userdata)
{
    const pa_buffer_attr *a;
    /* If you need debug pring enable these
    char cmt[PA_CHANNEL_MAP_SNPRINT_MAX], sst[PA_SAMPLE_SPEC_SNPRINT_MAX];
    */

    assert(s);

    /* If this is null we have big problems and we probably are out of memory */
    if(!s)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudioStreamStateCb: Out of memory");
        return;
    }

    /* printf("Portaudio [PulseAudio (PulseAudioStreamStateCb)]: Stream STATE CHANGED: "); */

    switch (pa_stream_get_state(s))
    {
    case PA_STREAM_TERMINATED:
        break;

    case PA_STREAM_CREATING:
        break;

    case PA_STREAM_READY:
        /* Mainly here is you need debug printing

          fprintf(stderr, "PulseAudioStreamStateCb: Stream successfully created.\n");

        if (!(a = pa_stream_get_buffer_attr(s)))
            fprintf(stderr, "pa_stream_get_buffer_attr() failed: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
        else {
                fprintf(stderr, "PulseAudioStreamStateCb: Buffer metrics: maxlength=%u, tlength=%u, prebuf=%u, minreq=%u\n", a->maxlength, a->tlength, a->prebuf, a->minreq);
        }

        fprintf(stderr, "PulseAudioStreamStateCb: Using sample spec '%s', channel map '%s'.\n",
                pa_sample_spec_snprint(sst, sizeof(sst), pa_stream_get_sample_spec(s)),
                pa_channel_map_snprint(cmt, sizeof(cmt), pa_stream_get_channel_map(s)));

        fprintf(stderr, "PulseAudioStreamStateCb: Connected to device %s (%u, %ssuspended).\n",
                pa_stream_get_device_name(s),
                pa_stream_get_device_index(s),
                pa_stream_is_suspended(s) ? "" : "not ");*/
        break;

    case PA_STREAM_FAILED:
    default:
        PA_DEBUG(("Portaudio %s: FAILED '%s'\n",__FUNCTION__, pa_strerror(pa_context_errno(pa_stream_get_context(s)))));

        break;
    }
}

void PulseAudioStreamUnderflowCb(pa_stream *s, void *userdata)
{
    PaPulseAudioStream *stream = (PaPulseAudioStream*)userdata;

    assert(s);

    /* If this is null we have big problems and we probably are out of memory */
    if(!s)
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PulseAudioStreamUnderflowCb: Out of memory");
        return;
    }

    stream->underflows++;

    if (stream->underflows >= 6 && stream->latency < 2000000)
    {
        stream->latency = (stream->latency * 3) / 2;
        stream->bufferAttr.maxlength = pa_usec_to_bytes(stream->latency, &stream->outSampleSpec);
        stream->bufferAttr.tlength = pa_usec_to_bytes(stream->latency, &stream->outSampleSpec);
        pa_stream_set_buffer_attr(s, &stream->bufferAttr, NULL, NULL);
        stream->underflows = 0;
        PA_DEBUG(("Portaudio %s: latency increased to %d\n", __FUNCTION__, stream->latency));
    }

    pa_threaded_mainloop_signal(stream->mainloop, 0);
}


PaError PaPulseAudio_Initialize(PaUtilHostApiRepresentation **hostApi, PaHostApiIndex hostApiIndex)
{
    PaError result = paNoError;
    int i;
    int deviceCount;
    PaPulseAudioHostApiRepresentation *l_ptrPulseAudioHostApi = NULL;
    PaDeviceInfo *deviceInfoArray = NULL;

    pa_operation *l_ptrOperation = NULL;

    l_ptrPulseAudioHostApi = PulseAudioNew();

    if(!l_ptrPulseAudioHostApi)
    {
        result = paInsufficientMemory;
        goto error;
    }

    l_ptrPulseAudioHostApi->allocations = PaUtil_CreateAllocationGroup();

    if(!l_ptrPulseAudioHostApi->allocations)
    {
        PulseAudioFree(l_ptrPulseAudioHostApi);
        result = paInsufficientMemory;
        goto error;
    }

    l_ptrPulseAudioHostApi->hostApiIndex = hostApiIndex;
    *hostApi = &l_ptrPulseAudioHostApi->inheritedHostApiRep;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paPulseAudio;
    (*hostApi)->info.name = "PulseAudio";

    (*hostApi)->info.defaultInputDevice = paNoDevice;
    (*hostApi)->info.defaultOutputDevice = paNoDevice;

    /* Connect to server */
    pa_context_connect(l_ptrPulseAudioHostApi->context, NULL, 0, NULL);

    while(1)
    {
        pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);

        switch (l_ptrPulseAudioHostApi->state)
        {
        case PA_CONTEXT_READY:
            break;

        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            goto error;
            break;
        case PA_CONTEXT_UNCONNECTED:
            break;


        case PA_CONTEXT_CONNECTING:
            break;

        case PA_CONTEXT_AUTHORIZING:
            break;

        case PA_CONTEXT_SETTING_NAME:
            break;
        }

        if(l_ptrPulseAudioHostApi->state == PA_CONTEXT_READY)
        {
            pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);
            break;
        }

        pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);
        usleep(100);
    }

    pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);

    for(i = 0; i < 1024; i ++)
    {
        memset(l_ptrPulseAudioHostApi->deviceInfoArray, 0x00, sizeof(PaDeviceInfo) * 1024);
        l_ptrPulseAudioHostApi->pulseaudioDeviceNames[i] = NULL;
    }

    l_ptrOperation = pa_context_get_sink_info_list(l_ptrPulseAudioHostApi->context,
                     PulseAudioSinkListCb,
                     l_ptrPulseAudioHostApi);

    pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);

    while (pa_operation_get_state(l_ptrOperation) == PA_OPERATION_RUNNING)
    {
        pa_threaded_mainloop_wait(l_ptrPulseAudioHostApi->mainloop);
    }

    pa_operation_unref(l_ptrOperation);

    pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);
    l_ptrOperation = pa_context_get_source_info_list(l_ptrPulseAudioHostApi->context,
                     PulseAudioSourceListCb,
                     l_ptrPulseAudioHostApi);

    pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);

    while (pa_operation_get_state(l_ptrOperation) == PA_OPERATION_RUNNING)
    {
        pa_threaded_mainloop_wait(l_ptrPulseAudioHostApi->mainloop);
    }

    pa_operation_unref(l_ptrOperation);

    (*hostApi)->info.deviceCount = l_ptrPulseAudioHostApi->deviceCount;

    if(l_ptrPulseAudioHostApi->deviceCount > 0)
    {
        /* If you have over 1024 Audio devices.. shame on you! */

        (*hostApi)->deviceInfos = (PaDeviceInfo**)PaUtil_GroupAllocateMemory(
                                      l_ptrPulseAudioHostApi->allocations, sizeof(PaDeviceInfo*) * l_ptrPulseAudioHostApi->deviceCount);

        if(!(*hostApi)->deviceInfos)
        {
            result = paInsufficientMemory;
            goto error;
        }

        for(i = 0; i < l_ptrPulseAudioHostApi->deviceCount; i ++)
        {
            (*hostApi)->deviceInfos[i] = &l_ptrPulseAudioHostApi->deviceInfoArray[i];
        }

        /* First should be the default */
        (*hostApi)->info.defaultInputDevice = -1;
        (*hostApi)->info.defaultOutputDevice = -1;

        for(i = 0; i < l_ptrPulseAudioHostApi->deviceCount; i ++)
        {
            if(l_ptrPulseAudioHostApi->deviceInfoArray[i].maxInputChannels > 0 && (*hostApi)->info.defaultInputDevice == -1)
            {
                (*hostApi)->info.defaultInputDevice = i;
            }

            if(l_ptrPulseAudioHostApi->deviceInfoArray[i].maxOutputChannels > 0 && (*hostApi)->info.defaultOutputDevice == -1)
            {
                (*hostApi)->info.defaultOutputDevice = i;
            }
        }


    }

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    PaUtil_InitializeStreamInterface(&l_ptrPulseAudioHostApi->callbackStreamInterface,
                                     PulseAudioCloseStreamCb,
                                     PulseAudioStartStreamCb,
                                     PulseAudioStopStreamCb,
                                     PulseAudioAbortStreamCb,
                                     IsStreamStopped,
                                     IsStreamActive,
                                     GetStreamTime,
                                     GetStreamCpuLoad,
                                     PaUtil_DummyRead,
                                     PaUtil_DummyWrite,
                                     PaUtil_DummyGetReadAvailable,
                                     PaUtil_DummyGetWriteAvailable);

    PaUtil_InitializeStreamInterface(&l_ptrPulseAudioHostApi->blockingStreamInterface,
                                     PulseAudioCloseStreamCb,
                                     PulseAudioStartStreamCb,
                                     PulseAudioStopStreamCb,
                                     PulseAudioAbortStreamCb,
                                     IsStreamStopped,
                                     IsStreamActive,
                                     GetStreamTime,
                                     PaUtil_DummyGetCpuLoad,
                                     PulseAudioReadStreamBlock,
                                     PulseAudioWriteStreamBlock,
                                     PulseAudioGetStreamReadAvailableBlock,
                                     PulseAudioGetStreamWriteAvailableBlock);

    return result;

error:

    if(l_ptrPulseAudioHostApi)
    {
        if(l_ptrPulseAudioHostApi->allocations)
        {
            PaUtil_FreeAllAllocations(l_ptrPulseAudioHostApi->allocations);
            PaUtil_DestroyAllocationGroup(l_ptrPulseAudioHostApi->allocations);
        }

        PulseAudioFree(l_ptrPulseAudioHostApi);
    }

    return result;
}


static void Terminate(struct PaUtilHostApiRepresentation *hostApi)
{
    PaPulseAudioHostApiRepresentation *l_ptrPulseAudioHostApi = (PaPulseAudioHostApiRepresentation*)hostApi;

    if(l_ptrPulseAudioHostApi->allocations)
    {
        PaUtil_FreeAllAllocations(l_ptrPulseAudioHostApi->allocations);
        PaUtil_DestroyAllocationGroup(l_ptrPulseAudioHostApi->allocations);
    }

    pa_context_disconnect(l_ptrPulseAudioHostApi->context);

    PulseAudioFree(l_ptrPulseAudioHostApi);
}


static PaError IsFormatSupported(struct PaUtilHostApiRepresentation *hostApi,
                                 const PaStreamParameters *inputParameters,
                                 const PaStreamParameters *outputParameters,
                                 double sampleRate)
{
    int inputChannelCount, outputChannelCount;
    PaSampleFormat inputSampleFormat, outputSampleFormat;

    if(inputParameters)
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
            this implementation doesn't support any custom sample formats */
        if(inputSampleFormat & paCustomFormat)
        {
            return paSampleFormatNotSupported;
        }

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if(inputParameters->device == paUseHostApiSpecificDeviceSpecification)
        {
            return paInvalidDevice;
        }

        /* check that input device can support inputChannelCount */
        if(inputChannelCount > hostApi->deviceInfos[ inputParameters->device ]->maxInputChannels)
        {
            return paInvalidChannelCount;
        }

        /* validate inputStreamInfo */
        if(inputParameters->hostApiSpecificStreamInfo)
        {
            return paIncompatibleHostApiSpecificStreamInfo;    /* this implementation doesn't use custom stream info */
        }

    }

    else
    {
        inputChannelCount = 0;
    }

    if(outputParameters)
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
            this implementation doesn't support any custom sample formats */
        if(outputSampleFormat & paCustomFormat)
        {
            return paSampleFormatNotSupported;
        }

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if(outputParameters->device == paUseHostApiSpecificDeviceSpecification)
        {
            return paInvalidDevice;
        }

        /* check that output device can support outputChannelCount */
        if(outputChannelCount > hostApi->deviceInfos[ outputParameters->device ]->maxOutputChannels)
        {
            return paInvalidChannelCount;
        }

        /* validate outputStreamInfo */
        if(outputParameters->hostApiSpecificStreamInfo)
        {
            return paIncompatibleHostApiSpecificStreamInfo;    /* this implementation doesn't use custom stream info */
        }

    }

    else
    {
        outputChannelCount = 0;
    }

    /*
        IMPLEMENT ME:

            - if a full duplex stream is requested, check that the combination
                of input and output parameters is supported if necessary

            - check that the device supports sampleRate

        Because the buffer adapter handles conversion between all standard
        sample formats, the following checks are only required if paCustomFormat
        is implemented, or under some other unusual conditions.

            - check that input device can support inputSampleFormat, or that
                we have the capability to convert from inputSampleFormat to
                a native format

            - check that output device can support outputSampleFormat, or that
                we have the capability to convert from outputSampleFormat to
                a native format
    */


    /* suppress unused variable warnings */
    (void) sampleRate;

    return paFormatIsSupported;
}

PaError PulseAudioConvertPortaudioFormatToPulseAudio(PaSampleFormat portaudiosf,
        pa_sample_spec *pulseaudiosf)
{
    switch(portaudiosf)
    {
    case paFloat32:
        pulseaudiosf->format = PA_SAMPLE_FLOAT32LE;
        break;

    case paInt32:
        pulseaudiosf->format = PA_SAMPLE_S32LE;
        break;

    case paInt24:
        pulseaudiosf->format = PA_SAMPLE_S24LE;
        break;

    case paInt16:
        pulseaudiosf->format = PA_SAMPLE_S16LE;
        break;

    case paInt8:
        pulseaudiosf->format = PA_SAMPLE_U8;
        break;

    case paUInt8:
        pulseaudiosf->format = PA_SAMPLE_U8;
        break;

    case paCustomFormat:
    case paNonInterleaved:
        PA_DEBUG(("PulseAudio %s: THIS IS NOT SUPPORTED BY PULSEAUDIO!\n", __FUNCTION__));
        return paNotInitialized;
        break;
    }

    return paNoError;
}


/* Allocate buffer. */
static PaError PulseAudioBlockingInitRingBuffer(PaPulseAudioStream *stream, PaUtilRingBuffer *rbuf, int size)
{
    long l_lNumBytes = 4096 * size;
    char *l_ptrBuffer = (char *) malloc( l_lNumBytes );

    if( l_ptrBuffer == NULL )
    {
        return paInsufficientMemory;
    }

    memset( l_ptrBuffer, 0x00, l_lNumBytes );
    return (PaError) PaUtil_InitializeRingBuffer( rbuf, 1, l_lNumBytes, l_ptrBuffer );
}

/* see pa_hostapi.h for a list of validity guarantees made about OpenStream parameters */

static PaError OpenStream(struct PaUtilHostApiRepresentation *hostApi,
                          PaStream** s,
                          const PaStreamParameters *inputParameters,
                          const PaStreamParameters *outputParameters,
                          double sampleRate,
                          unsigned long framesPerBuffer,
                          PaStreamFlags streamFlags,
                          PaStreamCallback *streamCallback,
                          void *userData)
{
    PaError result = paNoError;
    PaPulseAudioHostApiRepresentation *l_ptrPulseAudioHostApi = (PaPulseAudioHostApiRepresentation*)hostApi;
    PaPulseAudioStream *stream = NULL;
    unsigned long framesPerHostBuffer = framesPerBuffer; /* these may not be equivalent for all implementations */
    int inputChannelCount, outputChannelCount;
    PaSampleFormat inputSampleFormat, outputSampleFormat;
    PaSampleFormat hostInputSampleFormat, hostOutputSampleFormat;

    /* validate platform specific flags */
    if((streamFlags & paPlatformSpecificFlags) != 0)
    {
        return paInvalidFlag;    /* unexpected platform specific flag */
    }


    pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);
    stream = (PaPulseAudioStream*)PaUtil_AllocateMemory(sizeof(PaPulseAudioStream));

    if(!stream)
    {
        result = paInsufficientMemory;
        goto error;
    }

    stream->isActive = 0;
    stream->isStopped = 1;

    stream->inStream = NULL;
    stream->outStream = NULL;
    stream->outBuffer = NULL;
    stream->inBuffer = NULL;
    memset(&stream->outputRing, 0x00, sizeof(PaUtilRingBuffer));
    memset(&stream->inputRing, 0x00, sizeof(PaUtilRingBuffer));


    if(inputParameters)
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if(inputParameters->device == paUseHostApiSpecificDeviceSpecification)
        {
            result = paInvalidDevice;
            goto error;
        }

        /* check that input device can support inputChannelCount */
        if(inputChannelCount > hostApi->deviceInfos[ inputParameters->device ]->maxInputChannels)
        {
            result = paInvalidChannelCount;
            goto error;
        }

        /* validate inputStreamInfo */
        if(inputParameters->hostApiSpecificStreamInfo)
        {
            result = paIncompatibleHostApiSpecificStreamInfo;    /* this implementation doesn't use custom stream info */
            goto error;
        }

        hostInputSampleFormat =
            PaUtil_SelectClosestAvailableFormat(inputSampleFormat, inputSampleFormat);

        stream->inputFrameSize = Pa_GetSampleSize(inputSampleFormat) * inputChannelCount;

        PulseAudioConvertPortaudioFormatToPulseAudio(inputSampleFormat, &stream->inSampleSpec);
        stream->inSampleSpec.rate = sampleRate;
        stream->inSampleSpec.channels = inputChannelCount;

        if(!pa_sample_spec_valid(&stream->inSampleSpec   ))
        {
            PA_DEBUG(("Portaudio %s: Invalid input audio spec!\n",__FUNCTION__));
            goto error;
        }

        stream->inStream = pa_stream_new(l_ptrPulseAudioHostApi->context, "Portaudio source", &stream->inSampleSpec, NULL);

        if(stream->inStream != NULL)
        {
            pa_stream_set_state_callback(stream->inStream, PulseAudioStreamStateCb, NULL);
            pa_stream_set_started_callback(stream->inStream, PulseAudioStreamStartedCb, NULL);
            pa_stream_set_read_callback(stream->inStream, PulseAudioStreamReadCb, stream);

        }

        else
        {
            PA_DEBUG(("Portaudio %s: Can't alloc input stream!\n", __FUNCTION__));
        }

        stream->device = inputParameters->device;

        if(!streamCallback)
        {
            PulseAudioBlockingInitRingBuffer(stream, &stream->inputRing, stream->inputFrameSize);
        }

    }

    else
    {
        inputChannelCount = 0;
        inputSampleFormat = hostInputSampleFormat = paFloat32;
    }

    if(outputParameters)
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if(outputParameters->device == paUseHostApiSpecificDeviceSpecification)
        {
            result = paInvalidDevice;
            goto error;
        }

        /* check that output device can support inputChannelCount */
        if(outputChannelCount > hostApi->deviceInfos[ outputParameters->device ]->maxOutputChannels)
        {
            result = paInvalidChannelCount;
            goto error;
        }

        /* validate outputStreamInfo */
        if(outputParameters->hostApiSpecificStreamInfo)
        {
            result = paIncompatibleHostApiSpecificStreamInfo;    /* this implementation doesn't use custom stream info */
            goto error;
        }

        /* IMPLEMENT ME - establish which  host formats are available */
        hostOutputSampleFormat =
            PaUtil_SelectClosestAvailableFormat(outputSampleFormat /* native formats */, outputSampleFormat);

        stream->outputFrameSize = Pa_GetSampleSize(outputSampleFormat) * outputChannelCount;

        PulseAudioConvertPortaudioFormatToPulseAudio(outputSampleFormat, &stream->outSampleSpec);
        stream->outSampleSpec.rate = sampleRate;
        stream->outSampleSpec.channels = outputChannelCount;

        if(!pa_sample_spec_valid(&stream->outSampleSpec  ))
        {
            PA_DEBUG(("Portaudio %s: Invalid audio spec for output!\n", __FUNCTION__));
            goto error;
        }

        stream->outStream = pa_stream_new(l_ptrPulseAudioHostApi->context, "Portaudio sink", &stream->outSampleSpec, NULL);

        if(stream->outStream != NULL)
        {
            pa_stream_set_state_callback(stream->outStream, PulseAudioStreamStateCb, NULL);
            pa_stream_set_started_callback(stream->outStream, PulseAudioStreamStartedCb, NULL);
            pa_stream_set_write_callback(stream->outStream, PulseAudioStreamWriteCb, stream);
        }

        else
        {
            PA_DEBUG(("Portaudio %s: Can't alloc output stream!\n", __FUNCTION__));
        }

        if(!streamCallback)
        {
            long l_lnumBytes = 0;
            if( (l_lnumBytes = PulseAudioBlockingInitRingBuffer(stream, &stream->outputRing, stream->outputFrameSize)) != 0)
            {
                PA_DEBUG(("Portaudio %s: Can't alloc output RingBuffer (Error: %ld)!\n", __FUNCTION__, l_lnumBytes));
                goto error;
            }

        }

        stream->device = outputParameters->device;
    }

    else
    {
        outputChannelCount = 0;
        outputSampleFormat = hostOutputSampleFormat = paFloat32;
    }


    stream->hostapi = l_ptrPulseAudioHostApi;
    stream->context = l_ptrPulseAudioHostApi->context;
    stream->mainloop = l_ptrPulseAudioHostApi->mainloop;

    if(streamCallback)
    {
        PaUtil_InitializeStreamRepresentation(&stream->streamRepresentation,
                                              &l_ptrPulseAudioHostApi->callbackStreamInterface, streamCallback, userData);
    }
    else
    {
        PaUtil_InitializeStreamRepresentation(&stream->streamRepresentation,
                                              &l_ptrPulseAudioHostApi->blockingStreamInterface, streamCallback, userData);
    }

    PaUtil_InitializeCpuLoadMeasurer(&stream->cpuLoadMeasurer, sampleRate);


    /* we assume a fixed host buffer size in this example, but the buffer processor
        can also support bounded and unknown host buffer sizes by passing
        paUtilBoundedHostBufferSize or paUtilUnknownHostBufferSize instead of
        paUtilFixedHostBufferSize below. */

    result =  PaUtil_InitializeBufferProcessor(&stream->bufferProcessor,
              inputChannelCount, inputSampleFormat, hostInputSampleFormat,
              outputChannelCount, outputSampleFormat, hostOutputSampleFormat,
              sampleRate, streamFlags, framesPerBuffer,
              framesPerHostBuffer, paUtilUnknownHostBufferSize,
              streamCallback, userData);

    if(result != paNoError)
    {
        goto error;
    }

    stream->streamRepresentation.streamInfo.inputLatency =
        (PaTime)PaUtil_GetBufferProcessorInputLatencyFrames(&stream->bufferProcessor) / sampleRate; /* inputLatency is specified in _seconds_ */
    stream->streamRepresentation.streamInfo.outputLatency =
        (PaTime)PaUtil_GetBufferProcessorOutputLatencyFrames(&stream->bufferProcessor) / sampleRate; /* outputLatency is specified in _seconds_ */
    stream->streamRepresentation.streamInfo.sampleRate = sampleRate;

    stream->maxFramesHostPerBuffer = framesPerBuffer;
    stream->maxFramesPerBuffer = framesPerBuffer;

    *s = (PaStream*)stream;

    pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);
    return result;

error:
    pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);

    if(stream)
    {
        PaUtil_FreeMemory(stream);
        PulseAudioFree(l_ptrPulseAudioHostApi);
    }

    return paNotInitialized;
}

static PaError IsStreamStopped(PaStream *s)
{
    PaPulseAudioStream *stream = (PaPulseAudioStream*)s;
    return stream->isStopped;
}


static PaError IsStreamActive(PaStream *s)
{
    PaPulseAudioStream *stream = (PaPulseAudioStream*)s;
    return stream->isActive;
}


static PaTime GetStreamTime(PaStream *s)
{
    PaPulseAudioStream *stream = (PaPulseAudioStream*)s;
    PaPulseAudioHostApiRepresentation *l_ptrPulseAudioHostApi = stream->hostapi;
    pa_usec_t l_lUSec = 0;
    pa_operation *l_ptrOperation;

    pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);

    if(pa_stream_get_time(stream->outStream, &stream->outStreamTime) !=  -PA_ERR_NODATA)
    {
    }
    else
    {
        stream->outStreamTime = 0;
    }

    pa_threaded_mainloop_unlock(l_ptrPulseAudioHostApi->mainloop);
    return ((float)stream->outStreamTime / (float)1000000);
}


static double GetStreamCpuLoad(PaStream* s)
{
    PaPulseAudioStream *stream = (PaPulseAudioStream*)s;

    return PaUtil_GetCpuLoad(&stream->cpuLoadMeasurer);
}

