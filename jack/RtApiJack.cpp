#include "RtApiJack.h"
#include <unistd.h>
#include <cstdio>
#include <cstring>

namespace{
// A structure to hold various information related to the Jack API
// implementation.
struct JackHandle {
    jack_client_t *client;
    jack_port_t **ports[2];
    std::string deviceName[2];
    bool xrun[2];
    pthread_cond_t condition;
    int drainCounter;       // Tracks callback counts when draining
    bool internalDrain;     // Indicates if stop is initiated from callback or not.

    JackHandle()
        :client(0), drainCounter(0), internalDrain(false) { ports[0] = 0; ports[1] = 0; xrun[0] = false; xrun[1] = false; }
};

std::string escapeJackPortRegex(std::string &str)
{
    const std::string need_escaping = "()[]{}*+?$^.|\\";
    std::string escaped_string;
    for (auto c : str)
    {
        if (need_escaping.find(c) !=  std::string::npos)
            escaped_string.push_back('\\');

        escaped_string.push_back(c);
    }
    return escaped_string;
}

#if !defined(__RTAUDIO_DEBUG__)
static void jackSilentError( const char * ) {};
#endif


static int jackCallbackHandler( jack_nframes_t nframes, void *infoPointer )
{
    CallbackInfo *info = (CallbackInfo *) infoPointer;

    RtApiJack *object = (RtApiJack *) info->object;
    if ( object->callbackEvent( (unsigned long) nframes ) == false ) return 1;

    return 0;
}

// This function will be called by a spawned thread when the Jack
// server signals that it is shutting down.  It is necessary to handle
// it this way because the jackShutdown() function must return before
// the jack_deactivate() function (in closeStream()) will return.
static void *jackCloseStream( void *ptr )
{
    CallbackInfo *info = (CallbackInfo *) ptr;
    RtApiJack *object = (RtApiJack *) info->object;

    info->deviceDisconnected = true;
    object->closeStream();
    pthread_exit( NULL );
}

/*
// Could be used to catch client connections but requires open client.
static void jackClientChange( const char *name, int registered, void *infoPointer )
{
  std::cout << "in jackClientChange, name = " << name << ", registered = " << registered << std::endl;
}
*/

static void jackShutdown( void *infoPointer )
{
    CallbackInfo *info = (CallbackInfo *) infoPointer;
    RtApiJack *object = (RtApiJack *) info->object;

    // Check current stream state.  If stopped, then we'll assume this
    // was called as a result of a call to RtApiJack::stopStream (the
    // deactivation of a client handle causes this function to be called).
    // If not, we'll assume the Jack server is shutting down or some
    // other problem occurred and we should close the stream.
    if ( object->isStreamRunning() == false ) return;

    ThreadHandle threadId;
    pthread_create( &threadId, NULL, jackCloseStream, info );
}

static int jackXrun( void *infoPointer )
{
    JackHandle *handle = *((JackHandle **) infoPointer);

    if ( handle->ports[0] ) handle->xrun[0] = true;
    if ( handle->ports[1] ) handle->xrun[1] = true;

    return 0;
}

// This function will be called by a spawned thread when the user
// callback function signals that the stream should be stopped or
// aborted.  It is necessary to handle it this way because the
// callbackEvent() function must return before the jack_deactivate()
// function will return.
static void *jackStopStream( void *ptr )
{
    CallbackInfo *info = (CallbackInfo *) ptr;
    RtApiJack *object = (RtApiJack *) info->object;

    object->stopStream();
    pthread_exit( NULL );
}
}


RtApiJack :: RtApiJack()
    :shouldAutoconnect_(true) {
    // Nothing to do here.
#if !defined(__RTAUDIO_DEBUG__)
    // Turn off Jack's internal error reporting.
    jack_set_error_function( &jackSilentError );
#endif
}

RtApiJack :: ~RtApiJack()
{
    if ( stream_.state != STREAM_CLOSED ) closeStream();
}

void RtApiJack :: probeDevices( void )
{
    // See list of required functionality in RtApi::probeDevices().

    // See if we can become a jack client.
    jack_options_t options = (jack_options_t) ( JackNoStartServer ); //JackNullOption;
    jack_status_t *status = NULL;
    jack_client_t *client = jack_client_open( "RtApiJackProbe", options, status );
    if ( client == 0 ) {
        deviceList_.clear(); // in case the server is shutdown after a previous successful probe
        errorText_ = "RtApiJack::probeDevices: Jack server not found or connection error!";
        //error( RTAUDIO_SYSTEM_ERROR );
        error( RTAUDIO_WARNING );
        return;
    }

    const char **ports;
    std::string port, previousPort;
    unsigned int nChannels = 0, nDevices = 0;
    std::vector<std::string> portNames;
    ports = jack_get_ports( client, NULL, JACK_DEFAULT_AUDIO_TYPE, 0 );
    if ( ports ) {
        // Parse the port names up to the first colon (:).
        size_t iColon = 0;
        do {
            port = (char *) ports[ nChannels ];
            iColon = port.find(":");
            if ( iColon != std::string::npos ) {
                port = port.substr( 0, iColon );
                if ( port != previousPort ) {
                    portNames.push_back( port );
                    nDevices++;
                    previousPort = port;
                }
            }
        } while ( ports[++nChannels] );
        free( ports );
    }

    // Fill or update the deviceList_.
    unsigned int m, n;
    for ( n=0; n<nDevices; n++ ) {
        for ( m=0; m<deviceList_.size(); m++ ) {
            if ( deviceList_[m].name == portNames[n] )
                break; // We already have this device.
        }
        if ( m == deviceList_.size() ) { // new device
            RtAudio::DeviceInfo info;
            info.name = portNames[n];
            if ( probeDeviceInfo( info, client ) == false ) continue; // ignore if probe fails
            info.ID = currentDeviceId_++;  // arbitrary internal device ID
            deviceList_.push_back( info );
            // A callback can be registered in Jack to be notified about client
            // (dis)connections. However, this can only be done with an open client,
            // so unless we want to keep a special client open all the time, this
            // would only report (dis)connections when a stream is open. I'm not
            // going to bother for the moment.
        }
    }

    // Remove any devices left in the list that are no longer available.
    for ( std::vector<RtAudio::DeviceInfo>::iterator it=deviceList_.begin(); it!=deviceList_.end(); ) {
        for ( m=0; m<portNames.size(); m++ ) {
            if ( (*it).name == portNames[m] ) {
                ++it;
                break;
            }
        }
        if ( m == portNames.size() ) // not found so remove it from our list
            it = deviceList_.erase( it );
    }

    jack_client_close( client );

    if ( nDevices == 0 ) {
        deviceList_.clear();
        return;
    }

    // Jack doesn't provide default devices so call the getDefault
    // functions, which will set the first available input and output
    // devices as the defaults.
    getDefaultInputDevice();
    getDefaultOutputDevice();
}

bool RtApiJack :: probeDeviceInfo( RtAudio::DeviceInfo& info, jack_client_t *client )
{
    // Get the current jack server sample rate.
    info.sampleRates.clear();

    info.preferredSampleRate = jack_get_sample_rate( client );
    info.sampleRates.push_back( info.preferredSampleRate );

    // Count the available ports containing the client name as device
    // channels.  Jack "input ports" equal RtAudio output channels.
    unsigned int nChannels = 0;
    const char **ports = jack_get_ports( client, escapeJackPortRegex(info.name).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput );
    if ( ports ) {
        while ( ports[ nChannels ] ) nChannels++;
        free( ports );
        info.outputChannels = nChannels;
    }

    // Jack "output ports" equal RtAudio input channels.
    nChannels = 0;
    ports = jack_get_ports( client, escapeJackPortRegex(info.name).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput );
    if ( ports ) {
        while ( ports[ nChannels ] ) nChannels++;
        free( ports );
        info.inputChannels = nChannels;
    }

    if ( info.outputChannels == 0 && info.inputChannels == 0 ) {
        jack_client_close(client);
        errorText_ = "RtApiJack::getDeviceInfo: error determining Jack input/output channels!";
        error( RTAUDIO_WARNING );
        return false;
    }

    // If device opens for both playback and capture, we determine the channels.
    if ( info.outputChannels > 0 && info.inputChannels > 0 )
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;

    // Jack always uses 32-bit floats.
    info.nativeFormats = RTAUDIO_FLOAT32;

    return true;
}

bool RtApiJack :: probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                                   unsigned int firstChannel, unsigned int sampleRate,
                                   RtAudioFormat format, unsigned int *bufferSize,
                                   RtAudio::StreamOptions *options )
{
    JackHandle *handle = (JackHandle *) stream_.apiHandle;

    // Look for jack server and try to become a client (only do once per stream).
    jack_client_t *client = 0;
    if ( mode == OUTPUT || ( mode == INPUT && stream_.mode != OUTPUT ) ) {
        jack_options_t jackoptions = (jack_options_t) ( JackNoStartServer ); //JackNullOption;
        jack_status_t *status = NULL;
        if ( options && !options->streamName.empty() )
            client = jack_client_open( options->streamName.c_str(), jackoptions, status );
        else
            client = jack_client_open( "RtApiJack", jackoptions, status );
        if ( client == 0 ) {
            errorText_ = "RtApiJack::probeDeviceOpen: Jack server not found or connection error!";
            error( RTAUDIO_WARNING );
            return FAILURE;
        }
    }
    else {
        // The handle must have been created on an earlier pass.
        client = handle->client;
    }

    std::string deviceName;
    for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
        if ( deviceList_[m].ID == deviceId ) {
            deviceName = deviceList_[m].name;
            break;
        }
    }

    if ( deviceName.empty() ) {
        errorText_ = "RtApiJack::probeDeviceOpen: device ID is invalid!";
        return FAILURE;
    }

    unsigned long flag = JackPortIsInput;
    if ( mode == INPUT ) flag = JackPortIsOutput;

    const char **ports;
    if ( ! (options && (options->flags & RTAUDIO_JACK_DONT_CONNECT)) ) {
        // Count the available ports containing the client name as device
        // channels.  Jack "input ports" equal RtAudio output channels.
        unsigned int nChannels = 0;
        ports = jack_get_ports( client, escapeJackPortRegex(deviceName).c_str(), JACK_DEFAULT_AUDIO_TYPE, flag );
        if ( ports ) {
            while ( ports[ nChannels ] ) nChannels++;
            free( ports );
        }
        // Compare the jack ports for specified client to the requested number of channels.
        if ( nChannels < (channels + firstChannel) ) {
            errorStream_ << "RtApiJack::probeDeviceOpen: requested number of channels (" << channels << ") + offset (" << firstChannel << ") not found for specified device (" << deviceName << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    }

    // Check the jack server sample rate.
    unsigned int jackRate = jack_get_sample_rate( client );
    if ( sampleRate != jackRate ) {
        jack_client_close( client );
        errorStream_ << "RtApiJack::probeDeviceOpen: the requested sample rate (" << sampleRate << ") is different than the JACK server rate (" << jackRate << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }
    stream_.sampleRate = jackRate;

    // Get the latency of the JACK port.
    ports = jack_get_ports( client, escapeJackPortRegex(deviceName).c_str(), JACK_DEFAULT_AUDIO_TYPE, flag );
    if ( ports[ firstChannel ] ) {
        // Added by Ge Wang
        jack_latency_callback_mode_t cbmode = (mode == INPUT ? JackCaptureLatency : JackPlaybackLatency);
        // the range (usually the min and max are equal)
        jack_latency_range_t latrange; latrange.min = latrange.max = 0;
        // get the latency range
        jack_port_get_latency_range( jack_port_by_name( client, ports[firstChannel] ), cbmode, &latrange );
        // be optimistic, use the min!
        stream_.latency[mode] = latrange.min;
        //stream_.latency[mode] = jack_port_get_latency( jack_port_by_name( client, ports[ firstChannel ] ) );
    }
    free( ports );

    // The jack server always uses 32-bit floating-point data.
    stream_.deviceFormat[mode] = RTAUDIO_FLOAT32;
    stream_.userFormat = format;

    if ( options && options->flags & RTAUDIO_NONINTERLEAVED ) stream_.userInterleaved = false;
    else stream_.userInterleaved = true;

    // Jack always uses non-interleaved buffers.
    stream_.deviceInterleaved[mode] = false;

    // Jack always provides host byte-ordered data.
    stream_.doByteSwap[mode] = false;

    // Get the buffer size.  The buffer size and number of buffers
    // (periods) is set when the jack server is started.
    stream_.bufferSize = (int) jack_get_buffer_size( client );
    *bufferSize = stream_.bufferSize;

    stream_.nDeviceChannels[mode] = channels;
    stream_.nUserChannels[mode] = channels;

    // Set flags for buffer conversion.
    stream_.doConvertBuffer[mode] = false;
    if ( stream_.userFormat != stream_.deviceFormat[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
         stream_.nUserChannels[mode] > 1 )
        stream_.doConvertBuffer[mode] = true;

    // Allocate our JackHandle structure for the stream.
    if ( handle == 0 ) {
        try {
            handle = new JackHandle;
        }
        catch ( std::bad_alloc& ) {
            errorText_ = "RtApiJack::probeDeviceOpen: error allocating JackHandle memory.";
            goto error;
        }

        if ( pthread_cond_init(&handle->condition, NULL) ) {
            errorText_ = "RtApiJack::probeDeviceOpen: error initializing pthread condition variable.";
            goto error;
        }
        stream_.apiHandle = (void *) handle;
        handle->client = client;
    }
    handle->deviceName[mode] = deviceName;

    // Allocate necessary internal buffers.
    unsigned long bufferBytes;
    bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes( stream_.userFormat );
    stream_.userBuffer[mode] = (char *) calloc( bufferBytes, 1 );
    if ( stream_.userBuffer[mode] == NULL ) {
        errorText_ = "RtApiJack::probeDeviceOpen: error allocating user buffer memory.";
        goto error;
    }

    if ( stream_.doConvertBuffer[mode] ) {

        bool makeBuffer = true;
        if ( mode == OUTPUT )
            bufferBytes = stream_.nDeviceChannels[0] * formatBytes( stream_.deviceFormat[0] );
        else { // mode == INPUT
            bufferBytes = stream_.nDeviceChannels[1] * formatBytes( stream_.deviceFormat[1] );
            if ( stream_.mode == OUTPUT && stream_.deviceBuffer ) {
                unsigned long bytesOut = stream_.nDeviceChannels[0] * formatBytes(stream_.deviceFormat[0]);
                if ( bufferBytes < bytesOut ) makeBuffer = false;
            }
        }

        if ( makeBuffer ) {
            bufferBytes *= *bufferSize;
            if ( stream_.deviceBuffer ) free( stream_.deviceBuffer );
            stream_.deviceBuffer = (char *) calloc( bufferBytes, 1 );
            if ( stream_.deviceBuffer == NULL ) {
                errorText_ = "RtApiJack::probeDeviceOpen: error allocating device buffer memory.";
                goto error;
            }
        }
    }

    // Allocate memory for the Jack ports (channels) identifiers.
    handle->ports[mode] = (jack_port_t **) malloc ( sizeof (jack_port_t *) * channels );
    if ( handle->ports[mode] == NULL )  {
        errorText_ = "RtApiJack::probeDeviceOpen: error allocating port memory.";
        goto error;
    }

    stream_.channelOffset[mode] = firstChannel;
    stream_.state = STREAM_STOPPED;
    stream_.callbackInfo.object = (void *) this;

    if ( stream_.mode == OUTPUT && mode == INPUT )
        // We had already set up the stream for output.
        stream_.mode = DUPLEX;
    else {
        stream_.mode = mode;
        jack_set_process_callback( handle->client, jackCallbackHandler, (void *) &stream_.callbackInfo );
        jack_set_xrun_callback( handle->client, jackXrun, (void *) &stream_.apiHandle );
        jack_on_shutdown( handle->client, jackShutdown, (void *) &stream_.callbackInfo );
        //jack_set_client_registration_callback( handle->client, jackClientChange, (void *) &stream_.callbackInfo );
    }

    // Register our ports.
    char label[64];
    if ( mode == OUTPUT ) {
        for ( unsigned int i=0; i<stream_.nUserChannels[0]; i++ ) {
            snprintf( label, 64, "outport %d", i );
            handle->ports[0][i] = jack_port_register( handle->client, (const char *)label,
                                                      JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
        }
    }
    else {
        for ( unsigned int i=0; i<stream_.nUserChannels[1]; i++ ) {
            snprintf( label, 64, "inport %d", i );
            handle->ports[1][i] = jack_port_register( handle->client, (const char *)label,
                                                      JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0 );
        }
    }

    // Setup the buffer conversion information structure.  We don't use
    // buffers to do channel offsets, so we override that parameter
    // here.
    if ( stream_.doConvertBuffer[mode] ) setConvertInfo( mode, 0 );

    if ( options && options->flags & RTAUDIO_JACK_DONT_CONNECT ) shouldAutoconnect_ = false;

    return SUCCESS;

error:
    if ( handle ) {
        pthread_cond_destroy( &handle->condition );
        jack_client_close( handle->client );

        if ( handle->ports[0] ) free( handle->ports[0] );
        if ( handle->ports[1] ) free( handle->ports[1] );

        delete handle;
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

    return FAILURE;
}

void RtApiJack :: closeStream( void )
{
    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiJack::closeStream(): no open stream to close!";
        error( RTAUDIO_WARNING );
        return;
    }

    JackHandle *handle = (JackHandle *) stream_.apiHandle;
    if ( handle ) {
        if ( stream_.state == STREAM_RUNNING )
            jack_deactivate( handle->client );

        if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
            for ( unsigned int i=0; i<stream_.nUserChannels[0]; i++ )
                jack_port_unregister( handle->client, handle->ports[0][i] );
        }
        if ( stream_.mode == INPUT || stream_.mode == DUPLEX ) {
            for ( unsigned int i=0; i<stream_.nUserChannels[1]; i++ )
                jack_port_unregister( handle->client, handle->ports[1][i] );
        }
        jack_client_close( handle->client );

        if ( handle->ports[0] ) free( handle->ports[0] );
        if ( handle->ports[1] ) free( handle->ports[1] );
        pthread_cond_destroy( &handle->condition );
        delete handle;
        stream_.apiHandle = 0;
    }

    CallbackInfo *info = (CallbackInfo *) &stream_.callbackInfo;
    if ( info->deviceDisconnected ) {
        errorText_ = "RtApiJack: the Jack server is shutting down this client ... stream stopped and closed!";
        error( RTAUDIO_DEVICE_DISCONNECT );
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

RtAudioErrorType RtApiJack :: startStream( void )
{
    if ( stream_.state != STREAM_STOPPED ) {
        if ( stream_.state == STREAM_RUNNING )
            errorText_ = "RtApiJack::startStream(): the stream is already running!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiJack::startStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    /*
  #if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
  #endif
  */

    JackHandle *handle = (JackHandle *) stream_.apiHandle;
    int result = jack_activate( handle->client );
    if ( result ) {
        errorText_ = "RtApiJack::startStream(): unable to activate JACK client!";
        goto unlock;
    }

    const char **ports;

    // Get the list of available ports.
    if ( shouldAutoconnect_ && (stream_.mode == OUTPUT || stream_.mode == DUPLEX) ) {
        ports = jack_get_ports( handle->client, escapeJackPortRegex(handle->deviceName[0]).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        if ( ports == NULL) {
            errorText_ = "RtApiJack::startStream(): error determining available JACK input ports!";
            goto unlock;
        }

        // Now make the port connections.  Since RtAudio wasn't designed to
        // allow the user to select particular channels of a device, we'll
        // just open the first "nChannels" ports with offset.
        for ( unsigned int i=0; i<stream_.nUserChannels[0]; i++ ) {
            result = 1;
            if ( ports[ stream_.channelOffset[0] + i ] )
                result = jack_connect( handle->client, jack_port_name( handle->ports[0][i] ), ports[ stream_.channelOffset[0] + i ] );
            if ( result ) {
                free( ports );
                errorText_ = "RtApiJack::startStream(): error connecting output ports!";
                goto unlock;
            }
        }
        free(ports);
    }

    if ( shouldAutoconnect_ && (stream_.mode == INPUT || stream_.mode == DUPLEX) ) {
        ports = jack_get_ports( handle->client, escapeJackPortRegex(handle->deviceName[1]).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput );
        if ( ports == NULL) {
            errorText_ = "RtApiJack::startStream(): error determining available JACK output ports!";
            goto unlock;
        }

        // Now make the port connections.  See note above.
        for ( unsigned int i=0; i<stream_.nUserChannels[1]; i++ ) {
            result = 1;
            if ( ports[ stream_.channelOffset[1] + i ] )
                result = jack_connect( handle->client, ports[ stream_.channelOffset[1] + i ], jack_port_name( handle->ports[1][i] ) );
            if ( result ) {
                free( ports );
                errorText_ = "RtApiJack::startStream(): error connecting input ports!";
                goto unlock;
            }
        }
        free(ports);
    }

    handle->drainCounter = 0;
    handle->internalDrain = false;
    stream_.state = STREAM_RUNNING;

unlock:
    if ( result == 0 ) return RTAUDIO_NO_ERROR;
    return error( RTAUDIO_SYSTEM_ERROR );
}

RtAudioErrorType  RtApiJack :: stopStream( void )
{
    if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiJack::stopStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiJack::stopStream(): the stream is closed!";
        return error( RTAUDIO_WARNING );
    }

    JackHandle *handle = (JackHandle *) stream_.apiHandle;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

        if ( handle->drainCounter == 0 ) {
            handle->drainCounter = 2;
            pthread_cond_wait( &handle->condition, &stream_.mutex ); // block until signaled
        }
    }

    jack_deactivate( handle->client );
    stream_.state = STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiJack :: abortStream( void )
{
    if ( stream_.state != STREAM_RUNNING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiJack::abortStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiJack::abortStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    JackHandle *handle = (JackHandle *) stream_.apiHandle;
    handle->drainCounter = 2;

    return stopStream();
}

bool RtApiJack :: callbackEvent( unsigned long nframes )
{
    if ( stream_.state == STREAM_STOPPED || stream_.state == STREAM_STOPPING ) return SUCCESS;
    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiJack::callbackEvent(): the stream is closed ... this shouldn't happen!";
        error( RTAUDIO_WARNING );
        return FAILURE;
    }
    if ( stream_.bufferSize != nframes ) {
        errorText_ = "RtApiJack::callbackEvent(): the JACK buffer size has changed ... cannot process!";
        error( RTAUDIO_WARNING );
        return FAILURE;
    }

    CallbackInfo *info = (CallbackInfo *) &stream_.callbackInfo;
    JackHandle *handle = (JackHandle *) stream_.apiHandle;

    // Check if we were draining the stream and signal is finished.
    if ( handle->drainCounter > 3 ) {
        ThreadHandle threadId;

        stream_.state = STREAM_STOPPING;
        if ( handle->internalDrain == true )
            pthread_create( &threadId, NULL, jackStopStream, info );
        else // external call to stopStream()
            pthread_cond_signal( &handle->condition );
        return SUCCESS;
    }

    // Invoke user callback first, to get fresh output data.
    if ( handle->drainCounter == 0 ) {
        RtAudioCallback callback = (RtAudioCallback) info->callback;
        double streamTime = getStreamTime();
        RtAudioStreamStatus status = 0;
        if ( stream_.mode != INPUT && handle->xrun[0] == true ) {
            status |= RTAUDIO_OUTPUT_UNDERFLOW;
            handle->xrun[0] = false;
        }
        if ( stream_.mode != OUTPUT && handle->xrun[1] == true ) {
            status |= RTAUDIO_INPUT_OVERFLOW;
            handle->xrun[1] = false;
        }
        int cbReturnValue = callback( stream_.userBuffer[0], stream_.userBuffer[1],
                stream_.bufferSize, streamTime, status, info->userData );
        if ( cbReturnValue == 2 ) {
            stream_.state = STREAM_STOPPING;
            handle->drainCounter = 2;
            ThreadHandle id;
            pthread_create( &id, NULL, jackStopStream, info );
            return SUCCESS;
        }
        else if ( cbReturnValue == 1 ) {
            handle->drainCounter = 1;
            handle->internalDrain = true;
        }
    }

    jack_default_audio_sample_t *jackbuffer;
    unsigned long bufferBytes = nframes * sizeof( jack_default_audio_sample_t );
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

        if ( handle->drainCounter > 1 ) { // write zeros to the output stream

            for ( unsigned int i=0; i<stream_.nDeviceChannels[0]; i++ ) {
                jackbuffer = (jack_default_audio_sample_t *) jack_port_get_buffer( handle->ports[0][i], (jack_nframes_t) nframes );
                memset( jackbuffer, 0, bufferBytes );
            }

        }
        else if ( stream_.doConvertBuffer[0] ) {

            convertBuffer( stream_.deviceBuffer, stream_.userBuffer[0], stream_.convertInfo[0] );

            for ( unsigned int i=0; i<stream_.nDeviceChannels[0]; i++ ) {
                jackbuffer = (jack_default_audio_sample_t *) jack_port_get_buffer( handle->ports[0][i], (jack_nframes_t) nframes );
                memcpy( jackbuffer, &stream_.deviceBuffer[i*bufferBytes], bufferBytes );
            }
        }
        else { // no buffer conversion
            for ( unsigned int i=0; i<stream_.nUserChannels[0]; i++ ) {
                jackbuffer = (jack_default_audio_sample_t *) jack_port_get_buffer( handle->ports[0][i], (jack_nframes_t) nframes );
                memcpy( jackbuffer, &stream_.userBuffer[0][i*bufferBytes], bufferBytes );
            }
        }
    }

    // Don't bother draining input
    if ( handle->drainCounter ) {
        handle->drainCounter++;
        goto unlock;
    }

    if ( stream_.mode == INPUT || stream_.mode == DUPLEX ) {

        if ( stream_.doConvertBuffer[1] ) {
            for ( unsigned int i=0; i<stream_.nDeviceChannels[1]; i++ ) {
                jackbuffer = (jack_default_audio_sample_t *) jack_port_get_buffer( handle->ports[1][i], (jack_nframes_t) nframes );
                memcpy( &stream_.deviceBuffer[i*bufferBytes], jackbuffer, bufferBytes );
            }
            convertBuffer( stream_.userBuffer[1], stream_.deviceBuffer, stream_.convertInfo[1] );
        }
        else { // no buffer conversion
            for ( unsigned int i=0; i<stream_.nUserChannels[1]; i++ ) {
                jackbuffer = (jack_default_audio_sample_t *) jack_port_get_buffer( handle->ports[1][i], (jack_nframes_t) nframes );
                memcpy( &stream_.userBuffer[1][i*bufferBytes], jackbuffer, bufferBytes );
            }
        }
    }

unlock:
    RtApi::tickStreamTime();
    return SUCCESS;
}
