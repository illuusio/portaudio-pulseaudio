#ifndef _PA_HOSTAPI_PULSEAUDIO_BLOCK_H_
#define _PA_HOSTAPI_PULSEAUDIO_BLOCK_H_

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

PaError PulseaudioCloseStreamBlock(PaStream* stream);
PaError PulseaudioStartStreamBlock(PaStream *stream);
PaError PulseaudioStopStreamBlock(PaStream *stream);
PaError PulseaudioAbortStreamBlock(PaStream *stream);
PaError PulseaudioReadStreamBlock(PaStream* stream, void *buffer, unsigned long frames);
PaError PulseaudioWriteStreamBlock(PaStream* stream, const void *buffer, unsigned long frames);
signed long PulseaudioGetStreamReadAvailableBlock(PaStream* stream);
signed long PulseaudioGetStreamWriteAvailableBlock(PaStream* stream);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif

