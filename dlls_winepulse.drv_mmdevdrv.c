/*
 * Copyright 2010-2011 Maarten Lankhorst for CodeWeavers
 * Copyright 2011 Andrew Eikum for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Pulseaudio driver support.. hell froze over
 */

#define NONAMELESSUNION
#define COBJMACROS
#include "config.h"
#include <poll.h>
#include <pthread.h>

#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#include <pulse/pulseaudio.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/list.h"

#include "ole2.h"
#include "dshow.h"
#include "dsound.h"
#include "propsys.h"

#include "initguid.h"
#include "ks.h"
#include "ksmedia.h"
#include "mmdeviceapi.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"

#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(pulse);

static const REFERENCE_TIME MinimumPeriod = 100000;

static pa_context *pulse_ctx;
static pa_mainloop *pulse_ml;

static HANDLE pulse_thread;
static pthread_mutex_t pulse_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pulse_cond = PTHREAD_COND_INITIALIZER;

static struct list session_list = LIST_INIT( session_list );

typedef struct _AudioSession {
    GUID guid;

    EDataFlow dataflow;

    float master_vol;
    UINT32 channel_count;
    float *channel_vols;

    struct list entry;
} AudioSession;

typedef struct ACImpl {
    IAudioClient IAudioClient_iface;
    IAudioRenderClient IAudioRenderClient_iface;
    IAudioCaptureClient IAudioCaptureClient_iface;
    IAudioSessionControl2 IAudioSessionControl2_iface;
    ISimpleAudioVolume ISimpleAudioVolume_iface;
    IAudioClock IAudioClock_iface;
    IAudioClock2 IAudioClock2_iface;

    LONG ref;

    IMMDevice *parent;

    EDataFlow dataflow;
    DWORD flags;
    AUDCLNT_SHAREMODE share;
    HANDLE event;

    BOOL initted, started;
    UINT32 bufsize_frames;
    BYTE *locked_ptr, *tmp_buffer;
    UINT32 locked, peeked, extra_buffered;
    UINT64 play_ofs;

    pa_stream *stream;
    pa_sample_spec ss;
    pa_channel_map map;

    /* Mixer format + period times */
    pa_sample_spec mix_ss;
    pa_channel_map mix_map;
    REFERENCE_TIME min_period, def_period;
} ACImpl;

static const WCHAR defaultW[] = {'P','u','l','s','e','a','u','d','i','o',0};

static const IAudioClientVtbl AudioClient_Vtbl;
static const IAudioRenderClientVtbl AudioRenderClient_Vtbl;
static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl;
static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl;
static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl;
static const IAudioClockVtbl AudioClock_Vtbl;
static const IAudioClock2Vtbl AudioClock2_Vtbl;

static inline ACImpl *impl_from_IAudioClient(IAudioClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClient_iface);
}

static inline ACImpl *impl_from_IAudioRenderClient(IAudioRenderClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioRenderClient_iface);
}

static inline ACImpl *impl_from_IAudioCaptureClient(IAudioCaptureClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioCaptureClient_iface);
}

static inline ACImpl *impl_from_IAudioSessionControl2(IAudioSessionControl2 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioSessionControl2_iface);
}

static inline ACImpl *impl_from_ISimpleAudioVolume(ISimpleAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, ISimpleAudioVolume_iface);
}

static inline ACImpl *impl_from_IAudioClock(IAudioClock *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock_iface);
}

static inline ACImpl *impl_from_IAudioClock2(IAudioClock2 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock2_iface);
}

/* Following pulseaudio design here, mainloop has the lock taken whenever
 * it is handling something for pulse, and the lock is required whenever
 * doing any pa_* call that can affect the state in any way
 *
 * pa_cond_wait is used when waiting on results, because the mainloop needs
 * the same lock taken to affect the state
 *
 * This is basically the same as the pa_threaded_mainloop implementation,
 * but that cannot be used because it uses pthread_create directly
 *
 * pa_threaded_mainloop_(un)lock -> pthread_mutex_(un)lock
 * pa_threaded_mainloop_signal -> pthread_cond_signal
 * pa_threaded_mainloop_wait -> pthread_cond_wait
 */

static int pulse_poll_func(struct pollfd *ufds, unsigned long nfds, int timeout, void *userdata) {
    int r;
    pthread_mutex_unlock(&pulse_lock);
    r = poll(ufds, nfds, timeout);
    pthread_mutex_lock(&pulse_lock);
    return r;
}

static DWORD CALLBACK pulse_mainloop_thread(void *tmp) {
    int ret;
    pulse_ml = pa_mainloop_new();
    pa_mainloop_set_poll_func(pulse_ml, pulse_poll_func, NULL);
    pthread_mutex_lock(&pulse_lock);
    pthread_cond_signal(&pulse_cond);
    pa_mainloop_run(pulse_ml, &ret);
    pthread_mutex_unlock(&pulse_lock);
    pa_mainloop_free(pulse_ml);
    CloseHandle(pulse_thread);
    return ret;
}

static void pulse_contextcallback(pa_context *c, void *userdata);

static HRESULT pulse_connect(void)
{
    int len;
    WCHAR path[PATH_MAX], *name;
    char *str;

    if (!pulse_thread)
    {
        if (!(pulse_thread = CreateThread(NULL, 0, pulse_mainloop_thread, NULL, 0, NULL)))
        {
            ERR("Failed to create mainloop thread.");
            return E_FAIL;
        }
        pthread_cond_wait(&pulse_cond, &pulse_lock);
    }

    if (pulse_ctx && PA_CONTEXT_IS_GOOD(pa_context_get_state(pulse_ctx)))
        return S_OK;
    if (pulse_ctx)
        pa_context_unref(pulse_ctx);

    GetModuleFileNameW(NULL, path, sizeof(path)/sizeof(*path));
    name = strrchrW(path, '\\');
    if (!name)
        name = path;
    else
        name++;
    len = WideCharToMultiByte(CP_UNIXCP, 0, name, -1, NULL, 0, NULL, NULL);
    str = pa_xmalloc(len);
    WideCharToMultiByte(CP_UNIXCP, 0, name, -1, str, len, NULL, NULL);
    TRACE("Name: %s\n", str);
    pulse_ctx = pa_context_new(pa_mainloop_get_api(pulse_ml), str);
    pa_xfree(str);
    if (!pulse_ctx) {
        ERR("Failed to create context\n");
        return E_FAIL;
    }

    pa_context_set_state_callback(pulse_ctx, pulse_contextcallback, NULL);

    TRACE("libpulse protocol version: %u. API Version %u\n", pa_context_get_protocol_version(pulse_ctx), PA_API_VERSION);
    if (pa_context_connect(pulse_ctx, NULL, 0, NULL) < 0)
        goto fail;

    /* Wait for connection */
    while (pthread_cond_wait(&pulse_cond, &pulse_lock)) {
        pa_context_state_t state = pa_context_get_state(pulse_ctx);

        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            goto fail;

        if (state == PA_CONTEXT_READY)
            break;
    }

    TRACE("Connected to server %s with protocol version: %i.\n",
        pa_context_get_server(pulse_ctx),
        pa_context_get_server_protocol_version(pulse_ctx));
    return S_OK;

fail:
    pa_context_unref(pulse_ctx);
    pulse_ctx = NULL;
    return E_FAIL;
}

static void pulse_contextcallback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        default:
            FIXME("Unhandled state: %i\n", pa_context_get_state(c));
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            TRACE("State change to %i\n", pa_context_get_state(c));
            return;

        case PA_CONTEXT_READY:
            TRACE("Ready\n");
            break;

        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            ERR("Context failed: %s\n", pa_strerror(pa_context_errno(c)));
    }
    pthread_cond_signal(&pulse_cond);
}

static void pulse_stream_state(pa_stream *s, void *user);

static HRESULT pulse_stream_valid(ACImpl *This) {
    if (!This->initted)
        return AUDCLNT_E_NOT_INITIALIZED;
    if (!This->stream || pa_stream_get_state(This->stream) != PA_STREAM_READY)
        return AUDCLNT_E_DEVICE_INVALIDATED;
    return S_OK;
}

static void dump_attr(const pa_buffer_attr *attr) {
    TRACE("maxlength: %u\n", attr->maxlength);
    TRACE("minreq: %u\n", attr->minreq);
    TRACE("fragsize: %u\n", attr->fragsize);
    TRACE("tlength: %u\n", attr->tlength);
    TRACE("prebuf: %u\n", attr->prebuf);
}

static void pulse_op_cb(pa_stream *s, int success, void *user) {
    TRACE("Success: %i\n", success);
    *(int*)user = success;
    pthread_cond_signal(&pulse_cond);
}

static void pulse_attr_update(pa_stream *s, void *user) {
    const pa_buffer_attr *attr = pa_stream_get_buffer_attr(s);
    TRACE("New attributes or device moved:\n");
    dump_attr(attr);
}

static HRESULT pulse_stream_connect(ACImpl *This, REFERENCE_TIME period) {
    int ret;
    char buffer[64];
    static LONG number;
    pa_buffer_attr attr;
    if (This->stream) {
        pa_stream_disconnect(This->stream);
        while (pa_stream_get_state(This->stream) == PA_STREAM_READY)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        pa_stream_unref(This->stream);
    }
    ret = InterlockedIncrement(&number);
    sprintf(buffer, "audio stream #%i", ret);
    This->stream = pa_stream_new(pulse_ctx, buffer, &This->ss, &This->map);
    pa_stream_set_state_callback(This->stream, pulse_stream_state, This);
    pa_stream_set_buffer_attr_callback(This->stream, pulse_attr_update, This);
    pa_stream_set_moved_callback(This->stream, pulse_attr_update, This);

    attr.maxlength = -1;
    attr.tlength = This->bufsize_frames * pa_frame_size(&This->ss);
    if (This->def_period > period)
        period = This->def_period;
    attr.minreq = attr.fragsize = pa_usec_to_bytes(period/10, &This->ss);
    attr.prebuf = 0;
    dump_attr(&attr);
    if (This->dataflow == eRender)
        ret = pa_stream_connect_playback(This->stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_START_UNMUTED|PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_EARLY_REQUESTS, NULL, NULL);
    else
        ret = pa_stream_connect_record(This->stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_START_UNMUTED|PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_EARLY_REQUESTS);
    if (ret < 0) {
        WARN("Returns %i\n", ret);
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }
    while (pa_stream_get_state(This->stream) == PA_STREAM_CREATING)
        pthread_cond_wait(&pulse_cond, &pulse_lock);
    if (pa_stream_get_state(This->stream) != PA_STREAM_READY)
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    return S_OK;
}

static void pulse_stream_state(pa_stream *s, void *user)
{
    pa_stream_state_t state = pa_stream_get_state(s);
    TRACE("Stream state changed to %i\n", state);
    pthread_cond_signal(&pulse_cond);
}

HRESULT WINAPI AUDDRV_GetEndpointIDs(EDataFlow flow, WCHAR ***ids, void ***keys,
        UINT *num, UINT *def_index)
{
    HRESULT hr = S_OK;
    TRACE("%d %p %p %p\n", flow, ids, num, def_index);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_connect();
    pthread_mutex_unlock(&pulse_lock);
    if (FAILED(hr))
        return hr;
    *num = 1;
    *def_index = 0;

    *ids = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR *));
    if(!*ids)
        return E_OUTOFMEMORY;

    (*ids)[0] = HeapAlloc(GetProcessHeap(), 0, sizeof(defaultW));
    if(!(*ids)[0]){
        HeapFree(GetProcessHeap(), 0, *ids);
        return E_OUTOFMEMORY;
    }

    lstrcpyW((*ids)[0], defaultW);

    *keys = HeapAlloc(GetProcessHeap(), 0, sizeof(void *));
    (*keys)[0] = NULL;

    return S_OK;
}

HRESULT WINAPI AUDDRV_GetAudioEndpoint(void *key, IMMDevice *dev,
        EDataFlow dataflow, IAudioClient **out)
{
    HRESULT hr;
    ACImpl *This;

    TRACE("%p %p %d %p\n", key, dev, dataflow, out);

    *out = NULL;
    pthread_mutex_lock(&pulse_lock);
    hr = pulse_connect();
    pthread_mutex_unlock(&pulse_lock);
    if (FAILED(hr))
        return hr;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ACImpl));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioClient_iface.lpVtbl = &AudioClient_Vtbl;
    This->IAudioRenderClient_iface.lpVtbl = &AudioRenderClient_Vtbl;
    This->IAudioCaptureClient_iface.lpVtbl = &AudioCaptureClient_Vtbl;
    This->IAudioSessionControl2_iface.lpVtbl = &AudioSessionControl2_Vtbl;
    This->ISimpleAudioVolume_iface.lpVtbl = &SimpleAudioVolume_Vtbl;
    This->IAudioClock_iface.lpVtbl = &AudioClock_Vtbl;
    This->IAudioClock2_iface.lpVtbl = &AudioClock2_Vtbl;

    This->dataflow = dataflow;

    if(dataflow != eRender && dataflow != eCapture) {
        HeapFree(GetProcessHeap(), 0, This);
        return E_UNEXPECTED;
    }

    This->parent = dev;
    IMMDevice_AddRef(This->parent);

    *out = &This->IAudioClient_iface;
    IAudioClient_AddRef(&This->IAudioClient_iface);

    return S_OK;
}

static HRESULT WINAPI AudioClient_QueryInterface(IAudioClient *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }
    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClient_AddRef(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioClient_Release(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref){
        IAudioClient_Stop(iface);
        if (This->stream) {
            pthread_mutex_lock(&pulse_lock);
            if (pa_stream_get_state(This->stream) == PA_STREAM_READY)
                pa_stream_disconnect(This->stream);
            pa_stream_unref(This->stream);
            pthread_mutex_unlock(&pulse_lock);
        }
        IMMDevice_Release(This->parent);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void dump_fmt(const WAVEFORMATEX *fmt)
{
    TRACE("wFormatTag: 0x%x (", fmt->wFormatTag);
    switch(fmt->wFormatTag){
    case WAVE_FORMAT_PCM:
        TRACE("WAVE_FORMAT_PCM");
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        TRACE("WAVE_FORMAT_IEEE_FLOAT");
        break;
    case WAVE_FORMAT_EXTENSIBLE:
        TRACE("WAVE_FORMAT_EXTENSIBLE");
        break;
    default:
        TRACE("Unknown");
        break;
    }
    TRACE(")\n");

    TRACE("nChannels: %u\n", fmt->nChannels);
    TRACE("nSamplesPerSec: %u\n", fmt->nSamplesPerSec);
    TRACE("nAvgBytesPerSec: %u\n", fmt->nAvgBytesPerSec);
    TRACE("nBlockAlign: %u\n", fmt->nBlockAlign);
    TRACE("wBitsPerSample: %u\n", fmt->wBitsPerSample);
    TRACE("cbSize: %u\n", fmt->cbSize);

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
        WAVEFORMATEXTENSIBLE *fmtex = (void*)fmt;
        TRACE("dwChannelMask: %08x\n", fmtex->dwChannelMask);
        TRACE("Samples: %04x\n", fmtex->Samples.wReserved);
        TRACE("SubFormat: %s\n", wine_dbgstr_guid(&fmtex->SubFormat));
    }
}

static WAVEFORMATEX *clone_format(const WAVEFORMATEX *fmt)
{
    WAVEFORMATEX *ret;
    size_t size;

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        size = sizeof(WAVEFORMATEXTENSIBLE);
    else
        size = sizeof(WAVEFORMATEX);

    ret = HeapAlloc(GetProcessHeap(), 0, size);
    if(!ret)
        return NULL;

    memcpy(ret, fmt, size);

    ret->cbSize = size - sizeof(WAVEFORMATEX);

    return ret;
}

static DWORD get_channel_mask(unsigned int channels)
{
    switch(channels){
    case 0:
        return 0;
    case 1:
        return SPEAKER_FRONT_CENTER;
    case 2:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    case 3:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
            SPEAKER_LOW_FREQUENCY;
    case 4:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
            SPEAKER_BACK_RIGHT;
    case 5:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
            SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY;
    case 6:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
            SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_FRONT_CENTER;
    case 7:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
            SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_FRONT_CENTER |
            SPEAKER_BACK_CENTER;
    case 8:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
            SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_FRONT_CENTER |
            SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
    }
    FIXME("Unknown speaker configuration: %u\n", channels);
    return 0;
}

static HRESULT WINAPI AudioClient_Initialize(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
        REFERENCE_TIME period, const WAVEFORMATEX *fmt,
        const GUID *sessionguid)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%x, %x, %s, %s, %p, %s)\n", This, mode, flags,
          wine_dbgstr_longlong(duration), wine_dbgstr_longlong(period), fmt, debugstr_guid(sessionguid));

    if(!fmt)
        return E_POINTER;

    if(mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_NOT_INITIALIZED;

    if(flags & ~(AUDCLNT_STREAMFLAGS_CROSSPROCESS |
                AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST |
                AUDCLNT_STREAMFLAGS_RATEADJUST |
                AUDCLNT_SESSIONFLAGS_EXPIREWHENUNOWNED |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDE |
                AUDCLNT_SESSIONFLAGS_DISPLAY_HIDEWHENEXPIRED)){
        TRACE("Unknown flags: %08x\n", flags);
        return E_INVALIDARG;
    }

    pthread_mutex_lock(&pulse_lock);
    if(This->initted){
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }
    pa_channel_map_init(&This->map);
    This->ss.rate = fmt->nSamplesPerSec;
    This->ss.format = PA_SAMPLE_INVALID;
    switch(fmt->wFormatTag){
    case WAVE_FORMAT_PCM:
        if(fmt->wBitsPerSample == 8)
            This->ss.format = PA_SAMPLE_U8;
        else if(fmt->wBitsPerSample == 16)
            This->ss.format = PA_SAMPLE_S16LE;
        if (fmt->nChannels == 1 || fmt->nChannels == 2)
            pa_channel_map_init_auto(&This->map, fmt->nChannels, PA_CHANNEL_MAP_ALSA);
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        This->ss.format = PA_SAMPLE_FLOAT32LE;
        if (fmt->nChannels == 1 || fmt->nChannels == 2)
            pa_channel_map_init_auto(&This->map, fmt->nChannels, PA_CHANNEL_MAP_ALSA);
        break;
    case WAVE_FORMAT_EXTENSIBLE: {
        WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)fmt;
        DWORD mask = wfe->dwChannelMask;
        DWORD i = 0;
        if (fmt->cbSize != (sizeof(*wfe) - sizeof(*fmt)) && fmt->cbSize != sizeof(*wfe))
            break;
        if (IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            This->ss.format = PA_SAMPLE_FLOAT32LE;
        else if (IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            DWORD valid = wfe->Samples.wValidBitsPerSample;
            if (!valid)
                valid = fmt->wBitsPerSample;
            if (!valid || valid > fmt->wBitsPerSample)
                break;
            switch (fmt->wBitsPerSample) {
                case 8:
                    if (valid == 8)
                        This->ss.format = PA_SAMPLE_U8;
                    break;
                case 16:
                    if (valid == 16)
                        This->ss.format = PA_SAMPLE_S16LE;
                    break;
                case 24:
                    if (valid == 24)
                        This->ss.format = PA_SAMPLE_S24LE;
                    break;
                case 32:
                    if (valid == 24)
                        This->ss.format = PA_SAMPLE_S24_32LE;
                    else if (valid == 32)
                        This->ss.format = PA_SAMPLE_S32LE;
                default:
                    break;
            }
        }
        This->map.channels = fmt->nChannels;
        if (!mask)
            mask = get_channel_mask(fmt->nChannels);
        if (mask & SPEAKER_FRONT_LEFT) This->map.map[i++] = PA_CHANNEL_POSITION_FRONT_LEFT;
        if (mask & SPEAKER_FRONT_RIGHT) This->map.map[i++] = PA_CHANNEL_POSITION_FRONT_RIGHT;
        if (mask & SPEAKER_FRONT_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_FRONT_CENTER;
        if (mask & SPEAKER_LOW_FREQUENCY) This->map.map[i++] = PA_CHANNEL_POSITION_SUBWOOFER;
        if (mask & SPEAKER_BACK_LEFT) This->map.map[i++] = PA_CHANNEL_POSITION_REAR_LEFT;
        if (mask & SPEAKER_BACK_RIGHT) This->map.map[i++] = PA_CHANNEL_POSITION_REAR_RIGHT;
        if (mask & SPEAKER_BACK_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_REAR_CENTER;
        if (mask & SPEAKER_FRONT_LEFT_OF_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
        if (mask & SPEAKER_FRONT_RIGHT_OF_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
        if (mask & SPEAKER_BACK_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_REAR_CENTER;
        if (mask & SPEAKER_SIDE_LEFT) This->map.map[i++] = PA_CHANNEL_POSITION_SIDE_LEFT;
        if (mask & SPEAKER_SIDE_RIGHT) This->map.map[i++] = PA_CHANNEL_POSITION_SIDE_RIGHT;
        if (mask & SPEAKER_TOP_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_CENTER;
        if (mask & SPEAKER_TOP_FRONT_LEFT) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_FRONT_LEFT;
        if (mask & SPEAKER_TOP_FRONT_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_FRONT_CENTER;
        if (mask & SPEAKER_TOP_FRONT_RIGHT) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_FRONT_RIGHT;
        if (mask & SPEAKER_TOP_BACK_LEFT) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_REAR_LEFT;
        if (mask & SPEAKER_TOP_BACK_CENTER) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_REAR_CENTER;
        if (mask & SPEAKER_TOP_BACK_RIGHT) This->map.map[i++] = PA_CHANNEL_POSITION_TOP_REAR_RIGHT;
        if (mask & SPEAKER_ALL) {
            This->map.map[i++] = PA_CHANNEL_POSITION_MONO;
            FIXME("Is the 'all' channel mapped correctly?\n");
        }
        if (i != fmt->nChannels || mask & SPEAKER_RESERVED) {
            This->map.channels = 0;
            FIXME("Invalid channel mask: %i/%i and %x\n", i, fmt->nChannels, mask);
            break;
        }
        /* Special case for mono since pulse appears to map it differently */
        if (mask == SPEAKER_FRONT_CENTER)
            This->map.map[0] = PA_CHANNEL_POSITION_MONO;
        break;
        }
        default: FIXME("Unhandled tag %x\n", fmt->wFormatTag);
    }
    This->ss.channels = This->map.channels;
    hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
    if (!pa_channel_map_valid(&This->map) || This->ss.format == PA_SAMPLE_INVALID) {
        WARN("Invalid format! Channel spec valid: %i, format: %i\n", pa_channel_map_valid(&This->map), This->ss.format);
        dump_fmt(fmt);
        goto exit;
    }
    if (duration < 5000000)
        This->bufsize_frames = fmt->nSamplesPerSec/2;
    else if (duration < 20000000)
        This->bufsize_frames = ceil((duration / 10000000.) * fmt->nSamplesPerSec);
    else
        This->bufsize_frames = 2 * fmt->nSamplesPerSec;

    hr = pulse_stream_connect(This, period);
    if (SUCCEEDED(hr)) {
        /* Update frames according to new size */
        This->bufsize_frames = pa_stream_get_buffer_attr(This->stream)->tlength / pa_frame_size(&This->ss);
        //hr = AudioSession_CreateSession(This, sessionguid ? sessionguid : &GUID_NULL);
        if (SUCCEEDED(hr))
            This->initted = TRUE;
    }
    This->share = mode;
    This->flags = flags;

exit:
    if(FAILED(hr)) {
        if (This->stream) {
            pa_stream_disconnect(This->stream);
            pa_stream_unref(This->stream);
            This->stream = NULL;
        }
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_GetBufferSize(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (SUCCEEDED(hr))
        *out = This->bufsize_frames;
    pthread_mutex_unlock(&pulse_lock);

    return hr;
}

static HRESULT WINAPI AudioClient_GetStreamLatency(IAudioClient *iface,
        REFERENCE_TIME *latency)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    const pa_buffer_attr *attr;
    REFERENCE_TIME lat;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, latency);

    if(!latency)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }
    attr = pa_stream_get_buffer_attr(This->stream);
    if (This->dataflow == eCapture)
        lat = attr->fragsize / pa_frame_size(&This->ss);
    else
        lat = attr->minreq / pa_frame_size(&This->ss);
    *latency = 10000000;
    *latency *= lat;
    *latency /= This->ss.rate;
    pthread_mutex_unlock(&pulse_lock);
    TRACE("Latency: %u ms\n", (DWORD)(*latency / 10000));
    return S_OK;
}

static HRESULT WINAPI AudioClient_GetCurrentPadding(IAudioClient *iface,
        UINT32 *out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if(This->dataflow == eRender){
        UINT32 avail = pa_stream_writable_size(This->stream) / pa_frame_size(&This->ss);
        if (avail + This->extra_buffered >= This->bufsize_frames)
            *out = 0;
        else
            *out = This->bufsize_frames - avail - This->extra_buffered;
    }else if(This->dataflow == eCapture){
        if (!This->peeked) {
            DWORD frag, readable = pa_stream_readable_size(This->stream);
            pa_stream_peek(This->stream, (const void**)&This->locked_ptr, &frag);
            if (frag != readable) {
                DWORD done = frag;
                This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0, readable);
                memcpy(This->tmp_buffer, This->locked_ptr, frag);
                pa_stream_drop(This->stream);
                while (done < readable) {
                    pa_stream_peek(This->stream, (const void **)&This->locked_ptr, &frag);
                    memcpy(This->tmp_buffer + done, This->locked_ptr, frag);
                    pa_stream_drop(This->stream);
                    done += frag;
                }
                if (done > readable)
                    ERR("Read %u instead of %u\n", done, This->peeked);
                This->locked_ptr = NULL;
            }
            This->peeked = readable;
        }
        *out = This->peeked / pa_frame_size(&This->ss);
    }else{
        pthread_mutex_unlock(&pulse_lock);
        return E_UNEXPECTED;
    }
    pthread_mutex_unlock(&pulse_lock);

    TRACE("Pad: %u\n", *out);

    return S_OK;
}

static HRESULT WINAPI AudioClient_IsFormatSupported(IAudioClient *iface,
        AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *fmt,
        WAVEFORMATEX **out)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    WAVEFORMATEX *closest = NULL;
    WAVEFORMATEXTENSIBLE *wfe;

    TRACE("(%p)->(%x, %p, %p)\n", This, mode, fmt, out);

    if(!fmt || (mode == AUDCLNT_SHAREMODE_SHARED && !out))
        return E_POINTER;

    if(mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return E_INVALIDARG;

    if(fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return E_INVALIDARG;

    dump_fmt(fmt);

    closest = clone_format(fmt);
    if(!closest){
        hr = E_OUTOFMEMORY;
        goto exit;
    }
    wfe = (WAVEFORMATEXTENSIBLE*)closest;
    if (closest->wFormatTag == WAVE_FORMAT_EXTENSIBLE && !wfe->dwChannelMask) {
        wfe->dwChannelMask = get_channel_mask(closest->nChannels);
        hr = S_FALSE;
        WARN("Fixed up channel mask %p -> %p\n", fmt, closest);
    }

exit:
    if(hr == S_OK || !out){
        HeapFree(GetProcessHeap(), 0, closest);
        if(out)
            *out = NULL;
    }else if(closest){
        closest->nBlockAlign =
            closest->nChannels * closest->wBitsPerSample / 8;
        closest->nAvgBytesPerSec =
            closest->nBlockAlign * closest->nSamplesPerSec;
        *out = closest;
    }

    TRACE("returning: %08x %p\n", hr, out ? *out : NULL);
    return hr;
}

static void pulse_probe_settings(ACImpl *This) {
    pa_stream *stream;
    pa_channel_map map;
    pa_sample_spec ss;
    pa_buffer_attr attr;
    int ret;
    unsigned int length = 0;

    if (This->mix_ss.rate)
        return;

    pa_channel_map_init_auto(&map, 2, PA_CHANNEL_MAP_ALSA);
    ss.rate = 48000;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = map.channels;

    attr.maxlength = -1;
    attr.tlength = -1;
    attr.minreq = attr.fragsize = pa_frame_size(&ss);
    attr.prebuf = 0;

    stream = pa_stream_new(pulse_ctx, "format test stream", &ss, &map);
    if (stream)
        pa_stream_set_state_callback(stream, pulse_stream_state, NULL);
    if (!stream)
        ret = -1;
    else if (This->dataflow == eRender)
        ret = pa_stream_connect_playback(stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_FORMAT|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS, NULL, NULL);
    else
        ret = pa_stream_connect_record(stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_FORMAT|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS);
    if (ret >= 0) {
        while (pa_stream_get_state(stream) == PA_STREAM_CREATING)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        if (pa_stream_get_state(stream) == PA_STREAM_READY) {
            ss = *pa_stream_get_sample_spec(stream);
            map = *pa_stream_get_channel_map(stream);
            if (This->dataflow == eRender)
                length = pa_stream_get_buffer_attr(stream)->minreq;
            else
                length = pa_stream_get_buffer_attr(stream)->fragsize;
            pa_stream_disconnect(stream);
            while (pa_stream_get_state(stream) == PA_STREAM_READY)
                pthread_cond_wait(&pulse_cond, &pulse_lock);
        }
    }
    if (stream)
        pa_stream_unref(stream);
    This->mix_ss = ss;
    This->mix_map = map;
    if (length)
        This->def_period = This->min_period = pa_bytes_to_usec(10 * length, &This->mix_ss);
    else
        This->min_period = MinimumPeriod;
    if (This->def_period <= MinimumPeriod)
        This->def_period = MinimumPeriod;
}

static HRESULT WINAPI AudioClient_GetMixFormat(IAudioClient *iface,
        WAVEFORMATEX **pwfx)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    WAVEFORMATEXTENSIBLE *fmt;
    HRESULT hr = S_OK;
    int i;

    TRACE("(%p)->(%p)\n", This, pwfx);

    if(!pwfx)
        return E_POINTER;

    *pwfx = CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
    if(!*pwfx)
        return E_OUTOFMEMORY;

    fmt = (WAVEFORMATEXTENSIBLE*)*pwfx;

    pthread_mutex_lock(&pulse_lock);
    pulse_probe_settings(This);
    pthread_mutex_unlock(&pulse_lock);

    (*pwfx)->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    (*pwfx)->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    (*pwfx)->nChannels = This->mix_ss.channels;
    (*pwfx)->wBitsPerSample = 8 * pa_sample_size_of_format(This->mix_ss.format);
    (*pwfx)->nSamplesPerSec = This->mix_ss.rate;
    (*pwfx)->nBlockAlign = (*pwfx)->nChannels * (*pwfx)->wBitsPerSample / 8;
    (*pwfx)->nAvgBytesPerSec = (*pwfx)->nSamplesPerSec * (*pwfx)->nBlockAlign;
    if (This->mix_ss.format != PA_SAMPLE_S24_32LE)
        fmt->Samples.wValidBitsPerSample = (*pwfx)->wBitsPerSample;
    else
        fmt->Samples.wValidBitsPerSample = 24;
    if (This->mix_ss.format == PA_SAMPLE_FLOAT32LE)
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    fmt->dwChannelMask = 0;
    for (i = 0; i < This->mix_map.channels; ++i)
        switch (This->mix_map.map[i]) {
            default: FIXME("Unhandled channel %s\n", pa_channel_position_to_string(This->mix_map.map[i])); break;
            case PA_CHANNEL_POSITION_FRONT_LEFT: fmt->dwChannelMask |= SPEAKER_FRONT_LEFT; break;
            case PA_CHANNEL_POSITION_FRONT_RIGHT: fmt->dwChannelMask |= SPEAKER_FRONT_RIGHT; break;
            case PA_CHANNEL_POSITION_MONO:
            case PA_CHANNEL_POSITION_FRONT_CENTER: fmt->dwChannelMask |= SPEAKER_FRONT_CENTER; break;
            case PA_CHANNEL_POSITION_REAR_LEFT: fmt->dwChannelMask |= SPEAKER_BACK_LEFT; break;
            case PA_CHANNEL_POSITION_REAR_RIGHT: fmt->dwChannelMask |= SPEAKER_BACK_RIGHT; break;
            case PA_CHANNEL_POSITION_SUBWOOFER: fmt->dwChannelMask |= SPEAKER_LOW_FREQUENCY; break;
            case PA_CHANNEL_POSITION_SIDE_LEFT: fmt->dwChannelMask |= SPEAKER_SIDE_LEFT; break;
            case PA_CHANNEL_POSITION_SIDE_RIGHT: fmt->dwChannelMask |= SPEAKER_SIDE_RIGHT; break;
        }
    dump_fmt((WAVEFORMATEX*)fmt);
    if(FAILED(hr)) {
        CoTaskMemFree(*pwfx);
        *pwfx = NULL;
    }

    return hr;
}

static HRESULT WINAPI AudioClient_GetDevicePeriod(IAudioClient *iface,
        REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod)
{
    ACImpl *This = impl_from_IAudioClient(iface);

    TRACE("(%p)->(%p, %p)\n", This, defperiod, minperiod);

    if(!defperiod && !minperiod)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    pulse_probe_settings(This);
    if(defperiod)
        *defperiod = This->def_period;
    if(minperiod)
        *minperiod = This->min_period;
    pthread_mutex_unlock(&pulse_lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Start(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    int success;
    pa_operation *o;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if((This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !This->event){
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_SET;
    }

    if(This->started){
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    o = pa_stream_cork(This->stream, 0, pulse_op_cb, &success);
    if (o) {
        while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        pa_operation_unref(o);
    } else
        success = 0;
    if (!success)
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        This->started = TRUE;
    if (This->event)
        SetEvent(This->event);
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_Stop(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr = S_OK;
    pa_operation *o;
    int success;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if(!This->started){
        pthread_mutex_unlock(&pulse_lock);
        return S_FALSE;
    }

    o = pa_stream_cork(This->stream, 1, pulse_op_cb, &success);
    if (o) {
        while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        pa_operation_unref(o);
    } else
        success = 0;
    if (!success)
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        This->started = FALSE;
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static HRESULT WINAPI AudioClient_Reset(IAudioClient *iface)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    pa_usec_t time;
    pa_operation *o;
    int success;
    HRESULT hr = S_OK;

    TRACE("(%p)\n", This);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if(This->started){
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if (pa_stream_get_time(This->stream, &time) >= 0)
        This->play_ofs += time * This->ss.rate / 1000000;

    o = pa_stream_flush(This->stream, pulse_op_cb, &success);
    if (o) {
        while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        pa_operation_unref(o);
    } else
        success = 0;
    if (!success)
        hr = S_FALSE;
    pthread_mutex_unlock(&pulse_lock);

    return hr;
}

static HRESULT WINAPI AudioClient_SetEventHandle(IAudioClient *iface,
        HANDLE event)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, event);

    if(!event)
        return E_INVALIDARG;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if (FAILED(hr)) {
        pthread_mutex_unlock(&pulse_lock);
        return hr;
    }

    if(!(This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK)){
        pthread_mutex_unlock(&pulse_lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    }
    This->event = event;
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static HRESULT WINAPI AudioClient_GetService(IAudioClient *iface, REFIID riid,
        void **ppv)
{
    ACImpl *This = impl_from_IAudioClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    pthread_mutex_unlock(&pulse_lock);
    if (FAILED(hr))
        return hr;

    if(IsEqualIID(riid, &IID_IAudioRenderClient)){
        if(This->dataflow != eRender)
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        *ppv = &This->IAudioRenderClient_iface;
    }else if(IsEqualIID(riid, &IID_IAudioCaptureClient)){
        if(This->dataflow != eCapture)
            return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
        *ppv = &This->IAudioCaptureClient_iface;
    }else if(IsEqualIID(riid, &IID_IAudioSessionControl)){
        *ppv = &This->IAudioSessionControl2_iface;
    }else if(IsEqualIID(riid, &IID_ISimpleAudioVolume)){
        *ppv = &This->ISimpleAudioVolume_iface;
    }else if(IsEqualIID(riid, &IID_IAudioClock)){
        *ppv = &This->IAudioClock_iface;
    }

    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    FIXME("stub %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static const IAudioClientVtbl AudioClient_Vtbl =
{
    AudioClient_QueryInterface,
    AudioClient_AddRef,
    AudioClient_Release,
    AudioClient_Initialize,
    AudioClient_GetBufferSize,
    AudioClient_GetStreamLatency,
    AudioClient_GetCurrentPadding,
    AudioClient_IsFormatSupported,
    AudioClient_GetMixFormat,
    AudioClient_GetDevicePeriod,
    AudioClient_Start,
    AudioClient_Stop,
    AudioClient_Reset,
    AudioClient_SetEventHandle,
    AudioClient_GetService
};

static HRESULT WINAPI AudioRenderClient_QueryInterface(
        IAudioRenderClient *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioRenderClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioRenderClient_AddRef(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioRenderClient_Release(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioRenderClient_GetBuffer(IAudioRenderClient *iface,
        UINT32 frames, BYTE **data)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    UINT32 requested, avail;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%u, %p)\n", This, frames, data);

    if(!data)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if(FAILED(hr) || This->locked){
        pthread_mutex_unlock(&pulse_lock);
        return FAILED(hr) ? hr : AUDCLNT_E_OUT_OF_ORDER;
    }
    avail = pa_stream_writable_size(This->stream) / pa_frame_size(&This->ss);
    if (avail < frames){
        pthread_mutex_unlock(&pulse_lock);
        WARN("Wanted to write %u, but only %u available\n", frames, avail);
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }

    requested = frames * pa_frame_size(&This->ss);
    pa_stream_begin_write(This->stream, (void**)data, &requested);
    This->locked = frames;
    if (requested / pa_frame_size(&This->ss) < frames) {
        pa_stream_cancel_write(This->stream);
        FIXME("Unable to allocate all (%u/%u) preparing our own buffer\n", requested / pa_frame_size(&This->ss), frames);
        *data = This->locked_ptr = This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0, frames * pa_frame_size(&This->ss));
    } else {
        This->locked_ptr = *data;
    }
    pthread_mutex_unlock(&pulse_lock);
    return hr;
}

static void free_heap(void *p)
{
    HeapFree(GetProcessHeap(), 0, p);
}

static HRESULT WINAPI AudioRenderClient_ReleaseBuffer(
        IAudioRenderClient *iface, UINT32 written_frames, DWORD flags)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    int written;

    TRACE("(%p)->(%u, %x)\n", This, written_frames, flags);

    pthread_mutex_lock(&pulse_lock);
    if(!This->locked || !written_frames){
        if (This->tmp_buffer) {
            HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
            This->tmp_buffer = NULL;
        } else if (This->locked)
            pa_stream_cancel_write(This->stream);
        This->locked = 0;
        pthread_mutex_unlock(&pulse_lock);
        return written_frames ? AUDCLNT_E_OUT_OF_ORDER : S_OK;
    }

    if(flags & AUDCLNT_BUFFERFLAGS_SILENT){
        if(This->ss.format == PA_SAMPLE_U8)
            memset(This->locked_ptr, 128, written_frames * pa_frame_size(&This->ss));
        else
            memset(This->locked_ptr, 0, written_frames * pa_frame_size(&This->ss));
    }

    This->locked = 0;
    if (!This->tmp_buffer)
        written = pa_stream_write(This->stream, This->locked_ptr, written_frames * pa_frame_size(&This->ss), NULL, 0, PA_SEEK_RELATIVE);
    else
        written = pa_stream_write(This->stream, This->locked_ptr, written_frames * pa_frame_size(&This->ss), free_heap, 0, PA_SEEK_RELATIVE);
    This->tmp_buffer = NULL;
    TRACE("Released %u, wrote %i\n", written_frames * pa_frame_size(&This->ss), written);
    pthread_mutex_unlock(&pulse_lock);

    return S_OK;
}

static const IAudioRenderClientVtbl AudioRenderClient_Vtbl = {
    AudioRenderClient_QueryInterface,
    AudioRenderClient_AddRef,
    AudioRenderClient_Release,
    AudioRenderClient_GetBuffer,
    AudioRenderClient_ReleaseBuffer
};

static HRESULT WINAPI AudioCaptureClient_QueryInterface(
        IAudioCaptureClient *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioCaptureClient))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioCaptureClient_AddRef(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioCaptureClient_Release(IAudioCaptureClient *iface)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioCaptureClient_GetBuffer(IAudioCaptureClient *iface,
        BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *devpos,
        UINT64 *qpcpos)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p, %p, %p)\n", This, data, frames, flags,
            devpos, qpcpos);

    if(!data || !frames || !flags)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_stream_valid(This);
    if(FAILED(hr) || This->locked){
        pthread_mutex_unlock(&pulse_lock);
        return FAILED(hr) ? hr : AUDCLNT_E_OUT_OF_ORDER;
    }
    *data = NULL;
    *flags = 0;
    *frames = This->peeked / pa_frame_size(&This->ss);
    if (*frames)
        *data = This->locked_ptr ? This->locked_ptr : This->tmp_buffer;
    This->locked = *frames;
    pthread_mutex_unlock(&pulse_lock);
    if(devpos || qpcpos)
        IAudioClock_GetPosition(&This->IAudioClock_iface, devpos, qpcpos);

    return *frames ? S_OK : AUDCLNT_S_BUFFER_EMPTY;
}

static HRESULT WINAPI AudioCaptureClient_ReleaseBuffer(
        IAudioCaptureClient *iface, UINT32 done)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%u)\n", This, done);

    pthread_mutex_lock(&pulse_lock);
    if (done) {
        if (This->locked_ptr) {
            pa_stream_drop(This->stream);
            This->locked_ptr = NULL;
        } else {
            HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
            This->tmp_buffer = NULL;
        }
        This->peeked = 0;
    }
    This->locked = 0;
    pthread_mutex_unlock(&pulse_lock);
    return S_OK;
}

static HRESULT WINAPI AudioCaptureClient_GetNextPacketSize(
        IAudioCaptureClient *iface, UINT32 *frames)
{
    ACImpl *This = impl_from_IAudioCaptureClient(iface);

    TRACE("(%p)->(%p)\n", This, frames);
    return AudioClient_GetCurrentPadding(&This->IAudioClient_iface, frames);
}

static const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl =
{
    AudioCaptureClient_QueryInterface,
    AudioCaptureClient_AddRef,
    AudioCaptureClient_Release,
    AudioCaptureClient_GetBuffer,
    AudioCaptureClient_ReleaseBuffer,
    AudioCaptureClient_GetNextPacketSize
};

static HRESULT WINAPI SimpleAudioVolume_QueryInterface(
        ISimpleAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_ISimpleAudioVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI SimpleAudioVolume_AddRef(ISimpleAudioVolume *iface)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI SimpleAudioVolume_Release(ISimpleAudioVolume *iface)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI SimpleAudioVolume_SetMasterVolume(
        ISimpleAudioVolume *iface, float level, const GUID *context)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);

    FIXME("(%p)->(%f, %p) - stub\n", This, level, context);

    return E_NOTIMPL;
}

static HRESULT WINAPI SimpleAudioVolume_GetMasterVolume(
        ISimpleAudioVolume *iface, float *level)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);

    FIXME("(%p)->(%p) - stub\n", This, level);

    return E_NOTIMPL;
}

static HRESULT WINAPI SimpleAudioVolume_SetMute(ISimpleAudioVolume *iface,
        BOOL mute, const GUID *context)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);

    FIXME("(%p)->(%u, %p) - stub\n", This, mute, context);

    return E_NOTIMPL;
}

static HRESULT WINAPI SimpleAudioVolume_GetMute(ISimpleAudioVolume *iface,
        BOOL *mute)
{
    ACImpl *This = impl_from_ISimpleAudioVolume(iface);

    FIXME("(%p)->(%p) - stub\n", This, mute);

    return E_NOTIMPL;
}

static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl  =
{
    SimpleAudioVolume_QueryInterface,
    SimpleAudioVolume_AddRef,
    SimpleAudioVolume_Release,
    SimpleAudioVolume_SetMasterVolume,
    SimpleAudioVolume_GetMasterVolume,
    SimpleAudioVolume_SetMute,
    SimpleAudioVolume_GetMute
};

static HRESULT WINAPI AudioClock_QueryInterface(IAudioClock *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClock))
        *ppv = iface;
    else if(IsEqualIID(riid, &IID_IAudioClock2))
        *ppv = &This->IAudioClock2_iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClock_AddRef(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock_Release(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock_GetFrequency(IAudioClock *iface, UINT64 *freq)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, freq);

    *freq = This->ss.rate;
    return S_OK;
}

static HRESULT WINAPI AudioClock_GetPosition(IAudioClock *iface, UINT64 *pos,
        UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    pa_usec_t time;

    TRACE("(%p)->(%p, %p)\n", This, pos, qpctime);

    if(!pos)
        return E_POINTER;

    pthread_mutex_lock(&pulse_lock);
    if (pa_stream_get_time(This->stream, &time) >= 0)
        *pos = time * This->ss.rate / 1000000 - This->play_ofs;
    else
        *pos = This->play_ofs;
    TRACE("Position: %u\n", (unsigned)*pos);
    pthread_mutex_unlock(&pulse_lock);

    if(qpctime){
        LARGE_INTEGER stamp, freq;
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return S_OK;
}

static HRESULT WINAPI AudioClock_GetCharacteristics(IAudioClock *iface,
        DWORD *chars)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, chars);

    if(!chars)
        return E_POINTER;

    *chars = AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ;

    return S_OK;
}

static const IAudioClockVtbl AudioClock_Vtbl =
{
    AudioClock_QueryInterface,
    AudioClock_AddRef,
    AudioClock_Release,
    AudioClock_GetFrequency,
    AudioClock_GetPosition,
    AudioClock_GetCharacteristics
};

static HRESULT WINAPI AudioClock2_QueryInterface(IAudioClock2 *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClock_QueryInterface(&This->IAudioClock_iface, riid, ppv);
}

static ULONG WINAPI AudioClock2_AddRef(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioClock2_Release(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioClock2_GetDevicePosition(IAudioClock2 *iface,
        UINT64 *pos, UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return AudioClock_GetPosition(&This->IAudioClock_iface, pos, qpctime);
}

static const IAudioClock2Vtbl AudioClock2_Vtbl =
{
    AudioClock2_QueryInterface,
    AudioClock2_AddRef,
    AudioClock2_Release,
    AudioClock2_GetDevicePosition
};

static HRESULT WINAPI AudioSessionControl_QueryInterface(
        IAudioSessionControl2 *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionControl) ||
            IsEqualIID(riid, &IID_IAudioSessionControl2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionControl_AddRef(IAudioSessionControl2 *iface)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);
    return IAudioClient_AddRef(&This->IAudioClient_iface);
}

static ULONG WINAPI AudioSessionControl_Release(IAudioSessionControl2 *iface)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);
    return IAudioClient_Release(&This->IAudioClient_iface);
}

static HRESULT WINAPI AudioSessionControl_GetState(IAudioSessionControl2 *iface,
        AudioSessionState *state)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, state);

    if(!state)
        return E_POINTER;

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetDisplayName(
        IAudioSessionControl2 *iface, WCHAR **name)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, name);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetDisplayName(
        IAudioSessionControl2 *iface, const WCHAR *name, const GUID *session)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, name, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetIconPath(
        IAudioSessionControl2 *iface, WCHAR **path)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, path);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetIconPath(
        IAudioSessionControl2 *iface, const WCHAR *path, const GUID *session)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p, %s) - stub\n", This, path, debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetGroupingParam(
        IAudioSessionControl2 *iface, GUID *group)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, group);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_SetGroupingParam(
        IAudioSessionControl2 *iface, const GUID *group, const GUID *session)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_guid(group),
            debugstr_guid(session));

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_RegisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_UnregisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetSessionIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetSessionInstanceIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetProcessId(
        IAudioSessionControl2 *iface, DWORD *pid)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, pid);

    if(!pid)
        return E_POINTER;

    *pid = GetCurrentProcessId();

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_IsSystemSoundsSession(
        IAudioSessionControl2 *iface)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)\n", This);

    return S_FALSE;
}

static HRESULT WINAPI AudioSessionControl_SetDuckingPreference(
        IAudioSessionControl2 *iface, BOOL optout)
{
    ACImpl *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%d)\n", This, optout);

    return S_OK;
}

static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl =
{
    AudioSessionControl_QueryInterface,
    AudioSessionControl_AddRef,
    AudioSessionControl_Release,
    AudioSessionControl_GetState,
    AudioSessionControl_GetDisplayName,
    AudioSessionControl_SetDisplayName,
    AudioSessionControl_GetIconPath,
    AudioSessionControl_SetIconPath,
    AudioSessionControl_GetGroupingParam,
    AudioSessionControl_SetGroupingParam,
    AudioSessionControl_RegisterAudioSessionNotification,
    AudioSessionControl_UnregisterAudioSessionNotification,
    AudioSessionControl_GetSessionIdentifier,
    AudioSessionControl_GetSessionInstanceIdentifier,
    AudioSessionControl_GetProcessId,
    AudioSessionControl_IsSystemSoundsSession,
    AudioSessionControl_SetDuckingPreference
};

typedef struct _SessionMgr {
    IAudioSessionManager2 IAudioSessionManager2_iface;

    LONG ref;

    IMMDevice *device;
} SessionMgr;

HRESULT WINAPI AudioSessionManager_QueryInterface(IAudioSessionManager2 *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionManager) ||
            IsEqualIID(riid, &IID_IAudioSessionManager2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static inline SessionMgr *impl_from_IAudioSessionManager2(IAudioSessionManager2 *iface)
{
    return CONTAINING_RECORD(iface, SessionMgr, IAudioSessionManager2_iface);
}

ULONG WINAPI AudioSessionManager_AddRef(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

ULONG WINAPI AudioSessionManager_Release(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

HRESULT WINAPI AudioSessionManager_GetAudioSessionControl(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        IAudioSessionControl **out)
{
#if 0
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = (IAudioSessionControl*)&wrapper->IAudioSessionControl2_iface;

    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

HRESULT WINAPI AudioSessionManager_GetSimpleAudioVolume(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        ISimpleAudioVolume **out)
{
#if 0
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = &wrapper->ISimpleAudioVolume_iface;

    return S_OK;
#else
    FIXME("stub\n");
    return E_NOTIMPL;
#endif
}

HRESULT WINAPI AudioSessionManager_GetSessionEnumerator(
        IAudioSessionManager2 *iface, IAudioSessionEnumerator **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, out);
    return E_NOTIMPL;
}

HRESULT WINAPI AudioSessionManager_RegisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

HRESULT WINAPI AudioSessionManager_UnregisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

HRESULT WINAPI AudioSessionManager_RegisterDuckNotification(
        IAudioSessionManager2 *iface, const WCHAR *session_id,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

HRESULT WINAPI AudioSessionManager_UnregisterDuckNotification(
        IAudioSessionManager2 *iface,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl =
{
    AudioSessionManager_QueryInterface,
    AudioSessionManager_AddRef,
    AudioSessionManager_Release,
    AudioSessionManager_GetAudioSessionControl,
    AudioSessionManager_GetSimpleAudioVolume,
    AudioSessionManager_GetSessionEnumerator,
    AudioSessionManager_RegisterSessionNotification,
    AudioSessionManager_UnregisterSessionNotification,
    AudioSessionManager_RegisterDuckNotification,
    AudioSessionManager_UnregisterDuckNotification
};

HRESULT WINAPI AUDDRV_GetAudioSessionManager(IMMDevice *device,
        IAudioSessionManager2 **out)
{
    SessionMgr *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SessionMgr));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioSessionManager2_iface.lpVtbl = &AudioSessionManager2_Vtbl;
    This->device = device;
    This->ref = 1;

    *out = &This->IAudioSessionManager2_iface;

    return S_OK;
}
