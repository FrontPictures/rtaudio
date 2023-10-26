#include "RtApiCore.h"

#if defined( MAC_OS_VERSION_12_0 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0 )
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMain
#else
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMaster // deprecated with macOS 12
#endif

namespace{
struct CoreHandle {
    AudioDeviceID id[2];    // device ids
#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
    AudioDeviceIOProcID procId[2];
#endif
    UInt32 iStream[2];      // device stream index (or first if using multiple)
    UInt32 nStreams[2];     // number of streams to use
    bool xrun[2];
    char *deviceBuffer;
    pthread_cond_t condition;
    int drainCounter;       // Tracks callback counts when draining
    bool internalDrain;     // Indicates if stop is initiated from callback or not.
    bool xrunListenerAdded[2];
    bool disconnectListenerAdded[2];

    CoreHandle()
        :deviceBuffer(0), drainCounter(0), internalDrain(false) { nStreams[0] = 1; nStreams[1] = 1; id[0] = 0; id[1] = 0; procId[0] = 0; procId[1] = 0; xrun[0] = false; xrun[1] = false; xrunListenerAdded[0] = false; xrunListenerAdded[1] = false; disconnectListenerAdded[0] = false; disconnectListenerAdded[1] = false; }
};

// If a device used in an open stream is disconnected, close the stream.
static OSStatus streamDisconnectListener( AudioObjectID /*id*/,
                                         UInt32 nAddresses,
                                         const AudioObjectPropertyAddress properties[],
                                         void* infoPointer )
{
    for ( UInt32 i=0; i<nAddresses; i++ ) {
        if ( properties[i].mSelector == kAudioDevicePropertyDeviceIsAlive ) {
            CallbackInfo *info = (CallbackInfo *) infoPointer;
            RtApiCore *object = (RtApiCore *) info->object;
            info->deviceDisconnected = true;
            object->closeStream();
            return kAudioHardwareUnspecifiedError;
        }
    }

    return kAudioHardwareNoError;
}

static OSStatus callbackHandler( AudioDeviceID inDevice,
                                const AudioTimeStamp* /*inNow*/,
                                const AudioBufferList* inInputData,
                                const AudioTimeStamp* /*inInputTime*/,
                                AudioBufferList* outOutputData,
                                const AudioTimeStamp* /*inOutputTime*/,
                                void* infoPointer )
{
    CallbackInfo *info = (CallbackInfo *) infoPointer;
    if(info == NULL || info->object == NULL)
        return kAudioHardwareUnspecifiedError;

    RtApiCore *object = (RtApiCore *) info->object;
    if ( object->callbackEvent( inDevice, inInputData, outOutputData ) == false )
        return kAudioHardwareUnspecifiedError;
    else
        return kAudioHardwareNoError;
}

static OSStatus xrunListener( AudioObjectID /*inDevice*/,
                             UInt32 nAddresses,
                             const AudioObjectPropertyAddress properties[],
                             void* handlePointer )
{
    CoreHandle *handle = (CoreHandle *) handlePointer;
    for ( UInt32 i=0; i<nAddresses; i++ ) {
        if ( properties[i].mSelector == kAudioDeviceProcessorOverload ) {
            if ( properties[i].mScope == kAudioDevicePropertyScopeInput )
                handle->xrun[1] = true;
            else
                handle->xrun[0] = true;
        }
    }

    return kAudioHardwareNoError;
}

// This function will be called by a spawned thread when the user
// callback function signals that the stream should be stopped or
// aborted.  It is better to handle it this way because the
// callbackEvent() function probably should return before the
// AudioDeviceStop() function is called.
static void *coreStopStream( void *ptr )
{
    CallbackInfo *info = (CallbackInfo *) ptr;
    RtApiCore *object = (RtApiCore *) info->object;

    object->stopStream();
    pthread_exit( NULL );
}

}

RtApiCore:: RtApiCore()
{
#if defined( AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER )
    // This is a largely undocumented but absolutely necessary
    // requirement starting with OS-X 10.6.  If not called, queries and
    // updates to various audio device properties are not handled
    // correctly.
    CFRunLoopRef theRunLoop = NULL;
    AudioObjectPropertyAddress property = { kAudioHardwarePropertyRunLoop,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT };
    OSStatus result = AudioObjectSetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, sizeof(CFRunLoopRef), &theRunLoop);
    if ( result != noErr ) {
        errorText_ = "RtApiCore::RtApiCore: error setting run loop property!";
        error( RTAUDIO_SYSTEM_ERROR );
    }
#endif
}

RtApiCore :: ~RtApiCore()
{
    // The subclass destructor gets called before the base class
    // destructor, so close an existing stream before deallocating
    // apiDeviceId memory.
    if ( stream_.state != STREAM_CLOSED ) closeStream();
}

void RtApiCore :: listDevices( void )
{
    // See list of required functionality in RtApi::probeDevices().

    // Find out how many audio devices there are.
    UInt32 dataSize;
    AudioObjectPropertyAddress property = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, KAUDIOOBJECTPROPERTYELEMENT };
    OSStatus result = AudioObjectGetPropertyDataSize( kAudioObjectSystemObject, &property, 0, NULL, &dataSize );
    if ( result != noErr ) {
        errorText_ = "RtApiCore::probeDevices: OS-X system error getting device info!";
        error( RTAUDIO_SYSTEM_ERROR );
        return;
    }

    unsigned int nDevices = dataSize / sizeof( AudioDeviceID );
    if ( nDevices == 0 ) {
        deviceList_.clear();
        deviceIds_.clear();
        return;
    }

    AudioDeviceID ids[ nDevices ];
    property.mSelector = kAudioHardwarePropertyDevices;
    result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, &dataSize, (void *) &ids );
    if ( result != noErr ) {
        errorText_ = "RtApiCore::probeDevices: OS-X system error getting device IDs.";
        error( RTAUDIO_SYSTEM_ERROR );
        return;
    }

    // Fill or update the deviceList_ and also save a corresponding list of Ids.
    for ( unsigned int n=0; n<nDevices; n++ ) {
        if ( std::find( deviceIds_.begin(), deviceIds_.end(), ids[n] ) != deviceIds_.end() ) {
            continue; // We already have this device.
        }
        else { // There is a new device to probe.
            RtAudio::DeviceInfo info;
            if ( probeDeviceInfo( ids[n], info ) == false ) continue; // ignore if probe fails
            deviceIds_.push_back( ids[n] );
            info.ID = currentDeviceId_++;  // arbitrary internal device ID
            deviceList_.push_back( info );
            // We could set a property listener here for each device to know
            // if it is removed. However, we cannot detect (AFAIK) when a new
            // device is plugged in. If we cannot detect BOTH cases, I'm not
            // going to bother with only the one.
        }
    }

    // Remove any devices left in the list that are no longer available.
    unsigned int m;
    for ( std::vector<AudioDeviceID>::iterator it=deviceIds_.begin(); it!=deviceIds_.end(); ) {
        for ( m=0; m<nDevices; m++ ) {
            if ( ids[m] == *it ) {
                ++it;
                break;
            }
        }
        if ( m == nDevices ) { // not found so remove it from our two lists
            it = deviceIds_.erase(it);
            deviceList_.erase( deviceList_.begin() + distance(deviceIds_.begin(), it ) );
        }
    }

    // Get default devices and set flags in deviceList_.
    AudioDeviceID defaultOutputId, defaultInputId;
    dataSize = sizeof( AudioDeviceID );
    property.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, &dataSize, &defaultOutputId );
    if ( result != noErr ) {
        errorText_ = "RtApiCore::probeDeviceInfo: OS-X system error getting default output device.";
        error( RTAUDIO_WARNING );
        defaultOutputId = 0;
    }

    property.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, &dataSize, &defaultInputId );
    if ( result != noErr ) {
        errorText_ = "RtApiCore::probeDeviceInfo: OS-X system error getting default input device.";
        error( RTAUDIO_WARNING );
        defaultInputId = 0;
    }

    for ( m=0; m<deviceList_.size(); m++ ) {
        if ( deviceIds_[m] == defaultOutputId )
            deviceList_[m].isDefaultOutput = true;
        else
            deviceList_[m].isDefaultOutput = false;
        if ( deviceIds_[m] == defaultInputId )
            deviceList_[m].isDefaultInput = true;
        else
            deviceList_[m].isDefaultInput = false;
    }
}

bool RtApiCore::probeSingleDeviceInfo(RtAudio::DeviceInfo &info)
{
    return true;
}

std::string GetSTDStringFromCString(CFStringRef str){
    long length = CFStringGetLength(str);
    char *mname = (char *)malloc(length * 3 + 1);
#if defined( UNICODE ) || defined( _UNICODE )
    CFStringGetCString(str, mname, length * 3 + 1, kCFStringEncodingUTF8);
#else
    CFStringGetCString(str, mname, length * 3 + 1, CFStringGetSystemEncoding());
#endif
    std::string res = mname;
    free(mname);
    return res;
}

bool RtApiCore :: probeDeviceInfo( AudioDeviceID id, RtAudio::DeviceInfo& info )
{
    // Get the device name.
    info.name.erase();
    CFStringRef cfname = nullptr;
    CFStringRef uid = nullptr;
    UInt32 dataSize = sizeof( CFStringRef );
    OSStatus result = 0;

    AudioObjectPropertyAddress property = { kAudioDevicePropertyDeviceUID,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT };
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &uid );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting device manufacturer.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }
    auto uid_str = GetSTDStringFromCString(uid);
    info.busID = uid_str;
    CFRelease( uid );

    dataSize = sizeof( CFStringRef );
    property = { kAudioObjectPropertyManufacturer,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT };
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &cfname );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting device manufacturer.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    auto mname_str = GetSTDStringFromCString(cfname);
    info.name.append(mname_str);
    info.name.append( ": " );
    CFRelease( cfname );

    property.mSelector = kAudioObjectPropertyName;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &cfname );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting device name.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    auto name_str = GetSTDStringFromCString(cfname);
    info.name.append( name_str );
    CFRelease( cfname );

    // Get the output stream "configuration".
    AudioBufferList	*bufferList = nil;
    property.mSelector = kAudioDevicePropertyStreamConfiguration;
    property.mScope = kAudioDevicePropertyScopeOutput;
    dataSize = 0;
    result = AudioObjectGetPropertyDataSize( id, &property, 0, NULL, &dataSize );
    if ( result != noErr || dataSize == 0 ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting output stream configuration info for device (" << info.name << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Allocate the AudioBufferList.
    bufferList = (AudioBufferList *) malloc( dataSize );
    if ( bufferList == NULL ) {
        errorText_ = "RtApiCore::probeDeviceInfo: memory error allocating output AudioBufferList.";
        error( RTAUDIO_WARNING );
        return false;
    }

    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, bufferList );
    if ( result != noErr || dataSize == 0 ) {
        free( bufferList );
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting output stream configuration for device (" << info.name << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Get output channel information.
    unsigned int i, nStreams = bufferList->mNumberBuffers;
    for ( i=0; i<nStreams; i++ )
        info.outputChannels += bufferList->mBuffers[i].mNumberChannels;
    free( bufferList );

    // Get the input stream "configuration".
    property.mScope = kAudioDevicePropertyScopeInput;
    result = AudioObjectGetPropertyDataSize( id, &property, 0, NULL, &dataSize );
    if ( result != noErr || dataSize == 0 ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting input stream configuration info for device (" << info.name << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Allocate the AudioBufferList.
    bufferList = (AudioBufferList *) malloc( dataSize );
    if ( bufferList == NULL ) {
        errorText_ = "RtApiCore::probeDeviceInfo: memory error allocating input AudioBufferList.";
        error( RTAUDIO_WARNING );
        return false;
    }

    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, bufferList );
    if (result != noErr || dataSize == 0) {
        free( bufferList );
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting input stream configuration for device (" << info.name << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Get input channel information.
    nStreams = bufferList->mNumberBuffers;
    for ( i=0; i<nStreams; i++ )
        info.inputChannels += bufferList->mBuffers[i].mNumberChannels;
    free( bufferList );

    // If device opens for both playback and capture, we determine the channels.
    if ( info.outputChannels > 0 && info.inputChannels > 0 )
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;

    if (info.outputChannels){
        info.supportsOutput = true;
    }
    if (info.inputChannels){
        info.supportsInput = true;
    }

    // Probe the device sample rates.
    bool isInput = false;
    if ( info.outputChannels == 0 ) isInput = true;

    // Determine the supported sample rates.
    property.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
    if ( isInput == false ) property.mScope = kAudioDevicePropertyScopeOutput;
    result = AudioObjectGetPropertyDataSize( id, &property, 0, NULL, &dataSize );
    if ( result != kAudioHardwareNoError || dataSize == 0 ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting sample rate info.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    UInt32 nRanges = dataSize / sizeof( AudioValueRange );
    AudioValueRange rangeList[ nRanges ];
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &rangeList );
    if ( result != kAudioHardwareNoError ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting sample rates.";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // The sample rate reporting mechanism is a bit of a mystery.  It
    // seems that it can either return individual rates or a range of
    // rates.  I assume that if the min / max range values are the same,
    // then that represents a single supported rate and if the min / max
    // range values are different, the device supports an arbitrary
    // range of values (though there might be multiple ranges, so we'll
    // use the most conservative range).
    Float64 minimumRate = 1.0, maximumRate = 10000000000.0;
    bool haveValueRange = false;
    info.sampleRates.clear();
    for ( UInt32 i=0; i<nRanges; i++ ) {
        if ( rangeList[i].mMinimum == rangeList[i].mMaximum ) {
            unsigned int tmpSr = (unsigned int) rangeList[i].mMinimum;
            info.sampleRates.push_back( tmpSr );

            if ( !info.preferredSampleRate || ( tmpSr <= 48000 && tmpSr > info.preferredSampleRate ) )
                info.preferredSampleRate = tmpSr;

        } else {
            haveValueRange = true;
            if ( rangeList[i].mMinimum > minimumRate ) minimumRate = rangeList[i].mMinimum;
            if ( rangeList[i].mMaximum < maximumRate ) maximumRate = rangeList[i].mMaximum;
        }
    }

    if ( haveValueRange ) {
        for ( unsigned int k=0; k<MAX_SAMPLE_RATES; k++ ) {
            if ( SAMPLE_RATES[k] >= (unsigned int) minimumRate && SAMPLE_RATES[k] <= (unsigned int) maximumRate ) {
                info.sampleRates.push_back( SAMPLE_RATES[k] );

                if ( !info.preferredSampleRate || ( SAMPLE_RATES[k] <= 48000 && SAMPLE_RATES[k] > info.preferredSampleRate ) )
                    info.preferredSampleRate = SAMPLE_RATES[k];
            }
        }
    }

    // Sort and remove any redundant values
    std::sort( info.sampleRates.begin(), info.sampleRates.end() );
    info.sampleRates.erase( unique( info.sampleRates.begin(), info.sampleRates.end() ), info.sampleRates.end() );

    if ( info.sampleRates.size() == 0 ) {
        errorStream_ << "RtApiCore::probeDeviceInfo: No supported sample rates found for device (" << info.name << ").";
        errorText_ = errorStream_.str();
        error( RTAUDIO_WARNING );
        return false;
    }

    // Probe the currently configured sample rate
    Float64 nominalRate;
    dataSize = sizeof( Float64 );
    property.mSelector = kAudioDevicePropertyNominalSampleRate;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &nominalRate );
    if ( result == noErr ) info.currentSampleRate = (unsigned int) nominalRate;

    // CoreAudio always uses 32-bit floating point data for PCM streams.
    // Thus, any other "physical" formats supported by the device are of
    // no interest to the client.
    info.nativeFormats = RTAUDIO_FLOAT32;

    return true;
}

bool RtApiCore :: probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                                 unsigned int firstChannel, unsigned int sampleRate,
                                 RtAudioFormat format, unsigned int *bufferSize,
                                 RtAudio::StreamOptions *options )
{
    UInt32 dataSize = 0;
    OSStatus result = 0;
    std::string busID;
    for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
        if ( deviceList_[m].ID == deviceId ) {
            busID = deviceList_[m].busID;
            break;
        }
    }

    if (busID.empty()) {
        errorText_ = "RtApiCore::probeDeviceOpen: the device ID was not found!";
        return FAILURE;
    }

    AudioObjectPropertyAddress property = { kAudioHardwarePropertyTranslateUIDToDevice,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT };

    CFStringRef cfbusid = nullptr;
    cfbusid = CFStringCreateWithCString(nullptr, busID.c_str(), CFStringGetSystemEncoding());

    AudioDeviceID id = 0;
    dataSize = sizeof(AudioDeviceID);
    result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, sizeof(CFStringRef), &cfbusid, &dataSize, &id );
    if ( result != noErr ) {
        errorText_ = "RtApiCore::probeDevices: OS-X system error getting device IDs.";
        error( RTAUDIO_SYSTEM_ERROR );
        return FAILURE;
    }
    CFRelease(cfbusid);

    property = { kAudioHardwarePropertyDevices,
                kAudioDevicePropertyScopeOutput,
                KAUDIOOBJECTPROPERTYELEMENT };

    // Setup for stream mode.
    if ( mode == INPUT ) {
        property.mScope = kAudioDevicePropertyScopeInput;
    }

    // Get the stream "configuration".
    AudioBufferList	*bufferList = nil;
    dataSize = 0;
    property.mSelector = kAudioDevicePropertyStreamConfiguration;
    result = AudioObjectGetPropertyDataSize( id, &property, 0, NULL, &dataSize );
    if ( result != noErr || dataSize == 0 ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting stream configuration info for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Allocate the AudioBufferList.
    bufferList = (AudioBufferList *) malloc( dataSize );
    if ( bufferList == NULL ) {
        errorText_ = "RtApiCore::probeDeviceOpen: memory error allocating AudioBufferList.";
        return FAILURE;
    }

    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, bufferList );
    if (result != noErr || dataSize == 0) {
        free( bufferList );
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting stream configuration for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Search for one or more streams that contain the desired number of
    // channels. CoreAudio devices can have an arbitrary number of
    // streams and each stream can have an arbitrary number of channels.
    // For each stream, a single buffer of interleaved samples is
    // provided.  RtAudio prefers the use of one stream of interleaved
    // data or multiple consecutive single-channel streams.  However, we
    // now support multiple consecutive multi-channel streams of
    // interleaved data as well.
    UInt32 iStream, offsetCounter = firstChannel;
    UInt32 nStreams = bufferList->mNumberBuffers;
    bool monoMode = false;
    bool foundStream = false;

    // First check that the device supports the requested number of
    // channels.
    UInt32 deviceChannels = 0;
    for ( iStream=0; iStream<nStreams; iStream++ )
        deviceChannels += bufferList->mBuffers[iStream].mNumberChannels;

    if ( deviceChannels < ( channels + firstChannel ) ) {
        free( bufferList );
        errorStream_ << "RtApiCore::probeDeviceOpen: the device (" << deviceId << ") does not support the requested channel count.";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Look for a single stream meeting our needs.
    UInt32 firstStream = 0, streamCount = 1, streamChannels = 0, channelOffset = 0;
    for ( iStream=0; iStream<nStreams; iStream++ ) {
        streamChannels = bufferList->mBuffers[iStream].mNumberChannels;
        if ( streamChannels >= channels + offsetCounter ) {
            firstStream = iStream;
            channelOffset = offsetCounter;
            foundStream = true;
            break;
        }
        if ( streamChannels > offsetCounter ) break;
        offsetCounter -= streamChannels;
    }

    // If we didn't find a single stream above, then we should be able
    // to meet the channel specification with multiple streams.
    if ( foundStream == false ) {
        monoMode = true;
        offsetCounter = firstChannel;
        for ( iStream=0; iStream<nStreams; iStream++ ) {
            streamChannels = bufferList->mBuffers[iStream].mNumberChannels;
            if ( streamChannels > offsetCounter ) break;
            offsetCounter -= streamChannels;
        }

        firstStream = iStream;
        channelOffset = offsetCounter;
        Int32 channelCounter = channels + offsetCounter - streamChannels;

        if ( streamChannels > 1 ) monoMode = false;
        while ( channelCounter > 0 ) {
            streamChannels = bufferList->mBuffers[++iStream].mNumberChannels;
            if ( streamChannels > 1 ) monoMode = false;
            channelCounter -= streamChannels;
            streamCount++;
        }
    }

    free( bufferList );

    // Try to set "hog" mode ... it's not clear to me this is working.
    if ( options && options->flags & RTAUDIO_HOG_DEVICE ) {
        pid_t hog_pid;
        dataSize = sizeof( hog_pid );
        property.mSelector = kAudioDevicePropertyHogMode;
        result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &hog_pid );
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting 'hog' state!";
            errorText_ = errorStream_.str();
            return FAILURE;
        }

        if ( hog_pid != getpid() ) {
            hog_pid = getpid();
            result = AudioObjectSetPropertyData( id, &property, 0, NULL, dataSize, &hog_pid );
            if ( result != noErr ) {
                errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") setting 'hog' state!";
                errorText_ = errorStream_.str();
                return FAILURE;
            }
        }
    }

    // Check and if necessary, change the sample rate for the device.
    Float64 nominalRate;
    dataSize = sizeof( Float64 );
    property.mSelector = kAudioDevicePropertyNominalSampleRate;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &nominalRate );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting current sample rate.";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Only try to change the sample rate if off by more than 1 Hz.
    if ( fabs( nominalRate - (double)sampleRate ) > 1.0 ) {

        nominalRate = (Float64) sampleRate;
        result = AudioObjectSetPropertyData( id, &property, 0, NULL, dataSize, &nominalRate );
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") setting sample rate for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }

        // Now wait until the reported nominal rate is what we just set.
        UInt32 microCounter = 0;
        Float64 reportedRate = 0.0;
        while ( reportedRate != nominalRate ) {
            microCounter += 5000;
            if ( microCounter > 2000000 ) break;
            usleep( 5000 );
            result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &reportedRate );
        }

        if ( microCounter > 2000000 ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: timeout waiting for sample rate update for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    }

    // Determine the buffer size.
    AudioValueRange	bufferRange;
    dataSize = sizeof( AudioValueRange );
    property.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &bufferRange );

    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting buffer size range for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    if ( bufferRange.mMinimum > *bufferSize ) *bufferSize = (unsigned int) bufferRange.mMinimum;
    else if ( bufferRange.mMaximum < *bufferSize ) *bufferSize = (unsigned int) bufferRange.mMaximum;
    if ( options && options->flags & RTAUDIO_MINIMIZE_LATENCY ) *bufferSize = (unsigned int) bufferRange.mMinimum;

    // Set the buffer size.  For multiple streams, I'm assuming we only
    // need to make this setting for the master channel.
    UInt32 theSize = (UInt32) *bufferSize;
    dataSize = sizeof( UInt32 );
    property.mSelector = kAudioDevicePropertyBufferFrameSize;
    result = AudioObjectSetPropertyData( id, &property, 0, NULL, dataSize, &theSize );

    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") setting the buffer size for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // If attempting to setup a duplex stream, the bufferSize parameter
    // MUST be the same in both directions!
    *bufferSize = theSize;
    if ( stream_.mode == OUTPUT && mode == INPUT && *bufferSize != stream_.bufferSize ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error setting buffer size for duplex stream on device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    stream_.bufferSize = *bufferSize;
    stream_.nBuffers = 1;

    // Now set the stream format for all streams.  Also, check the
    // physical format of the device and change that if necessary.
    AudioStreamBasicDescription	description;
    dataSize = sizeof( AudioStreamBasicDescription );
    property.mSelector = kAudioStreamPropertyVirtualFormat;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &description );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting stream format for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    // Set the sample rate and data format id.  However, only make the
    // change if the sample rate is not within 1.0 of the desired
    // rate and the format is not linear pcm.
    bool updateFormat = false;
    if ( fabs( description.mSampleRate - (Float64)sampleRate ) > 1.0 ) {
        description.mSampleRate = (Float64) sampleRate;
        updateFormat = true;
    }

    if ( description.mFormatID != kAudioFormatLinearPCM ) {
        description.mFormatID = kAudioFormatLinearPCM;
        updateFormat = true;
    }

    if ( updateFormat ) {
        result = AudioObjectSetPropertyData( id, &property, 0, NULL, dataSize, &description );
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") setting sample rate or data format for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    }

    // Now check the physical format.
    property.mSelector = kAudioStreamPropertyPhysicalFormat;
    result = AudioObjectGetPropertyData( id, &property, 0, NULL,  &dataSize, &description );
    if ( result != noErr ) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting stream physical format for device (" << deviceId << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
    }

    //std::cout << "Current physical stream format:" << std::endl;
    //std::cout << "   mBitsPerChan = " << description.mBitsPerChannel << std::endl;
    //std::cout << "   aligned high = " << (description.mFormatFlags & kAudioFormatFlagIsAlignedHigh) << ", isPacked = " << (description.mFormatFlags & kAudioFormatFlagIsPacked) << std::endl;
    //std::cout << "   bytesPerFrame = " << description.mBytesPerFrame << std::endl;
    //std::cout << "   sample rate = " << description.mSampleRate << std::endl;

    if ( description.mFormatID != kAudioFormatLinearPCM || description.mBitsPerChannel < 16 ) {
        description.mFormatID = kAudioFormatLinearPCM;
        //description.mSampleRate = (Float64) sampleRate;
        AudioStreamBasicDescription	testDescription = description;
        UInt32 formatFlags;

        // We'll try higher bit rates first and then work our way down.
        std::vector< std::pair<UInt32, UInt32>  > physicalFormats;
        formatFlags = (description.mFormatFlags | kLinearPCMFormatFlagIsFloat) & ~kLinearPCMFormatFlagIsSignedInteger;
        physicalFormats.push_back( std::pair<Float32, UInt32>( 32, formatFlags ) );
        formatFlags = (description.mFormatFlags | kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked) & ~kLinearPCMFormatFlagIsFloat;
        physicalFormats.push_back( std::pair<Float32, UInt32>( 32, formatFlags ) );
        physicalFormats.push_back( std::pair<Float32, UInt32>( 24, formatFlags ) );   // 24-bit packed
        formatFlags &= ~( kAudioFormatFlagIsPacked | kAudioFormatFlagIsAlignedHigh );
        physicalFormats.push_back( std::pair<Float32, UInt32>( 24.2, formatFlags ) ); // 24-bit in 4 bytes, aligned low
        formatFlags |= kAudioFormatFlagIsAlignedHigh;
        physicalFormats.push_back( std::pair<Float32, UInt32>( 24.4, formatFlags ) ); // 24-bit in 4 bytes, aligned high
        formatFlags = (description.mFormatFlags | kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked) & ~kLinearPCMFormatFlagIsFloat;
        physicalFormats.push_back( std::pair<Float32, UInt32>( 16, formatFlags ) );
        physicalFormats.push_back( std::pair<Float32, UInt32>( 8, formatFlags ) );

        bool setPhysicalFormat = false;
        for( unsigned int i=0; i<physicalFormats.size(); i++ ) {
            testDescription = description;
            testDescription.mBitsPerChannel = (UInt32) physicalFormats[i].first;
            testDescription.mFormatFlags = physicalFormats[i].second;
            if ( (24 == (UInt32)physicalFormats[i].first) && ~( physicalFormats[i].second & kAudioFormatFlagIsPacked ) )
                testDescription.mBytesPerFrame =  4 * testDescription.mChannelsPerFrame;
            else
                testDescription.mBytesPerFrame =  testDescription.mBitsPerChannel/8 * testDescription.mChannelsPerFrame;
            testDescription.mBytesPerPacket = testDescription.mBytesPerFrame * testDescription.mFramesPerPacket;
            result = AudioObjectSetPropertyData( id, &property, 0, NULL, dataSize, &testDescription );
            if ( result == noErr ) {
                setPhysicalFormat = true;
                //std::cout << "Updated physical stream format:" << std::endl;
                //std::cout << "   mBitsPerChan = " << testDescription.mBitsPerChannel << std::endl;
                //std::cout << "   aligned high = " << (testDescription.mFormatFlags & kAudioFormatFlagIsAlignedHigh) << ", isPacked = " << (testDescription.mFormatFlags & kAudioFormatFlagIsPacked) << std::endl;
                //std::cout << "   bytesPerFrame = " << testDescription.mBytesPerFrame << std::endl;
                //std::cout << "   sample rate = " << testDescription.mSampleRate << std::endl;
                break;
            }
        }

        if ( !setPhysicalFormat ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") setting physical data format for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    } // done setting virtual/physical formats.

    // Get the stream / device latency.
    UInt32 latency;
    dataSize = sizeof( UInt32 );
    property.mSelector = kAudioDevicePropertyLatency;
    if ( AudioObjectHasProperty( id, &property ) == true ) {
        result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &latency );
        if ( result == kAudioHardwareNoError ) stream_.latency[ mode ] = latency;
        else {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error (" << getErrorCode( result ) << ") getting device latency for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
        }
    }

    // Byte-swapping: According to AudioHardware.h, the stream data will
    // always be presented in native-endian format, so we should never
    // need to byte swap.
    stream_.doByteSwap[mode] = false;

    // From the CoreAudio documentation, PCM data must be supplied as
    // 32-bit floats.
    stream_.userFormat = format;
    stream_.deviceFormat[mode] = RTAUDIO_FLOAT32;

    if ( streamCount == 1 )
        stream_.nDeviceChannels[mode] = description.mChannelsPerFrame;
    else // multiple streams
        stream_.nDeviceChannels[mode] = channels;
    stream_.nUserChannels[mode] = channels;
    stream_.channelOffset[mode] = channelOffset;  // offset within a CoreAudio stream
    if ( options && options->flags & RTAUDIO_NONINTERLEAVED ) stream_.userInterleaved = false;
    else stream_.userInterleaved = true;
    stream_.deviceInterleaved[mode] = true;
    if ( monoMode == true ) stream_.deviceInterleaved[mode] = false;

    // Set flags for buffer conversion.
    stream_.doConvertBuffer[mode] = false;
    if ( stream_.userFormat != stream_.deviceFormat[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( stream_.nUserChannels[mode] < stream_.nDeviceChannels[mode] )
        stream_.doConvertBuffer[mode] = true;
    if ( streamCount == 1 ) {
        if ( stream_.nUserChannels[mode] > 1 &&
            stream_.userInterleaved != stream_.deviceInterleaved[mode] )
            stream_.doConvertBuffer[mode] = true;
    }
    else if ( monoMode && stream_.userInterleaved )
        stream_.doConvertBuffer[mode] = true;

    // Allocate our CoreHandle structure for the stream.
    CoreHandle *handle = 0;
    if ( stream_.apiHandle == 0 ) {
        try {
            handle = new CoreHandle;
        }
        catch ( std::bad_alloc& ) {
            errorText_ = "RtApiCore::probeDeviceOpen: error allocating CoreHandle memory.";
            goto error;
        }

        if ( pthread_cond_init( &handle->condition, NULL ) ) {
            errorText_ = "RtApiCore::probeDeviceOpen: error initializing pthread condition variable.";
            goto error;
        }
        stream_.apiHandle = (void *) handle;
    }
    else
        handle = (CoreHandle *) stream_.apiHandle;
    handle->iStream[mode] = firstStream;
    handle->nStreams[mode] = streamCount;
    handle->id[mode] = id;

    // Allocate necessary internal buffers.
    unsigned long bufferBytes;
    bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes( stream_.userFormat );
    stream_.userBuffer[mode] = (char *) calloc( bufferBytes, 1 );
    if ( stream_.userBuffer[mode] == NULL ) {
        errorText_ = "RtApiCore::probeDeviceOpen: error allocating user buffer memory.";
        goto error;
    }

    // If possible, we will make use of the CoreAudio stream buffers as
    // "device buffers".  However, we can't do this if using multiple
    // streams.
    if ( stream_.doConvertBuffer[mode] && handle->nStreams[mode] > 1 ) {

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
                errorText_ = "RtApiCore::probeDeviceOpen: error allocating device buffer memory.";
                goto error;
            }
        }
    }

    stream_.sampleRate = sampleRate;
    stream_.deviceId[mode] = deviceId;
    stream_.state = STREAM_STOPPED;
    stream_.callbackInfo.object = (void *) this;

    // Setup the buffer conversion information structure.
    if ( stream_.doConvertBuffer[mode] ) {
        if ( streamCount > 1 ) setConvertInfo( mode, 0 );
        else setConvertInfo( mode, channelOffset );
    }

    if ( mode == INPUT && stream_.mode == OUTPUT && stream_.deviceId[0] == deviceId )
        // Only one callback procedure and property listener per device.
        stream_.mode = DUPLEX;
    else {
#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
        result = AudioDeviceCreateIOProcID( id, callbackHandler, (void *) &stream_.callbackInfo, &handle->procId[mode] );
#else \
        // deprecated in favor of AudioDeviceCreateIOProcID()
        result = AudioDeviceAddIOProc( id, callbackHandler, (void *) &stream_.callbackInfo );
#endif
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error setting callback for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            goto error;
        }
        if ( stream_.mode == OUTPUT && mode == INPUT )
            stream_.mode = DUPLEX;
        else
            stream_.mode = mode;

        // Setup the device property listener for over/underload.
        property.mSelector = kAudioDeviceProcessorOverload;
        property.mScope = kAudioObjectPropertyScopeGlobal;
        result = AudioObjectAddPropertyListener( id, &property, xrunListener, (void *) handle );
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error setting xrun listener for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            goto error;
        }
        handle->xrunListenerAdded[mode] = true;

        // Setup a listener to detect a possible device disconnect.
        property.mSelector = kAudioDevicePropertyDeviceIsAlive;
        result = AudioObjectAddPropertyListener( id , &property, streamDisconnectListener, (void *) &stream_.callbackInfo );
        if ( result != noErr ) {
            AudioObjectRemovePropertyListener( id, &property, xrunListener, (void *) handle );
            errorStream_ << "RtApiCore::probeDeviceOpen: system error setting disconnect listener for device (" << deviceId << ").";
            errorText_ = errorStream_.str();
            goto error;
        }
        handle->disconnectListenerAdded[mode] = true;
    }

    return SUCCESS;

error:
    closeStream(); // this should safely clear out procedures, listeners and memory, even for duplex stream
    return FAILURE;
}

void RtApiCore :: closeStream( void )
{
    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiCore::closeStream(): no open stream to close!";
        error( RTAUDIO_WARNING );
        return;
    }

    CoreHandle *handle = (CoreHandle *) stream_.apiHandle;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
        if ( handle ) {
            AudioObjectPropertyAddress property = { kAudioHardwarePropertyDevices,
                                                   kAudioObjectPropertyScopeGlobal,
                                                   KAUDIOOBJECTPROPERTYELEMENT };
            if ( handle->xrunListenerAdded[0] ) {
                property.mSelector = kAudioDeviceProcessorOverload;
                if (AudioObjectRemovePropertyListener( handle->id[0], &property, xrunListener, (void *) handle ) != noErr) {
                    errorText_ = "RtApiCore::closeStream(): error removing xrun property listener!";
                    error( RTAUDIO_WARNING );
                }
            }
            if ( handle->disconnectListenerAdded[0] ) {
                property.mSelector = kAudioDevicePropertyDeviceIsAlive;
                if (AudioObjectRemovePropertyListener( handle->id[0], &property, streamDisconnectListener, (void *) &stream_.callbackInfo ) != noErr) {
                    errorText_ = "RtApiCore::closeStream(): error removing disconnect property listener!";
                    error( RTAUDIO_WARNING );
                }
            }

#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
            if ( handle->procId[0] ) {
                if ( stream_.state == STREAM_RUNNING )
                    AudioDeviceStop( handle->id[0], handle->procId[0] );
                AudioDeviceDestroyIOProcID( handle->id[0], handle->procId[0] );
            }
#else // deprecated behaviour
            if ( stream_.state == STREAM_RUNNING )
                AudioDeviceStop( handle->id[0], callbackHandler );
            AudioDeviceRemoveIOProc( handle->id[0], callbackHandler );
#endif
        }
    }

    if ( stream_.mode == INPUT || ( stream_.mode == DUPLEX && stream_.deviceId[0] != stream_.deviceId[1] ) ) {
        if ( handle ) {
            AudioObjectPropertyAddress property = { kAudioHardwarePropertyDevices,
                                                   kAudioObjectPropertyScopeGlobal,
                                                   KAUDIOOBJECTPROPERTYELEMENT };

            if ( handle->xrunListenerAdded[1] ) {
                property.mSelector = kAudioDeviceProcessorOverload;
                if (AudioObjectRemovePropertyListener( handle->id[1], &property, xrunListener, (void *) handle ) != noErr) {
                    errorText_ = "RtApiCore::closeStream(): error removing xrun property listener!";
                    error( RTAUDIO_WARNING );
                }
            }

            if ( handle->disconnectListenerAdded[0] ) {
                property.mSelector = kAudioDevicePropertyDeviceIsAlive;
                if (AudioObjectRemovePropertyListener( handle->id[1], &property, streamDisconnectListener, (void *) &stream_.callbackInfo ) != noErr) {
                    errorText_ = "RtApiCore::closeStream(): error removing disconnect property listener!";
                    error( RTAUDIO_WARNING );
                }
            }

#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
            if ( handle->procId[1] ) {
                if ( stream_.state == STREAM_RUNNING )
                    AudioDeviceStop( handle->id[1], handle->procId[1] );
                AudioDeviceDestroyIOProcID( handle->id[1], handle->procId[1] );
            }
#else // deprecated behaviour
            if ( stream_.state == STREAM_RUNNING )
                AudioDeviceStop( handle->id[1], callbackHandler );
            AudioDeviceRemoveIOProc( handle->id[1], callbackHandler );
#endif
        }
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

    // Destroy pthread condition variable.
    pthread_cond_signal( &handle->condition ); // signal condition variable in case stopStream is blocked
    pthread_cond_destroy( &handle->condition );
    delete handle;
    stream_.apiHandle = 0;

    CallbackInfo *info = (CallbackInfo *) &stream_.callbackInfo;
    if ( info->deviceDisconnected ) {
        errorText_ = "RtApiCore: the stream device was disconnected (and closed)!";
        error( RTAUDIO_DEVICE_DISCONNECT );
    }

    clearStreamInfo();
}

RtAudioErrorType RtApiCore :: startStream( void )
{
    if ( stream_.state != STREAM_STOPPED ) {
        if ( stream_.state == STREAM_RUNNING )
            errorText_ = "RtApiCore::startStream(): the stream is already running!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiCore::startStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    OSStatus result = noErr;
    CoreHandle *handle = (CoreHandle *) stream_.apiHandle;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
        result = AudioDeviceStart( handle->id[0], handle->procId[0] );
#else // deprecated behaviour
        result = AudioDeviceStart( handle->id[0], callbackHandler );
#endif
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::startStream: system error (" << getErrorCode( result ) << ") starting callback procedure on device (" << stream_.deviceId[0] << ").";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    if ( stream_.mode == INPUT ||
        ( stream_.mode == DUPLEX && stream_.deviceId[0] != stream_.deviceId[1] ) ) {

        // Clear user input buffer
        unsigned long bufferBytes;
        bufferBytes = stream_.nUserChannels[1] * stream_.bufferSize * formatBytes( stream_.userFormat );
        memset( stream_.userBuffer[1], 0, bufferBytes * sizeof(char) );

#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
        result = AudioDeviceStart( handle->id[1], handle->procId[1] );
#else // deprecated behaviour
        result = AudioDeviceStart( handle->id[1], callbackHandler );
#endif
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::startStream: system error starting input callback procedure on device (" << stream_.deviceId[1] << ").";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    handle->drainCounter = 0;
    handle->internalDrain = false;
    stream_.state = STREAM_RUNNING;

unlock:
    if ( result == noErr ) return RTAUDIO_NO_ERROR;
    return error( RTAUDIO_SYSTEM_ERROR );
}

RtAudioErrorType RtApiCore :: stopStream( void )
{
    if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiCore::stopStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiCore::stopStream(): the stream is closed!";
        return error( RTAUDIO_WARNING );
    }

    OSStatus result = noErr;
    CoreHandle *handle = (CoreHandle *) stream_.apiHandle;
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

        if ( handle->drainCounter == 0 ) {
            handle->drainCounter = 2;
            pthread_cond_wait( &handle->condition, &stream_.mutex ); // block until signaled
        }

#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
        result = AudioDeviceStop( handle->id[0], handle->procId[0] );
#else // deprecated behaviour
        result = AudioDeviceStop( handle->id[0], callbackHandler );
#endif
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::stopStream: system error (" << getErrorCode( result ) << ") stopping callback procedure on device (" << stream_.deviceId[0] << ").";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    if ( stream_.mode == INPUT || ( stream_.mode == DUPLEX && stream_.deviceId[0] != stream_.deviceId[1] ) ) {
#if defined( MAC_OS_X_VERSION_10_5 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5 )
        result = AudioDeviceStop( handle->id[1], handle->procId[1] );
#else  // deprecated behaviour
        result = AudioDeviceStop( handle->id[1], callbackHandler );
#endif
        if ( result != noErr ) {
            errorStream_ << "RtApiCore::stopStream: system error (" << getErrorCode( result ) << ") stopping input callback procedure on device (" << stream_.deviceId[1] << ").";
            errorText_ = errorStream_.str();
            goto unlock;
        }
    }

    stream_.state = STREAM_STOPPED;

unlock:
    if ( result == noErr ) return RTAUDIO_NO_ERROR;
    return error( RTAUDIO_SYSTEM_ERROR );
}

RtAudioErrorType RtApiCore :: abortStream( void )
{
    if ( stream_.state != STREAM_RUNNING ) {
        if ( stream_.state == STREAM_STOPPED )
            errorText_ = "RtApiCore::abortStream(): the stream is already stopped!";
        else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
            errorText_ = "RtApiCore::abortStream(): the stream is stopping or closed!";
        return error( RTAUDIO_WARNING );
    }

    CoreHandle *handle = (CoreHandle *) stream_.apiHandle;
    handle->drainCounter = 2;

    stream_.state = STREAM_STOPPING;
    return stopStream();
}

bool RtApiCore :: callbackEvent( AudioDeviceID deviceId,
                               const AudioBufferList *inBufferList,
                               const AudioBufferList *outBufferList )
{
    if ( stream_.state == STREAM_STOPPED || stream_.state == STREAM_STOPPING ) return SUCCESS;
    if ( stream_.state == STREAM_CLOSED ) {
        errorText_ = "RtApiCore::callbackEvent(): the stream is closed ... this shouldn't happen!";
        error( RTAUDIO_WARNING );
        return FAILURE;
    }

    CallbackInfo *info = (CallbackInfo *) &stream_.callbackInfo;
    CoreHandle *handle = (CoreHandle *) stream_.apiHandle;

    // Check if we were draining the stream and signal is finished.
    if ( handle->drainCounter > 3 ) {
        ThreadHandle threadId;

        stream_.state = STREAM_STOPPING;
        if ( handle->internalDrain == true )
            pthread_create( &threadId, NULL, coreStopStream, info );
        else // external call to stopStream()
            pthread_cond_signal( &handle->condition );
        return SUCCESS;
    }

    AudioDeviceID outputDevice = handle->id[0];

    // Invoke user callback to get fresh output data UNLESS we are
    // draining stream or duplex mode AND the input/output devices are
    // different AND this function is called for the input device.
    if ( handle->drainCounter == 0 && ( stream_.mode != DUPLEX || deviceId == outputDevice ) ) {
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
            abortStream();
            return SUCCESS;
        }
        else if ( cbReturnValue == 1 ) {
            handle->drainCounter = 1;
            handle->internalDrain = true;
        }
    }

    if ( stream_.mode == OUTPUT || ( stream_.mode == DUPLEX && deviceId == outputDevice ) ) {

        if ( handle->drainCounter > 1 ) { // write zeros to the output stream

            if ( handle->nStreams[0] == 1 ) {
                memset( outBufferList->mBuffers[handle->iStream[0]].mData,
                       0,
                       outBufferList->mBuffers[handle->iStream[0]].mDataByteSize );
            }
            else { // fill multiple streams with zeros
                for ( unsigned int i=0; i<handle->nStreams[0]; i++ ) {
                    memset( outBufferList->mBuffers[handle->iStream[0]+i].mData,
                           0,
                           outBufferList->mBuffers[handle->iStream[0]+i].mDataByteSize );
                }
            }
        }
        else if ( handle->nStreams[0] == 1 ) {
            if ( stream_.doConvertBuffer[0] ) { // convert directly to CoreAudio stream buffer
                convertBuffer( (char *) outBufferList->mBuffers[handle->iStream[0]].mData,
                              stream_.userBuffer[0], stream_.convertInfo[0], stream_.bufferSize, StreamMode::OUTPUT);
            }
            else { // copy from user buffer
                memcpy( outBufferList->mBuffers[handle->iStream[0]].mData,
                       stream_.userBuffer[0],
                       outBufferList->mBuffers[handle->iStream[0]].mDataByteSize );
            }
        }
        else { // fill multiple streams
            Float32 *inBuffer = (Float32 *) stream_.userBuffer[0];
            if ( stream_.doConvertBuffer[0] ) {
                convertBuffer( stream_.deviceBuffer, stream_.userBuffer[0], stream_.convertInfo[0], stream_.bufferSize, StreamMode::OUTPUT);
                inBuffer = (Float32 *) stream_.deviceBuffer;
            }

            if ( stream_.deviceInterleaved[0] == false ) { // mono mode
                UInt32 bufferBytes = outBufferList->mBuffers[handle->iStream[0]].mDataByteSize;
                for ( unsigned int i=0; i<stream_.nUserChannels[0]; i++ ) {
                    memcpy( outBufferList->mBuffers[handle->iStream[0]+i].mData,
                           (void *)&inBuffer[i*stream_.bufferSize], bufferBytes );
                }
            }
            else { // fill multiple multi-channel streams with interleaved data
                UInt32 streamChannels, channelsLeft, inJump, outJump, inOffset;
                Float32 *out, *in;

                bool inInterleaved = ( stream_.userInterleaved ) ? true : false;
                UInt32 inChannels = stream_.nUserChannels[0];
                if ( stream_.doConvertBuffer[0] ) {
                    inInterleaved = true; // device buffer will always be interleaved for nStreams > 1 and not mono mode
                    inChannels = stream_.nDeviceChannels[0];
                }

                if ( inInterleaved ) inOffset = 1;
                else inOffset = stream_.bufferSize;

                channelsLeft = inChannels;
                for ( unsigned int i=0; i<handle->nStreams[0]; i++ ) {
                    in = inBuffer;
                    out = (Float32 *) outBufferList->mBuffers[handle->iStream[0]+i].mData;
                    streamChannels = outBufferList->mBuffers[handle->iStream[0]+i].mNumberChannels;

                    outJump = 0;
                    // Account for possible channel offset in first stream
                    if ( i == 0 && stream_.channelOffset[0] > 0 ) {
                        streamChannels -= stream_.channelOffset[0];
                        outJump = stream_.channelOffset[0];
                        out += outJump;
                    }

                    // Account for possible unfilled channels at end of the last stream
                    if ( streamChannels > channelsLeft ) {
                        outJump = streamChannels - channelsLeft;
                        streamChannels = channelsLeft;
                    }

                    // Determine input buffer offsets and skips
                    if ( inInterleaved ) {
                        inJump = inChannels;
                        in += inChannels - channelsLeft;
                    }
                    else {
                        inJump = 1;
                        in += (inChannels - channelsLeft) * inOffset;
                    }

                    for ( unsigned int i=0; i<stream_.bufferSize; i++ ) {
                        for ( unsigned int j=0; j<streamChannels; j++ ) {
                            *out++ = in[j*inOffset];
                        }
                        out += outJump;
                        in += inJump;
                    }
                    channelsLeft -= streamChannels;
                }
            }
        }
    }

    // Don't bother draining input
    if ( handle->drainCounter ) {
        handle->drainCounter++;
        goto unlock;
    }

    AudioDeviceID inputDevice;
    inputDevice = handle->id[1];
    if ( stream_.mode == INPUT || ( stream_.mode == DUPLEX && deviceId == inputDevice ) ) {

        if ( handle->nStreams[1] == 1 ) {
            if ( stream_.doConvertBuffer[1] ) { // convert directly from CoreAudio stream buffer
                convertBuffer( stream_.userBuffer[1],
                              (char *) inBufferList->mBuffers[handle->iStream[1]].mData,
                              stream_.convertInfo[1], stream_.bufferSize, StreamMode::INPUT);
            }
            else { // copy to user buffer
                memcpy( stream_.userBuffer[1],
                       inBufferList->mBuffers[handle->iStream[1]].mData,
                       inBufferList->mBuffers[handle->iStream[1]].mDataByteSize );
            }
        }
        else { // read from multiple streams
            Float32 *outBuffer = (Float32 *) stream_.userBuffer[1];
            if ( stream_.doConvertBuffer[1] ) outBuffer = (Float32 *) stream_.deviceBuffer;

            if ( stream_.deviceInterleaved[1] == false ) { // mono mode
                UInt32 bufferBytes = inBufferList->mBuffers[handle->iStream[1]].mDataByteSize;
                for ( unsigned int i=0; i<stream_.nUserChannels[1]; i++ ) {
                    memcpy( (void *)&outBuffer[i*stream_.bufferSize],
                           inBufferList->mBuffers[handle->iStream[1]+i].mData, bufferBytes );
                }
            }
            else { // read from multiple multi-channel streams
                UInt32 streamChannels, channelsLeft, inJump, outJump, outOffset;
                Float32 *out, *in;

                bool outInterleaved = ( stream_.userInterleaved ) ? true : false;
                UInt32 outChannels = stream_.nUserChannels[1];
                if ( stream_.doConvertBuffer[1] ) {
                    outInterleaved = true; // device buffer will always be interleaved for nStreams > 1 and not mono mode
                    outChannels = stream_.nDeviceChannels[1];
                }

                if ( outInterleaved ) outOffset = 1;
                else outOffset = stream_.bufferSize;

                channelsLeft = outChannels;
                for ( unsigned int i=0; i<handle->nStreams[1]; i++ ) {
                    out = outBuffer;
                    in = (Float32 *) inBufferList->mBuffers[handle->iStream[1]+i].mData;
                    streamChannels = inBufferList->mBuffers[handle->iStream[1]+i].mNumberChannels;

                    inJump = 0;
                    // Account for possible channel offset in first stream
                    if ( i == 0 && stream_.channelOffset[1] > 0 ) {
                        streamChannels -= stream_.channelOffset[1];
                        inJump = stream_.channelOffset[1];
                        in += inJump;
                    }

                    // Account for possible unread channels at end of the last stream
                    if ( streamChannels > channelsLeft ) {
                        inJump = streamChannels - channelsLeft;
                        streamChannels = channelsLeft;
                    }

                    // Determine output buffer offsets and skips
                    if ( outInterleaved ) {
                        outJump = outChannels;
                        out += outChannels - channelsLeft;
                    }
                    else {
                        outJump = 1;
                        out += (outChannels - channelsLeft) * outOffset;
                    }

                    for ( unsigned int i=0; i<stream_.bufferSize; i++ ) {
                        for ( unsigned int j=0; j<streamChannels; j++ ) {
                            out[j*outOffset] = *in++;
                        }
                        out += outJump;
                        in += inJump;
                    }
                    channelsLeft -= streamChannels;
                }
            }

            if ( stream_.doConvertBuffer[1] ) { // convert from our internal "device" buffer
                convertBuffer( stream_.userBuffer[1],
                              stream_.deviceBuffer,
                              stream_.convertInfo[1], stream_.bufferSize, StreamMode::INPUT);
            }
        }
    }

unlock:

    // Make sure to only tick duplex stream time once if using two devices
    if ( stream_.mode == DUPLEX ) {
        if ( handle->id[0] == handle->id[1] ) // same device, only one callback
            RtApi::tickStreamTime();
        else if ( deviceId == handle->id[0] )
            RtApi::tickStreamTime(); // two devices, only tick on the output callback
    } else
        RtApi::tickStreamTime(); // input or output stream only

    return SUCCESS;
}

const char* RtApiCore :: getErrorCode( OSStatus code )
{
    switch( code ) {

    case kAudioHardwareNotRunningError:
        return "kAudioHardwareNotRunningError";

    case kAudioHardwareUnspecifiedError:
        return "kAudioHardwareUnspecifiedError";

    case kAudioHardwareUnknownPropertyError:
        return "kAudioHardwareUnknownPropertyError";

    case kAudioHardwareBadPropertySizeError:
        return "kAudioHardwareBadPropertySizeError";

    case kAudioHardwareIllegalOperationError:
        return "kAudioHardwareIllegalOperationError";

    case kAudioHardwareBadObjectError:
        return "kAudioHardwareBadObjectError";

    case kAudioHardwareBadDeviceError:
        return "kAudioHardwareBadDeviceError";

    case kAudioHardwareBadStreamError:
        return "kAudioHardwareBadStreamError";

    case kAudioHardwareUnsupportedOperationError:
        return "kAudioHardwareUnsupportedOperationError";

    case kAudioDeviceUnsupportedFormatError:
        return "kAudioDeviceUnsupportedFormatError";

    case kAudioDevicePermissionsError:
        return "kAudioDevicePermissionsError";

    default:
        return "CoreAudio unknown error";
    }
}
