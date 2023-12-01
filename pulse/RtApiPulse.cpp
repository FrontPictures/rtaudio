#include "RtApiPulse.h"
#include "utils.h"
#include "OnExit.hpp"

#include <pulse/error.h>
#include <pulse/simple.h>
#include <cstdio>

namespace{
// A structure needed to pass variables for device probing.
struct PaDeviceProbeInfo {
    pa_mainloop_api *paMainLoopApi;
    std::string defaultSinkName;
    std::string defaultSourceName;
    int defaultRate;
    unsigned int *currentDeviceId;
    std::vector< RtAudio::DeviceInfo > *rtDeviceList;
};

static const unsigned int SUPPORTED_SAMPLERATES[] = { 8000, 16000, 22050, 32000,
                                                      44100, 48000, 96000, 192000, 0};

struct rtaudio_pa_format_mapping_t {
    RtAudioFormat rtaudio_format;
    pa_sample_format_t pa_format;
};

static const rtaudio_pa_format_mapping_t supported_sampleformats[] = {
    {RTAUDIO_SINT16, PA_SAMPLE_S16LE},
    {RTAUDIO_SINT24, PA_SAMPLE_S24LE},
    {RTAUDIO_SINT32, PA_SAMPLE_S32LE},
    {RTAUDIO_FLOAT32, PA_SAMPLE_FLOAT32LE},
    {0, PA_SAMPLE_INVALID}};

struct PulseAudioHandle {
    pa_simple *s_play;
    pa_simple *s_rec;
    pthread_t thread;
    pthread_cond_t runnable_cv;
    bool runnable;
    PulseAudioHandle() : s_play(0), s_rec(0), runnable(false) { }
};

// The following 3 functions are called by the device probing
// system. This first one gets overall system information.
static void rt_pa_set_server_info( pa_context *context, const pa_server_info *info, void *userdata )
{
    (void)context;
    pa_sample_spec ss;

    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>( userdata );
    if (!info) {
        paProbeInfo->paMainLoopApi->quit( paProbeInfo->paMainLoopApi, 1 );
        return;
    }

    ss = info->sample_spec;
    paProbeInfo->defaultRate = ss.rate;
    paProbeInfo->defaultSinkName = info->default_sink_name;
    paProbeInfo->defaultSourceName = info->default_source_name;
}

// Used to get output device information.
static void rt_pa_set_sink_info( pa_context * /*c*/, const pa_sink_info *i,
                                 int eol, void *userdata )
{
    if ( eol ) return;

    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>( userdata );
    RtAudio::DeviceInfo info;
    info.name = pa_proplist_gets( i->proplist, "device.description" );
    // TODO get device id for Pulse devices
    info.busID = i->name;
    info.outputChannels = i->sample_spec.channels;
    info.preferredSampleRate = i->sample_spec.rate;
    info.isDefaultOutput = ( paProbeInfo->defaultSinkName == i->name );
    info.supportsOutput = true;
    for ( const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr )
        info.sampleRates.push_back( *sr );
    for ( const rtaudio_pa_format_mapping_t *fm = supported_sampleformats; fm->rtaudio_format; ++fm )
        info.nativeFormats |= fm->rtaudio_format;
    info.ID = *(paProbeInfo->currentDeviceId);
    *(paProbeInfo->currentDeviceId) = info.ID + 1;
    paProbeInfo->rtDeviceList->push_back( info );
}

// Used to get input device information.
static void rt_pa_set_source_info_and_quit( pa_context * /*c*/, const pa_source_info *i,
                                            int eol, void *userdata )
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>( userdata );
    if ( eol ) {
        paProbeInfo->paMainLoopApi->quit( paProbeInfo->paMainLoopApi, 0 );
        return;
    }

    RtAudio::DeviceInfo info;
    info.name = pa_proplist_gets( i->proplist, "device.description" );
    info.inputChannels = i->sample_spec.channels;
    info.preferredSampleRate = i->sample_spec.rate;
    info.isDefaultInput = ( paProbeInfo->defaultSourceName == i->name );
    info.supportsInput = true;
    for ( const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr )
        info.sampleRates.push_back( *sr );
    for ( const rtaudio_pa_format_mapping_t *fm = supported_sampleformats; fm->rtaudio_format; ++fm )
        info.nativeFormats |= fm->rtaudio_format;
    info.ID = *(paProbeInfo->currentDeviceId);
    *(paProbeInfo->currentDeviceId) = info.ID + 1;
    paProbeInfo->rtDeviceList->push_back( info );
}

// This is the initial function that is called when the callback is
// set. This one then calls the functions above.
static void rt_pa_context_state_callback( pa_context *context, void *userdata )
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>( userdata );
    auto state = pa_context_get_state(context);
    switch (state) {
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        break;

    case PA_CONTEXT_READY:
        pa_context_get_server_info( context, rt_pa_set_server_info, userdata ); // server info
        pa_context_get_sink_info_list( context, rt_pa_set_sink_info, userdata ); // output info ... needs to be before input
        pa_context_get_source_info_list( context, rt_pa_set_source_info_and_quit, userdata ); // input info
        break;

    case PA_CONTEXT_TERMINATED:
        paProbeInfo->paMainLoopApi->quit( paProbeInfo->paMainLoopApi, 0 );
        break;

    case PA_CONTEXT_FAILED:
    default:
        paProbeInfo->paMainLoopApi->quit( paProbeInfo->paMainLoopApi, 1 );
    }
}

static void *pulseaudio_callback( void * user )
{
    CallbackInfo *cbi = static_cast<CallbackInfo *>( user );
    RtApiPulse *context = static_cast<RtApiPulse *>( cbi->object );
    volatile bool *isRunning = &cbi->isRunning;

#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
    if (cbi->doRealtime) {
        std::cerr << "RtAudio pulse: " <<
                     (sched_getscheduler(0) == SCHED_RR ? "" : "_NOT_ ") <<
                     "running realtime scheduling" << std::endl;
    }
#endif

    while ( *isRunning ) {
        pthread_testcancel();
        context->callbackEvent();
    }

    pthread_exit( NULL );
}

}

RtApiPulse::~RtApiPulse()
{
    if ( stream_.state != STREAM_CLOSED )
        closeStream();
}

void RtApiPulse::setRateAndFormat(StreamMode mode, RtAudioFormat format, unsigned int sampleRate, pa_sample_spec& ss)
{
    bool sr_found = false;
    for ( const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr ) {
        if ( sampleRate == *sr ) {
            sr_found = true;
            stream_.sampleRate = sampleRate;
            ss.rate = sampleRate;
            break;
        }
    }
    if ( !sr_found ) {
        stream_.sampleRate = sampleRate;
        ss.rate = sampleRate;
    }

    bool sf_found = 0;
    for ( const rtaudio_pa_format_mapping_t *sf = supported_sampleformats;
         sf->rtaudio_format && sf->pa_format != PA_SAMPLE_INVALID; ++sf ) {
        if ( format == sf->rtaudio_format ) {
            sf_found = true;
            stream_.userFormat = sf->rtaudio_format;
            stream_.deviceFormat[mode] = stream_.userFormat;
            ss.format = sf->pa_format;
            break;
        }
    }
    if ( !sf_found ) { // Use internal data format conversion.
        stream_.userFormat = format;
        stream_.deviceFormat[mode] = RTAUDIO_FLOAT32;
        ss.format = PA_SAMPLE_FLOAT32LE;
    }
}

bool RtApiPulse::setupThread(RtAudio::StreamOptions *options, pthread_t *pah)
{
    stream_.callbackInfo.object = this;
    // Set the thread attributes for joinable and realtime scheduling
    // priority (optional).  The higher priority will only take affect
    // if the program is run as root or suid. Note, under Linux
    // processes with CAP_SYS_NICE privilege, a user can change
    // scheduling policy and priority (thus need not be root). See
    // POSIX "capabilities".
    pthread_attr_t attr;
    pthread_attr_init( &attr );
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_JOINABLE );
#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
    if ( options && options->flags & RTAUDIO_SCHEDULE_REALTIME ) {
        stream_.callbackInfo.doRealtime = true;
        struct sched_param param;
        int priority = options->priority;
        int min = sched_get_priority_min( SCHED_RR );
        int max = sched_get_priority_max( SCHED_RR );
        if ( priority < min ) priority = min;
        else if ( priority > max ) priority = max;
        param.sched_priority = priority;

        // Set the policy BEFORE the priority. Otherwise it fails.
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
        // This is definitely required. Otherwise it fails.
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedparam(&attr, &param);
    }
    else
        pthread_attr_setschedpolicy( &attr, SCHED_OTHER );
#else
    pthread_attr_setschedpolicy( &attr, SCHED_OTHER );
#endif

    stream_.callbackInfo.isRunning = true;
    int result = pthread_create( pah, &attr, pulseaudio_callback, (void *)&stream_.callbackInfo);
    pthread_attr_destroy(&attr);

    if(result != 0) {
        // Failed. Try instead with default attributes.
        result = pthread_create( pah, NULL, pulseaudio_callback, (void *)&stream_.callbackInfo);
        if(result != 0) {
            stream_.callbackInfo.isRunning = false;
            errorText_ = "RtApiPulse::probeDeviceOpen: error creating thread.";
            return FAILURE;
        }
    }
    return SUCCESS;;
}

bool RtApiPulse::probeDeviceOpen( unsigned int deviceId, StreamMode mode,
                                  unsigned int channels, unsigned int firstChannel,
                                  unsigned int sampleRate, RtAudioFormat format,
                                  unsigned int *bufferSize, RtAudio::StreamOptions *options )
{
    PulseAudioHandle *pah = 0;
    unsigned long bufferBytes = 0;
    pa_sample_spec ss {};

    OnExit onExit([&](){
        if ( pah && stream_.callbackInfo.isRunning ) {
            pthread_cond_destroy( &pah->runnable_cv );
            delete pah;
            stream_.apiHandle = 0;
        }

        for ( int i=0; i<2; i++ ) {
            if ( stream_.userBuffer[i] ) {
                free( stream_.userBuffer[i] );
                stream_.userBuffer[i] = 0;
            }
        }

        if ( stream_.deviceBuffer ) {
            free( stream_.deviceBuffer );
            stream_.deviceBuffer = 0;
        }

        stream_.state = STREAM_CLOSED;
    });

    const char *dev_name = NULL;
    int deviceIdx = -1;
    for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
        if ( deviceList_[m].ID == deviceId ) {
            deviceIdx = m;
            dev_name = deviceList_[m].busID.c_str();
            break;
        }
    }

    if ( deviceIdx < 0 ) return false;

    if ( firstChannel != 0 ) {
        errorText_ = "PulseAudio does not support channel offset mapping.";
        return false;
    }

    if ( mode==INPUT && deviceList_[deviceIdx].inputChannels < channels ) {
        errorText_ = "PulseAudio device does not support requested input channel count.";
        return false;
    }
    if ( mode==OUTPUT && deviceList_[deviceIdx].outputChannels < channels ) {
        errorText_ = "PulseAudio device does not support requested output channel count.";
        return false;
    }

    ss.channels = channels;

    setRateAndFormat(mode, format, sampleRate, ss);

    // Set other stream parameters.
    if ( options && options->flags & RTAUDIO_NONINTERLEAVED ) stream_.userInterleaved = false;
    else stream_.userInterleaved = true;
    stream_.deviceInterleaved[mode] = true;
    stream_.nBuffers = options ? options->numberOfBuffers : 1;
    stream_.doByteSwap[mode] = false;
    stream_.nUserChannels[mode] = channels;
    stream_.nDeviceChannels[mode] = channels + firstChannel;
    stream_.channelOffset[mode] = 0;
    std::string streamName = "RtAudio";

    // Set flags for buffer conversion.
    stream_.doConvertBuffer[mode] = false;
    if ( stream_.userFormat != stream_.deviceFormat[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.nUserChannels[mode] < stream_.nDeviceChannels[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.userInterleaved != stream_.deviceInterleaved[mode] )
        stream_.doConvertBuffer[mode] = true;

    // Allocate necessary internal buffers.
    bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes( stream_.userFormat );
    stream_.userBuffer[mode] = (char *) calloc( bufferBytes, 1 );
    if ( stream_.userBuffer[mode] == NULL ) {
        errorText_ = "RtApiPulse::probeDeviceOpen: error allocating user buffer memory.";
        return FAILURE;
    }
    stream_.bufferSize = *bufferSize;

    if ( stream_.doConvertBuffer[mode] ) {

        bool makeBuffer = true;
        bufferBytes = stream_.nDeviceChannels[mode] * formatBytes( stream_.deviceFormat[mode] );
        if ( mode == INPUT ) {
            if ( stream_.mode == OUTPUT && stream_.deviceBuffer ) {
                unsigned long bytesOut = stream_.nDeviceChannels[0] * formatBytes( stream_.deviceFormat[0] );
                if ( bufferBytes <= bytesOut ) makeBuffer = false;
            }
        }

        if ( makeBuffer ) {
            bufferBytes *= *bufferSize;
            if ( stream_.deviceBuffer ) free( stream_.deviceBuffer );
            stream_.deviceBuffer = (char *) calloc( bufferBytes, 1 );
            if ( stream_.deviceBuffer == NULL ) {
                errorText_ = "RtApiPulse::probeDeviceOpen: error allocating device buffer memory.";
                return FAILURE;
            }
        }
    }

    stream_.deviceId[mode] = deviceIdx;

    // Setup the buffer conversion information structure.
    if ( stream_.doConvertBuffer[mode] ) setConvertInfo( mode, firstChannel );

    if ( !stream_.apiHandle ) {
        PulseAudioHandle *pah = new PulseAudioHandle;
        if ( !pah ) {
            errorText_ = "RtApiPulse::probeDeviceOpen: error allocating memory for handle.";
            return FAILURE;
        }

        stream_.apiHandle = pah;
        if ( pthread_cond_init( &pah->runnable_cv, NULL ) != 0 ) {
            errorText_ = "RtApiPulse::probeDeviceOpen: error creating condition variable.";
            return FAILURE;
        }
    }
    pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

    int error = 0;
    struct pa_channel_map mapping;
    mapping = {};
    if (pa_channel_map_init_extend(&mapping, ss.channels, PA_CHANNEL_MAP_WAVEEX) == NULL){
        errorText_ = "RtApiPulse::probeDeviceOpen: error mapping.";
        return FAILURE;
    }

    if ( options && !options->streamName.empty() ) streamName = options->streamName;
    switch (mode) {
        pa_buffer_attr buffer_attr;
    case INPUT:
        buffer_attr.fragsize = bufferBytes;
        if ( options && options->numberOfBuffers > 0 )
            buffer_attr.maxlength = bufferBytes * (options->numberOfBuffers + 1);
        else
            buffer_attr.maxlength = bufferBytes * 4;

        pah->s_rec = pa_simple_new( NULL, streamName.c_str(), PA_STREAM_RECORD,
                                    dev_name, "Record", &ss, &mapping, &buffer_attr, &error );
        if ( !pah->s_rec ) {
            errorText_ = "RtApiPulse::probeDeviceOpen: error connecting input to PulseAudio server.";
            return FAILURE;
        }
        break;
    case OUTPUT: {
        pa_buffer_attr * attr_ptr;

        if ( options && options->numberOfBuffers > 0 ) {
            // pa_buffer_attr::fragsize is recording-only.
            // Hopefully PortAudio won't access uninitialized fields.
            buffer_attr.maxlength = bufferBytes * options->numberOfBuffers;
            buffer_attr.minreq = -1;
            buffer_attr.prebuf = -1;
            buffer_attr.tlength = -1;
            attr_ptr = &buffer_attr;
        } else {
            attr_ptr = nullptr;
        }

        pah->s_play = pa_simple_new( NULL, streamName.c_str(), PA_STREAM_PLAYBACK,
                                     dev_name, "Playback", &ss, &mapping, attr_ptr, &error );
        if ( !pah->s_play ) {
            errorText_ = "RtApiPulse::probeDeviceOpen: error connecting output to PulseAudio server.";
            return FAILURE;
        }
        break;
    }
    case DUPLEX:
        /* Note: We could add DUPLEX by synchronizing multiple streams,
       but it would mean moving from Simple API to Asynchronous API:
       https://freedesktop.org/software/pulseaudio/doxygen/streams.html#sync_streams */
        errorText_ = "RtApiPulse::probeDeviceOpen: duplex not supported for PulseAudio.";
        return FAILURE;
    default:
        return FAILURE;
    }

    if ( stream_.mode == UNINITIALIZED )
        stream_.mode = mode;
    else if ( stream_.mode == mode )
        return FAILURE;
    else
        return FAILURE;

    if ( !stream_.callbackInfo.isRunning ) {
        if (setupThread(options, &pah->thread) != SUCCESS){
            return FAILURE;
        }
    }
    stream_.state = STREAM_STOPPED;

    onExit.invalidate();
    return SUCCESS;
}

void RtApiPulse::closeStream( void )
{
    PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

    stream_.callbackInfo.isRunning = false;
    if ( pah ) {
        {
            MutexRaii<StreamMutex> lock(stream_.mutex);
            pah->runnable = true;
            pthread_cond_signal( &pah->runnable_cv );
        }

        pthread_join( pah->thread, 0 );
        if ( pah->s_play ) {
            pa_simple_flush( pah->s_play, NULL );
            pa_simple_free( pah->s_play );
        }
        if ( pah->s_rec )
            pa_simple_free( pah->s_rec );

        pthread_cond_destroy( &pah->runnable_cv );
        delete pah;
        stream_.apiHandle = 0;
    }

    if ( stream_.userBuffer[0] ) {
        free( stream_.userBuffer[0] );
        stream_.userBuffer[0] = 0;
    }
    if ( stream_.userBuffer[1] ) {
        free( stream_.userBuffer[1] );
        stream_.userBuffer[1] = 0;
    }

    clearStreamInfo();
}

void RtApiPulse::callbackEvent( void )
{
    PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );
    MutexRaii<StreamMutex> lock(stream_.mutex);

    if ( stream_.state == STREAM_STOPPED || stream_.state == STREAM_ERROR) {
        while ( !pah->runnable )
            pthread_cond_wait( &pah->runnable_cv, &stream_.mutex );

        if ( stream_.state != STREAM_RUNNING ) {
            return;
        }
    }

    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiPulse::callbackEvent(): the stream is closed ... "
                     "this shouldn't happen!";
        error( RTAUDIO_WARNING );
        return;
    }

    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;
    callback( stream_.userBuffer[OUTPUT], stream_.userBuffer[INPUT],
                                 stream_.bufferSize, streamTime, status,
                                 stream_.callbackInfo.userData );
    void *pulse_in = stream_.doConvertBuffer[INPUT] ? stream_.deviceBuffer : stream_.userBuffer[INPUT];
    void *pulse_out = stream_.doConvertBuffer[OUTPUT] ? stream_.deviceBuffer : stream_.userBuffer[OUTPUT];

    if ( stream_.state != STREAM_RUNNING )
        return;

    int pa_error;
    size_t bytes;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
        if ( stream_.doConvertBuffer[OUTPUT] ) {
            convertBuffer( stream_.deviceBuffer,
                           stream_.userBuffer[OUTPUT],
                          stream_.convertInfo[OUTPUT], stream_.bufferSize, StreamMode::OUTPUT);
            bytes = stream_.nDeviceChannels[OUTPUT] * stream_.bufferSize *
                    formatBytes( stream_.deviceFormat[OUTPUT] );
        } else
            bytes = stream_.nUserChannels[OUTPUT] * stream_.bufferSize *
                    formatBytes( stream_.userFormat );

        if ( pa_simple_write( pah->s_play, pulse_out, bytes, &pa_error ) < 0 ) {
            errorStream_ << "RtApiPulse::callbackEvent: audio write error, " <<
                            pa_strerror( pa_error ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
            stream_.state = STREAM_ERROR;
        }
    }

    if ( stream_.mode == INPUT || stream_.mode == DUPLEX) {
        if ( stream_.doConvertBuffer[INPUT] )
            bytes = stream_.nDeviceChannels[INPUT] * stream_.bufferSize *
                    formatBytes( stream_.deviceFormat[INPUT] );
        else
            bytes = stream_.nUserChannels[INPUT] * stream_.bufferSize *
                    formatBytes( stream_.userFormat );

        if ( pa_simple_read( pah->s_rec, pulse_in, bytes, &pa_error ) < 0 ) {
            errorStream_ << "RtApiPulse::callbackEvent: audio read error, " <<
                            pa_strerror( pa_error ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
            stream_.state = STREAM_ERROR;
        }
        if ( stream_.doConvertBuffer[INPUT] ) {
            convertBuffer( stream_.userBuffer[INPUT],
                           stream_.deviceBuffer,
                          stream_.convertInfo[INPUT], stream_.bufferSize, StreamMode::INPUT);
        }
    }
    RtApi::tickStreamTime();    
}

void RtApiPulse::listDevices()
{
    pa_mainloop *ml = NULL;
    pa_context *context = NULL;
    char *server = NULL;
    int ret = 1;
    PaDeviceProbeInfo paProbeInfo{};

    deviceList_.clear();

    OnExit onExit([&](){
        if (context)
            pa_context_unref(context);
        if (ml)
            pa_mainloop_free(ml);

        pa_xfree(server);

        context = nullptr;
        ml = nullptr;
        server = nullptr;
    });

    if (!(ml = pa_mainloop_new())) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_new() failed.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return;
    }

    paProbeInfo.paMainLoopApi = pa_mainloop_get_api( ml );
    paProbeInfo.currentDeviceId = &currentDeviceId_;
    paProbeInfo.rtDeviceList = &deviceList_;

    if (!(context = pa_context_new_with_proplist( paProbeInfo.paMainLoopApi, NULL, NULL ))) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return;
    }

    pa_context_set_state_callback( context, rt_pa_context_state_callback, &paProbeInfo );

    if (pa_context_connect( context, server, PA_CONTEXT_NOFLAGS, NULL ) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_connect() failed: "
                     << pa_strerror(pa_context_errno(context));
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return;
    }

    if (pa_mainloop_run( ml, &ret ) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_run() failed.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return;
    }

    if (ret != 0) {
        errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return;
    }
}

bool RtApiPulse::probeSingleDeviceInfo(RtAudio::DeviceInfo & info)
{
    return true;
}

RtAudioErrorType RtApiPulse::startStream( void )
{
    MutexRaii<StreamMutex> lock(stream_.mutex);
    if ( stream_.state != STREAM_STOPPED ) {
        if ( stream_.state == STREAM_RUNNING )
            errorText_ = "RtApiPulse::startStream(): the stream is already running!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiPulse::startStream(): the stream is stopping or closed!";
        if ( stream_.state == STREAM_ERROR )
            errorText_ = "RtApiPulse::startStream(): the stream is in error state!";
        return error( RTAUDIO_WARNING );
    }

    PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );
    stream_.state = STREAM_RUNNING;
    pah->runnable = true;
    pthread_cond_signal( &pah->runnable_cv );
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiPulse::stopStream( void )
{
    MutexRaii<StreamMutex> lock(stream_.mutex);
    if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiPulse::stopStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiPulse::stopStream(): the stream is closed!";
        if ( stream_.state == STREAM_ERROR )
            errorText_ = "RtApiPulse::startStream(): the stream is in error state!";
        return error( RTAUDIO_WARNING );
    }

    PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

    stream_.state = STREAM_STOPPED;

    if ( pah ) {
        pah->runnable = false;
        if ( pah->s_play ) {
            int pa_error;
            if ( pa_simple_drain( pah->s_play, &pa_error ) < 0 ) {
                errorStream_ << "RtApiPulse::stopStream: error draining output device, " <<
                                pa_strerror( pa_error ) << ".";
                errorText_ = errorStream_.str();
                return error( RTAUDIO_SYSTEM_ERROR );
            }
        }
    }
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiPulse::abortStream( void )
{
    MutexRaii<StreamMutex> lock(stream_.mutex);
    if ( stream_.state != STREAM_RUNNING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiPulse::abortStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiPulse::abortStream(): the stream is stopping or closed!";
        if ( stream_.state == STREAM_ERROR )
            errorText_ = "RtApiPulse::startStream(): the stream is in error state!";
        return error( RTAUDIO_WARNING );
    }

    PulseAudioHandle *pah = static_cast<PulseAudioHandle*>( stream_.apiHandle );

    stream_.state = STREAM_STOPPED;

    if ( pah ) {
        pah->runnable = false;
        if ( pah->s_play ) {
            int pa_error;
            if ( pa_simple_flush( pah->s_play, &pa_error ) < 0 ) {
                errorStream_ << "RtApiPulse::abortStream: error flushing output device, " <<
                    pa_strerror( pa_error ) << ".";
                errorText_ = errorStream_.str();
                return error( RTAUDIO_SYSTEM_ERROR );
            }
        }
    }
    return RTAUDIO_NO_ERROR;
}
