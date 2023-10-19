#include <alsa/asoundlib.h>
#include <unistd.h>
#include <limits.h>
#include "RtApiAlsa.h"
#include "utils.h"
#include <optional>

namespace{
static void *alsaCallbackHandler( void *ptr )
{
    CallbackInfo *info = (CallbackInfo *) ptr;
    RtApiAlsa *object = (RtApiAlsa *) info->object;
    bool *isRunning = &info->isRunning;

#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
    if ( info->doRealtime ) {
        std::cerr << "RtAudio alsa: " <<
                     (sched_getscheduler(0) == SCHED_RR ? "" : "_NOT_ ") <<
                     "running realtime scheduling" << std::endl;
    }
#endif

    while ( *isRunning == true ) {
        pthread_testcancel();
        object->callbackEvent();
    }

    pthread_exit( NULL );
}

// A structure to hold various information related to the ALSA API
// implementation.
struct AlsaHandle {
    snd_pcm_t *handles[2];
    bool synchronized;
    bool xrun[2];
    pthread_cond_t runnable_cv;
    bool runnable;

    AlsaHandle()
#if _cplusplus >= 201103L
        :handles{nullptr, nullptr}, synchronized(false), runnable(false) { xrun[0] = false; xrun[1] = false; }
#else
        : synchronized(false), runnable(false) { handles[0] = NULL; handles[1] = NULL; xrun[0] = false; xrun[1] = false; }
#endif
};
}

RtApiAlsa :: RtApiAlsa()
{
    // Nothing to do here.
}

RtApiAlsa :: ~RtApiAlsa()
{
    if ( stream_.state != STREAM_CLOSED ) closeStream();
}

bool RtApiAlsa :: probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                                   unsigned int firstChannel, unsigned int sampleRate,
                                   RtAudioFormat format, unsigned int *bufferSize,
                                   RtAudio::StreamOptions *options )

{
#if defined(__RTAUDIO_DEBUG__)
    struct SndOutputTdealloc {
        SndOutputTdealloc() : _out(NULL) { snd_output_stdio_attach(&_out, stderr, 0); }
        ~SndOutputTdealloc() { snd_output_close(_out); }
        operator snd_output_t*() { return _out; }
        snd_output_t *_out;
    } out;
#endif

    std::string name;
    for ( auto& id : deviceList_) {
        if ( id.ID == deviceId ) {
            name = id.busID;
            break;
        }
    }

    if (name.empty()){
        errorStream_ << "RtApiAlsa::probeDeviceOpen: device not found (" << name << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    snd_pcm_stream_t stream;
    if ( mode == OUTPUT )
        stream = SND_PCM_STREAM_PLAYBACK;
    else
        stream = SND_PCM_STREAM_CAPTURE;

    snd_pcm_t *phandle;
    int openMode = SND_PCM_ASYNC;
    if ( options && options->flags & RTAUDIO_ALSA_NONBLOCK ) {
        openMode = SND_PCM_NONBLOCK;
    }
    int result = snd_pcm_open( &phandle, name.c_str(), stream, openMode );
    if ( result < 0 ) {
        if ( mode == OUTPUT )
            errorStream_ << "RtApiAlsa::probeDeviceOpen: pcm device (" << name << ") won't open for output.";
        else
            errorStream_ << "RtApiAlsa::probeDeviceOpen: pcm device (" << name << ") won't open for input.";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Fill the parameter structure.
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca( &hw_params );
    result = snd_pcm_hw_params_any( phandle, hw_params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error getting pcm device (" << name << ") parameters, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

#if defined(__RTAUDIO_DEBUG__)
    fprintf( stderr, "\nRtApiAlsa: dump hardware params just after device open:\n\n" );
    snd_pcm_hw_params_dump( hw_params, out );
#endif

    // Set access ... check user preference.
    if ( options && options->flags & RTAUDIO_NONINTERLEAVED ) {
        stream_.userInterleaved = false;
        result = snd_pcm_hw_params_set_access( phandle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED );
        if ( result < 0 ) {
            result = snd_pcm_hw_params_set_access( phandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED );
            stream_.deviceInterleaved[mode] =  true;
        }
        else
            stream_.deviceInterleaved[mode] = false;
    }
    else {
        stream_.userInterleaved = true;
        result = snd_pcm_hw_params_set_access( phandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED );
        if ( result < 0 ) {
            result = snd_pcm_hw_params_set_access( phandle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED );
            stream_.deviceInterleaved[mode] =  false;
        }
        else
            stream_.deviceInterleaved[mode] =  true;
    }

    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting pcm device (" << name << ") access, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Determine how to set the device format.
    stream_.userFormat = format;
    snd_pcm_format_t deviceFormat = SND_PCM_FORMAT_UNKNOWN;

    if ( format == RTAUDIO_SINT8 )
        deviceFormat = SND_PCM_FORMAT_S8;
    else if ( format == RTAUDIO_SINT16 )
        deviceFormat = SND_PCM_FORMAT_S16;
    else if ( format == RTAUDIO_SINT24 )
        deviceFormat = SND_PCM_FORMAT_S24;
    else if ( format == RTAUDIO_SINT32 )
        deviceFormat = SND_PCM_FORMAT_S32;
    else if ( format == RTAUDIO_FLOAT32 )
        deviceFormat = SND_PCM_FORMAT_FLOAT;
    else if ( format == RTAUDIO_FLOAT64 )
        deviceFormat = SND_PCM_FORMAT_FLOAT64;

    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat) == 0) {
        stream_.deviceFormat[mode] = format;
        goto setFormat;
    }

    // The user requested format is not natively supported by the device.
    deviceFormat = SND_PCM_FORMAT_FLOAT64;
    if ( snd_pcm_hw_params_test_format( phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_FLOAT64;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_FLOAT;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_FLOAT32;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_S32;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT32;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_S24;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT24;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_S16;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT16;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_S8;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT8;
        goto setFormat;
    }

    deviceFormat = SND_PCM_FORMAT_S24_3LE;
    if ( snd_pcm_hw_params_test_format(phandle, hw_params, deviceFormat ) == 0 ) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT24;
        goto setFormat;
    }

    // If we get here, no supported format was found.
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceOpen: pcm device (" << name << ") data format not supported by RtAudio.";
    errorText_ = errorStream_.str();
    return FAILURE;

setFormat:
    result = snd_pcm_hw_params_set_format( phandle, hw_params, deviceFormat );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting pcm device (" << name << ") data format, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Determine whether byte-swaping is necessary.
    stream_.doByteSwap[mode] = false;
    if ( deviceFormat != SND_PCM_FORMAT_S8 ) {
        result = snd_pcm_format_cpu_endian( deviceFormat );
        if ( result == 0 )
            stream_.doByteSwap[mode] = true;
        else if (result < 0) {
            snd_pcm_close( phandle );
            errorStream_ << "RtApiAlsa::probeDeviceOpen: error getting pcm device (" << name << ") endian-ness, " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    }

    // Set the sample rate.
    result = snd_pcm_hw_params_set_rate_near( phandle, hw_params, (unsigned int*) &sampleRate, 0 );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting sample rate on device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Determine the number of channels for this device.  We support a possible
    // minimum device channel number > than the value requested by the user.
    stream_.nUserChannels[mode] = channels;
    unsigned int value;
    result = snd_pcm_hw_params_get_channels_max( hw_params, &value );
    unsigned int deviceChannels = value;
    if ( result < 0 || deviceChannels < channels + firstChannel ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: requested channel parameters not supported by device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    result = snd_pcm_hw_params_get_channels_min( hw_params, &value );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error getting minimum channels for device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }
    deviceChannels = value;
    if ( deviceChannels < channels + firstChannel ) deviceChannels = channels + firstChannel;
    stream_.nDeviceChannels[mode] = deviceChannels;

    // Set the device channels.
    result = snd_pcm_hw_params_set_channels( phandle, hw_params, deviceChannels );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting channels for device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Set the buffer (or period) size.
    int dir = 0;
    snd_pcm_uframes_t periodSize = *bufferSize;
    result = snd_pcm_hw_params_set_period_size_near( phandle, hw_params, &periodSize, &dir );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting period size for device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }
    *bufferSize = periodSize;

    // Set the buffer number, which in ALSA is referred to as the "period".
    unsigned int periods = 0;
    if ( options && options->flags & RTAUDIO_MINIMIZE_LATENCY ) periods = 2;
    if ( options && options->numberOfBuffers > 0 ) periods = options->numberOfBuffers;
    if ( periods < 2 ) periods = 4; // a fairly safe default value
    result = snd_pcm_hw_params_set_periods_near( phandle, hw_params, &periods, &dir );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting periods for device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // If attempting to setup a duplex stream, the bufferSize parameter
    // MUST be the same in both directions!
    if ( stream_.mode == OUTPUT && mode == INPUT && *bufferSize != stream_.bufferSize ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: system error setting buffer size for duplex stream on device (" << name << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    stream_.bufferSize = *bufferSize;

    // Install the hardware configuration
    result = snd_pcm_hw_params( phandle, hw_params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error installing hardware configuration on device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

#if defined(__RTAUDIO_DEBUG__)
    fprintf(stderr, "\nRtApiAlsa: dump hardware params after installation:\n\n");
    snd_pcm_hw_params_dump( hw_params, out );
#endif

    // Set the software configuration to fill buffers with zeros and prevent device stopping on xruns.
    snd_pcm_sw_params_t *sw_params = NULL;
    snd_pcm_sw_params_alloca( &sw_params );
    snd_pcm_sw_params_current( phandle, sw_params );
    snd_pcm_sw_params_set_start_threshold( phandle, sw_params, *bufferSize );
    snd_pcm_sw_params_set_stop_threshold( phandle, sw_params, ULONG_MAX );
    snd_pcm_sw_params_set_silence_threshold( phandle, sw_params, 0 );

    // The following two settings were suggested by Theo Veenker
    //snd_pcm_sw_params_set_avail_min( phandle, sw_params, *bufferSize );
    //snd_pcm_sw_params_set_xfer_align( phandle, sw_params, 1 );

    // here are two options for a fix
    //snd_pcm_sw_params_set_silence_size( phandle, sw_params, ULONG_MAX );
    snd_pcm_uframes_t val;
    snd_pcm_sw_params_get_boundary( sw_params, &val );
    snd_pcm_sw_params_set_silence_size( phandle, sw_params, val );

    result = snd_pcm_sw_params( phandle, sw_params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error installing software configuration on device (" << name << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

#if defined(__RTAUDIO_DEBUG__)
    fprintf(stderr, "\nRtApiAlsa: dump software params after installation:\n\n");
    snd_pcm_sw_params_dump( sw_params, out );
#endif

    // Set flags for buffer conversion
    stream_.doConvertBuffer[mode] = false;
    if ( stream_.userFormat != stream_.deviceFormat[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.nUserChannels[mode] < stream_.nDeviceChannels[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
         stream_.nUserChannels[mode] > 1 )
        stream_.doConvertBuffer[mode] = true;

    // Allocate the ApiHandle if necessary and then save.
    AlsaHandle *apiInfo = 0;
    if ( stream_.apiHandle == 0 ) {
        try {
            apiInfo = (AlsaHandle *) new AlsaHandle;
        }
        catch ( std::bad_alloc& ) {
            errorText_ = "RtApiAlsa::probeDeviceOpen: error allocating AlsaHandle memory.";
            goto error;
        }

        if ( pthread_cond_init( &apiInfo->runnable_cv, NULL ) ) {
            errorText_ = "RtApiAlsa::probeDeviceOpen: error initializing pthread condition variable.";
            goto error;
        }

        stream_.apiHandle = (void *) apiInfo;
        apiInfo->handles[0] = 0;
        apiInfo->handles[1] = 0;
    }
    else {
        apiInfo = (AlsaHandle *) stream_.apiHandle;
    }
    apiInfo->handles[mode] = phandle;
    phandle = 0;

    // Allocate necessary internal buffers.
    unsigned long bufferBytes;
    bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes( stream_.userFormat );
    stream_.userBuffer[mode] = (char *) calloc( bufferBytes, 1 );
    if ( stream_.userBuffer[mode] == NULL ) {
        errorText_ = "RtApiAlsa::probeDeviceOpen: error allocating user buffer memory.";
        goto error;
    }

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
                errorText_ = "RtApiAlsa::probeDeviceOpen: error allocating device buffer memory.";
                goto error;
            }
        }
    }

    stream_.sampleRate = sampleRate;
    stream_.nBuffers = periods;
    stream_.deviceId[mode] = deviceId;
    stream_.state = STREAM_STOPPED;

    // Setup the buffer conversion information structure.
    if ( stream_.doConvertBuffer[mode] ) setConvertInfo( mode, firstChannel );

    // Setup thread if necessary.
    if ( stream_.mode == OUTPUT && mode == INPUT ) {
        // We had already set up an output stream.
        stream_.mode = DUPLEX;
        // Link the streams if possible.
        apiInfo->synchronized = false;
        if ( snd_pcm_link( apiInfo->handles[0], apiInfo->handles[1] ) == 0 )
            apiInfo->synchronized = true;
        else {
            errorText_ = "RtApiAlsa::probeDeviceOpen: unable to synchronize input and output devices.";
            error( RTAUDIO_WARNING );
        }
    }
    else {
        stream_.mode = mode;

        // Setup callback thread.
        stream_.callbackInfo.object = (void *) this;

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
        result = pthread_create( &stream_.callbackInfo.thread, &attr, alsaCallbackHandler, &stream_.callbackInfo );
        pthread_attr_destroy( &attr );
        if ( result ) {
            // Failed. Try instead with default attributes.
            result = pthread_create( &stream_.callbackInfo.thread, NULL, alsaCallbackHandler, &stream_.callbackInfo );
            if ( result ) {
                stream_.callbackInfo.isRunning = false;
                errorText_ = "RtApiAlsa::error creating callback thread!";
                goto error;
            }
        }
    }

    return SUCCESS;

error:
    if ( apiInfo ) {
        pthread_cond_destroy( &apiInfo->runnable_cv );
        if ( apiInfo->handles[0] ) snd_pcm_close( apiInfo->handles[0] );
        if ( apiInfo->handles[1] ) snd_pcm_close( apiInfo->handles[1] );
        delete apiInfo;
        stream_.apiHandle = 0;
    }

    if ( phandle) snd_pcm_close( phandle );

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
    return FAILURE;
}

int RtApiAlsa::processInput()
{
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    int result;
    char *buffer;
    int channels;
    snd_pcm_t **handle;
    snd_pcm_sframes_t frames;
    RtAudioFormat format;
    handle = (snd_pcm_t **) apiInfo->handles;

    // Setup parameters.
    if ( stream_.doConvertBuffer[1] ) {
        buffer = stream_.deviceBuffer;
        channels = stream_.nDeviceChannels[1];
        format = stream_.deviceFormat[1];
    }
    else {
        buffer = stream_.userBuffer[1];
        channels = stream_.nUserChannels[1];
        format = stream_.userFormat;
    }

    // Read samples from device in interleaved/non-interleaved format.
    int readSamples = 0;

    while (readSamples < stream_.bufferSize){
        if ( stream_.deviceInterleaved[1] )
            result = snd_pcm_readi( handle[1], buffer + (channels * readSamples * formatBytes (format)), stream_.bufferSize - readSamples );
        else {
            void *bufs[channels];
            size_t offset = stream_.bufferSize * formatBytes( format );
            for ( int i=0; i<channels; i++ )
                bufs[i] = (void *) (buffer + (i * offset));
            result = snd_pcm_readn( handle[1], bufs, stream_.bufferSize );
        }
        if ( result <= 0) {
            // Either an error or overrun occurred.
            if ( result == -EPIPE ) {
                snd_pcm_state_t state = snd_pcm_state( handle[1] );
                if ( state == SND_PCM_STATE_XRUN ) {
                    apiInfo->xrun[1] = true;
                    result = snd_pcm_prepare( handle[1] );
                    if ( result < 0 ) {
                        errorStream_ << "RtApiAlsa::callbackEvent: error preparing device after overrun, " << snd_strerror( result ) << ".";
                        errorText_ = errorStream_.str();
                    }
                }
                else {
                    errorStream_ << "RtApiAlsa::callbackEvent: error, current state is " << snd_pcm_state_name( state ) << ", " << snd_strerror( result ) << ".";
                    errorText_ = errorStream_.str();
                }
            }else if (result == -EAGAIN){
                uint64_t bufsize64 = stream_.bufferSize;
                usleep(bufsize64 * (1000000 / 2) / stream_.sampleRate);
                continue;
            }else {
                errorStream_ << "RtApiAlsa::callbackEvent: audio read error, " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
            }
            error( RTAUDIO_WARNING );
            return 0;
        }
        readSamples += result;
    }

    // Do byte swapping if necessary.
    if ( stream_.doByteSwap[1] )
        byteSwapBuffer( buffer, readSamples * channels, format );

    // Do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[1] )
        convertBuffer( stream_.userBuffer[1], stream_.deviceBuffer, stream_.convertInfo[1], readSamples);

    // Check stream latency
    result = snd_pcm_delay( handle[1], &frames );
    if ( result == 0 && frames > 0 ) stream_.latency[1] = frames;
    return readSamples;
}

bool RtApiAlsa::processOutput(int samples)
{
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    int result;
    char *buffer;
    int channels;
    snd_pcm_t **handle;
    snd_pcm_sframes_t frames;
    RtAudioFormat format;
    handle = (snd_pcm_t **) apiInfo->handles;
    // Setup parameters and do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[0] ) {
        buffer = stream_.deviceBuffer;
        convertBuffer( buffer, stream_.userBuffer[0], stream_.convertInfo[0], samples);
        channels = stream_.nDeviceChannels[0];
        format = stream_.deviceFormat[0];
    }
    else {
        buffer = stream_.userBuffer[0];
        channels = stream_.nUserChannels[0];
        format = stream_.userFormat;
    }

    // Do byte swapping if necessary.
    if ( stream_.doByteSwap[0] )
        byteSwapBuffer(buffer, samples * channels, format);

    // Write samples to device in interleaved/non-interleaved format.
    int samplesPlayed = 0;

    while (samplesPlayed < samples){
        if ( stream_.deviceInterleaved[0] )
            result = snd_pcm_writei( handle[0], buffer + (samplesPlayed * formatBytes(format) * channels), samples - samplesPlayed );
        else {
            void *bufs[channels];
            size_t offset = stream_.bufferSize * formatBytes( format );
            for ( int i=0; i<channels; i++ )
                bufs[i] = (void *) (buffer + (i * offset));
            result = snd_pcm_writen( handle[0], bufs, samples );
        }
        if ( result <= 0 ) {
            // Either an error or underrun occurred.
            if ( result == -EPIPE ) {
                snd_pcm_state_t state = snd_pcm_state( handle[0] );
                if ( state == SND_PCM_STATE_XRUN ) {
                    apiInfo->xrun[0] = true;
                    result = snd_pcm_prepare( handle[0] );
                    if ( result < 0 ) {
                        errorStream_ << "RtApiAlsa::callbackEvent: error preparing device after underrun, " << snd_strerror( result ) << ".";
                        errorText_ = errorStream_.str();
                    }
                    else
                        errorText_ =  "RtApiAlsa::callbackEvent: audio write error, underrun.";
                }
                else {
                    errorStream_ << "RtApiAlsa::callbackEvent: error, current state is " << snd_pcm_state_name( state ) << ", " << snd_strerror( result ) << ".";
                    errorText_ = errorStream_.str();
                }
            }else if (result == -EAGAIN){
                continue;
            }
            else {
                errorStream_ << "RtApiAlsa::callbackEvent: audio write error, " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
                stream_.state = STREAM_ERROR;
            }
            error( RTAUDIO_WARNING );
            return false;
        }
        samplesPlayed += result;
        if (result != samples){
            result = result;
        }
    }

    // Check stream latency
    result = snd_pcm_delay( handle[0], &frames );
    if ( result == 0 && frames > 0 ) stream_.latency[0] = frames;
    return true;
}

void RtApiAlsa :: closeStream()
{
    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiAlsa::closeStream(): no open stream to close!";
        error( RTAUDIO_WARNING );
        return;
    }

    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    stream_.callbackInfo.isRunning = false;
    MUTEX_LOCK( &stream_.mutex );
    if ( stream_.state == STREAM_STOPPED ) {
        apiInfo->runnable = true;
        pthread_cond_signal( &apiInfo->runnable_cv );
    }
    MUTEX_UNLOCK( &stream_.mutex );
    pthread_join( stream_.callbackInfo.thread, NULL );

    if ( stream_.state == STREAM_RUNNING ) {
        stream_.state = STREAM_STOPPED;
        if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX )
            snd_pcm_drop( apiInfo->handles[0] );
        if ( stream_.mode == INPUT || stream_.mode == DUPLEX )
            snd_pcm_drop( apiInfo->handles[1] );
    }

    if ( apiInfo ) {
        pthread_cond_destroy( &apiInfo->runnable_cv );
        if ( apiInfo->handles[0] ) snd_pcm_close( apiInfo->handles[0] );
        if ( apiInfo->handles[1] ) snd_pcm_close( apiInfo->handles[1] );
        delete apiInfo;
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

    clearStreamInfo();
}

RtAudioErrorType RtApiAlsa :: startStream()
{
    // This method calls snd_pcm_prepare if the device isn't already in that state.

    if ( stream_.state != STREAM_STOPPED ) {
        if ( stream_.state == STREAM_RUNNING )
            errorText_ = "RtApiAlsa::startStream(): the stream is already running!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiAlsa::startStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    MUTEX_LOCK( &stream_.mutex );

    /*
  #if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
  #endif
  */

    int result = 0;
    snd_pcm_state_t state;
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    snd_pcm_t **handle = (snd_pcm_t **) apiInfo->handles;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
        state = snd_pcm_state( handle[0] );
        if ( state != SND_PCM_STATE_PREPARED ) {
            result = snd_pcm_prepare( handle[0] );
            if ( result < 0 ) {
                errorStream_ << "RtApiAlsa::startStream: error preparing output pcm device, " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
                goto unlock;
            }
        }
    }

    if ( ( stream_.mode == INPUT || stream_.mode == DUPLEX ) && !apiInfo->synchronized ) {
        result = snd_pcm_drop(handle[1]); // fix to remove stale data received since device has been open
        state = snd_pcm_state( handle[1] );
        if ( state != SND_PCM_STATE_PREPARED ) {
            result = snd_pcm_prepare( handle[1] );
            if ( result < 0 ) {
                errorStream_ << "RtApiAlsa::startStream: error preparing input pcm device, " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
                goto unlock;
            }
        }
    }

    stream_.state = STREAM_RUNNING;

unlock:
    apiInfo->runnable = true;
    pthread_cond_signal( &apiInfo->runnable_cv );
    MUTEX_UNLOCK( &stream_.mutex );

    if ( result < 0 ) return error( RTAUDIO_SYSTEM_ERROR );
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiAlsa :: stopStream()
{
    if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiAlsa::stopStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiAlsa::stopStream(): the stream is closed!";
        return error( RTAUDIO_WARNING );
    }

    stream_.state = STREAM_STOPPED;
    MUTEX_LOCK( &stream_.mutex );

    int result = 0;
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    snd_pcm_t **handle = (snd_pcm_t **) apiInfo->handles;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
        if ( apiInfo->synchronized )
            result = snd_pcm_drop( handle[0] );
        else
            result = snd_pcm_drain( handle[0] );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::stopStream: error draining output pcm device, " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    if ( ( stream_.mode == INPUT || stream_.mode == DUPLEX ) && !apiInfo->synchronized ) {
        result = snd_pcm_drop( handle[1] );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::stopStream: error stopping input pcm device, " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

unlock:
    apiInfo->runnable = false; // fixes high CPU usage when stopped
    MUTEX_UNLOCK( &stream_.mutex );

    if ( result < 0 ) return error( RTAUDIO_SYSTEM_ERROR );
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiAlsa :: abortStream()
{
    if ( stream_.state != STREAM_RUNNING) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiAlsa::abortStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiAlsa::abortStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    stream_.state = STREAM_STOPPED;
    MUTEX_LOCK( &stream_.mutex );

    int result = 0;
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    snd_pcm_t **handle = (snd_pcm_t **) apiInfo->handles;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
        result = snd_pcm_drop( handle[0] );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::abortStream: error aborting output pcm device, " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    if ( ( stream_.mode == INPUT || stream_.mode == DUPLEX ) && !apiInfo->synchronized ) {
        result = snd_pcm_drop( handle[1] );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::abortStream: error aborting input pcm device, " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

unlock:
    apiInfo->runnable = false; // fixes high CPU usage when stopped
    MUTEX_UNLOCK( &stream_.mutex );

    if ( result < 0 ) return error( RTAUDIO_SYSTEM_ERROR );
    return RTAUDIO_NO_ERROR;
}

void RtApiAlsa :: callbackEvent()
{
    AlsaHandle *apiInfo = (AlsaHandle *) stream_.apiHandle;
    if ( stream_.state == STREAM_STOPPED ) {
        MUTEX_LOCK( &stream_.mutex );
        while ( !apiInfo->runnable )
            pthread_cond_wait( &apiInfo->runnable_cv, &stream_.mutex );

        if ( stream_.state != STREAM_RUNNING ) {
            MUTEX_UNLOCK( &stream_.mutex );
            return;
        }
        MUTEX_UNLOCK( &stream_.mutex );
    }

    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiAlsa::callbackEvent(): the stream is closed ... this shouldn't happen!";
        error( RTAUDIO_WARNING );
        return;
    }

    int doStopStream = 0;
    int inputSamples = 0;
    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;
    if ( stream_.mode != INPUT && apiInfo->xrun[0] == true ) {
        status |= RTAUDIO_OUTPUT_UNDERFLOW;
        apiInfo->xrun[0] = false;
    }
    if ( stream_.mode != OUTPUT && apiInfo->xrun[1] == true ) {
        status |= RTAUDIO_INPUT_OVERFLOW;
        apiInfo->xrun[1] = false;
    }

    MUTEX_LOCK( &stream_.mutex );

    // The state might change while waiting on a mutex.
    if ( stream_.state == STREAM_STOPPED ) goto unlock;

    int result;
    char *buffer;
    int channels;
    snd_pcm_t **handle;
    snd_pcm_sframes_t frames;
    RtAudioFormat format;
    handle = (snd_pcm_t **) apiInfo->handles;

    if ( stream_.mode == INPUT || stream_.mode == DUPLEX ) {
        inputSamples = processInput();
    }

    callback( stream_.userBuffer[0], inputSamples > 0 ? stream_.userBuffer[1] : nullptr,
            inputSamples > 0 ? inputSamples : stream_.bufferSize, streamTime, status, stream_.callbackInfo.userData );

    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
       processOutput(inputSamples > 0 ? inputSamples : stream_.bufferSize);
    }

unlock:
    MUTEX_UNLOCK( &stream_.mutex );

    RtApi::tickStreamTime();
}

void RtApiAlsa::listDevices()
{
    deviceList_.clear();

    // See list of required functionality in RtApi::probeDevices().
    int result, device, card;
    char name[128];
    snd_ctl_t *handle = 0;
    snd_ctl_card_info_t *ctlinfo;
    snd_pcm_info_t *pcminfo;
    snd_ctl_card_info_alloca(&ctlinfo);
    snd_pcm_info_alloca(&pcminfo);
    snd_pcm_stream_t stream;
    std::string defaultDeviceName;

    // Count cards and devices and get ascii identifiers.
    card = -1;
    snd_card_next( &card );
    while ( card >= 0 ) {
        sprintf( name, "hw:%d", card );
        result = snd_ctl_open( &handle, name, 0 );
        if ( result < 0 ) {
            handle = 0;
            errorStream_ << "RtApiAlsa::probeDevices: control open, card = " << card << ", " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
            goto nextcard;
        }
        result = snd_ctl_card_info( handle, ctlinfo );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::probeDevices: control info, card = " << card << ", " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
            goto nextcard;
        }
        device = -1;
        while( 1 ) {
            result = snd_ctl_pcm_next_device( handle, &device );
            if ( result < 0 ) {
                errorStream_ << "RtApiAlsa::probeDevices: control next device, card = " << card << ", " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
                error( RTAUDIO_WARNING );
                break;
            }
            if ( device < 0 )
                break;

            snd_pcm_info_set_device( pcminfo, device );
            snd_pcm_info_set_subdevice( pcminfo, 0 );

            bool supportsInput = false;
            bool supportsOutput = false;

            stream = SND_PCM_STREAM_PLAYBACK;
            snd_pcm_info_set_stream( pcminfo, stream );
            result = snd_ctl_pcm_info( handle, pcminfo );
            if (result==0){
                supportsOutput = true;
            }else if (result != -ENOENT){
                continue;
            }

            stream = SND_PCM_STREAM_CAPTURE;
            snd_pcm_info_set_stream( pcminfo, stream );
            result = snd_ctl_pcm_info( handle, pcminfo );
            if (result==0){
                supportsInput = true;
            }else if (result != -ENOENT){
                continue;
            }

            if (!supportsInput && !supportsOutput){
                errorStream_ << "RtApiAlsa::probeDevices: control pcm info, card = " << card << ", device = " << device << ", " << snd_strerror( result ) << ".";
                errorText_ = errorStream_.str();
                error( RTAUDIO_WARNING );
                continue;
            }


            sprintf( name, "hw:%s,%d", snd_ctl_card_info_get_id(ctlinfo), device );
            std::string id(name);
            sprintf( name, "%s (%s)", snd_ctl_card_info_get_name(ctlinfo), snd_pcm_info_get_id(pcminfo) );
            std::string prettyName(name);

            RtAudio::DeviceInfo info;
            info.name = prettyName;
            info.busID = id;
            info.ID = currentDeviceId_++;  // arbitrary internal device ID
            info.supportsInput = supportsInput;
            info.supportsOutput = supportsOutput;
            deviceList_.push_back( info );
        }
nextcard:
        if ( handle )
            snd_ctl_close( handle );
        snd_card_next( &card );
    }
}

bool RtApiAlsa::probeSingleDeviceInfo(RtAudio::DeviceInfo & info)
{
    int result, openMode = SND_PCM_ASYNC;
    snd_pcm_stream_t stream;
    snd_pcm_t *phandle;
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca( &params );

    // First try for playback
    stream = SND_PCM_STREAM_PLAYBACK;
    result = snd_pcm_open( &phandle, info.busID.c_str(), stream, openMode | SND_PCM_NONBLOCK );
    if ( result < 0 ) {
        if ( result != -2 && result != -16) { // device doesn't support playback
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open (playback) error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
        }
        goto captureProbe;
    }

    // The device is open ... fill the parameter structure.
    result = snd_pcm_hw_params_any( phandle, params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        goto captureProbe;
    }

    // Get output channel information.
    unsigned int value;
    result = snd_pcm_hw_params_get_channels_max( params, &value );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting device (" << info.busID << ") output channels, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        goto captureProbe;
    }
    info.outputChannels = value;
    snd_pcm_close( phandle );

captureProbe:
    stream = SND_PCM_STREAM_CAPTURE;
    result = snd_pcm_open( &phandle, info.busID.c_str(), stream, openMode | SND_PCM_NONBLOCK);
    if ( result < 0 && result ) {
        if ( result != -2 && result != -16 ) { // device busy or doesn't support capture
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open (capture) error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
        }
        goto probeParameters;
    }

    // The device is open ... fill the parameter structure.
    result = snd_pcm_hw_params_any( phandle, params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        if ( info.outputChannels == 0 ) return false;
        goto probeParameters;
    }

    result = snd_pcm_hw_params_get_channels_max( params, &value );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting device (" << info.busID << ") input channels, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        if ( info.outputChannels == 0 ) return false;
        goto probeParameters;
    }
    info.inputChannels = value;
    snd_pcm_close( phandle );

    // If device opens for both playback and capture, we determine the channels.
    if ( info.outputChannels > 0 && info.inputChannels > 0 )
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;

probeParameters:
    // At this point, we just need to figure out the supported data
    // formats and sample rates.  We'll proceed by opening the device in
    // the direction with the maximum number of channels, or playback if
    // they are equal.  This might limit our sample rate options, but so
    // be it.

    if ( info.outputChannels >= info.inputChannels )
        stream = SND_PCM_STREAM_PLAYBACK;
    else
        stream = SND_PCM_STREAM_CAPTURE;

    result = snd_pcm_open( &phandle, info.busID.c_str(), stream, openMode | SND_PCM_NONBLOCK);
    if ( result < 0 ) {
        errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // The device is open ... fill the parameter structure.
    result = snd_pcm_hw_params_any( phandle, params );
    if ( result < 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << info.busID << "), " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Test our discrete set of sample rate values.
    info.sampleRates.clear();
    for ( unsigned int i=0; i<MAX_SAMPLE_RATES; i++ ) {
        if ( snd_pcm_hw_params_test_rate( phandle, params, SAMPLE_RATES[i], 0 ) == 0 ) {
            info.sampleRates.push_back( SAMPLE_RATES[i] );

            if ( !info.preferredSampleRate || ( SAMPLE_RATES[i] <= 48000 && SAMPLE_RATES[i] > info.preferredSampleRate ) )
                info.preferredSampleRate = SAMPLE_RATES[i];
        }
    }
    if ( info.sampleRates.size() == 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: no supported sample rates found for device (" << info.busID << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Probe the supported data formats ... we don't care about endian-ness just yet
    snd_pcm_format_t format;
    info.nativeFormats = 0;
    format = SND_PCM_FORMAT_S8;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_SINT8;
    format = SND_PCM_FORMAT_S16;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_SINT16;
    format = SND_PCM_FORMAT_S24;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_SINT24;
    format = SND_PCM_FORMAT_S32;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_SINT32;
    format = SND_PCM_FORMAT_FLOAT;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_FLOAT32;
    format = SND_PCM_FORMAT_FLOAT64;
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_FLOAT64;
    format = SND_PCM_FORMAT_S24_3LE;
    snd_pcm_hw_params_get_format(params, &format);
    if ( snd_pcm_hw_params_test_format( phandle, params, format ) == 0 )
        info.nativeFormats |= RTAUDIO_SINT24;

    // Check that we have at least one supported format
    if ( info.nativeFormats == 0 ) {
        snd_pcm_close( phandle );
        errorStream_ << "RtApiAlsa::probeDeviceInfo: pcm device (" << info.busID << ") data format not supported by RtAudio.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Close the device and return
    snd_pcm_close( phandle );
    return true;
}

