#ifndef _PA_HOSTAPI_PULSEAUDIO_CB_H_
#define _PA_HOSTAPI_PULSEAUDIO_CB_H_

#include "pa_util.h"
#include "pa_allocation.h"
#include "pa_hostapi.h"
#include "pa_stream.h"
#include "pa_cpuload.h"
#include "pa_process.h"

#include "pa_unix_util.h"
#include "pa_ringbuffer.h"


/* Pulseaudio headers */
#include <stdio.h>
#include <string.h>
#include <pulse/pulseaudio.h>

#include "pa_hostapi_pulseaudio.h"


#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


PaError PulseaudioCloseStreamCb(PaStream* stream);
PaError PulseaudioStartStreamCb(PaStream *stream);
PaError PulseaudioStopStreamCb(PaStream *stream);
PaError PulseaudioAbortStreamCb(PaStream *stream);

void PulseaudioStreamReadCb(pa_stream *s, size_t length, void *userdata);
void PulseaudioStreamWriteCb(pa_stream *s, size_t length, void *userdata);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif

