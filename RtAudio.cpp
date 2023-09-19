/************************************************************************/
/*! \class RtAudio
    \brief Realtime audio i/o C++ classes.

    RtAudio provides a common API (Application Programming Interface)
    for realtime audio input/output across Linux (native ALSA, Jack,
    and OSS), Macintosh OS X (CoreAudio and Jack), and Windows
    (DirectSound, ASIO and WASAPI) operating systems.

    RtAudio GitHub site: https://github.com/thestk/rtaudio
    RtAudio WWW site: http://www.music.mcgill.ca/~gary/rtaudio/

    RtAudio: realtime audio i/o C++ classes
    Copyright (c) 2001-2023 Gary P. Scavone

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation files
    (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software,
    and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    Any person wishing to distribute modifications to the Software is
    asked to send the modifications to the original developer so that
    they can be incorporated into the canonical version.  This is,
    however, not a binding provision of this license.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/************************************************************************/

// RtAudio: Version 6.0.1

#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>
#include <algorithm>
#include <codecvt>
#include <locale>
#include "utils.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// Static variable definitions.
const unsigned int RtApi::MAX_SAMPLE_RATES = 14;
const unsigned int RtApi::SAMPLE_RATES[] = {
  4000, 5512, 8000, 9600, 11025, 16000, 22050,
  32000, 44100, 48000, 88200, 96000, 176400, 192000
};

// *************************************************** //
//
// RtApi subclass prototypes.
//
// *************************************************** //

#if defined(__MACOSX_CORE__)

#include <CoreAudio/AudioHardware.h>

class RtApiCore: public RtApi
{
public:

  RtApiCore();
  ~RtApiCore();
  RtAudio::Api getCurrentApi( void ) override { return RtAudio::MACOSX_CORE; }
  unsigned int getDefaultOutputDevice( void ) override;
  unsigned int getDefaultInputDevice( void ) override;
  void closeStream( void ) override;
  RtAudioErrorType startStream( void ) override;
  RtAudioErrorType stopStream( void ) override;
  RtAudioErrorType abortStream( void ) override;

  // This function is intended for internal use only.  It must be
  // public because it is called by an internal callback handler,
  // which is not a member of RtAudio.  External use of this function
  // will most likely produce highly undesirable results!
  bool callbackEvent( AudioDeviceID deviceId,
                      const AudioBufferList *inBufferList,
                      const AudioBufferList *outBufferList );

 private:
  void probeDevices( void ) override;
  bool probeDeviceInfo( AudioDeviceID id, RtAudio::DeviceInfo &info );
  bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels, 
                        unsigned int firstChannel, unsigned int sampleRate,
                        RtAudioFormat format, unsigned int *bufferSize,
                        RtAudio::StreamOptions *options ) override;
  static const char* getErrorCode( OSStatus code );
  std::vector< AudioDeviceID > deviceIds_;
};

#endif

#if defined(__UNIX_JACK__)

#include <jack/jack.h>

class RtApiJack: public RtApi
{
public:

  RtApiJack();
  ~RtApiJack();
  RtAudio::Api getCurrentApi( void ) override { return RtAudio::UNIX_JACK; }
  void closeStream( void ) override;
  RtAudioErrorType startStream( void ) override;
  RtAudioErrorType stopStream( void ) override;
  RtAudioErrorType abortStream( void ) override;

  // This function is intended for internal use only.  It must be
  // public because it is called by the internal callback handler,
  // which is not a member of RtAudio.  External use of this function
  // will most likely produce highly undesirable results!
  bool callbackEvent( unsigned long nframes );

  private:
  void probeDevices( void ) override;
  bool probeDeviceInfo( RtAudio::DeviceInfo &info, jack_client_t *client );
  bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels, 
                        unsigned int firstChannel, unsigned int sampleRate,
                        RtAudioFormat format, unsigned int *bufferSize,
                        RtAudio::StreamOptions *options ) override;

  bool shouldAutoconnect_;
};

#endif

#if defined(__WINDOWS_ASIO__)

#include "asio/RtApiAsio.h"

#endif

#if defined(__WINDOWS_DS__)

#include "directsound/RtApiDirectsound.h"

#endif

#if defined(__WINDOWS_WASAPI__)

#include "RtAudioWasapi.h"

#endif

#if defined(__LINUX_ALSA__)

class RtApiAlsa: public RtApi
{
public:

  RtApiAlsa();
  ~RtApiAlsa();
  RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_ALSA; }
  void closeStream( void ) override;
  RtAudioErrorType startStream( void ) override;
  RtAudioErrorType stopStream( void ) override;
  RtAudioErrorType abortStream( void ) override;

  // This function is intended for internal use only.  It must be
  // public because it is called by the internal callback handler,
  // which is not a member of RtAudio.  External use of this function
  // will most likely produce highly undesirable results!
  void callbackEvent( void );

  private:
  std::vector<std::pair<std::string, unsigned int>> deviceIdPairs_;
  
  void probeDevices( void ) override;
  bool probeDeviceInfo( RtAudio::DeviceInfo &info, std::string name );
  bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels, 
                        unsigned int firstChannel, unsigned int sampleRate,
                        RtAudioFormat format, unsigned int *bufferSize,
                        RtAudio::StreamOptions *options ) override;
};

#endif

#if defined(__LINUX_PULSE__)

#include <pulse/pulseaudio.h>

class RtApiPulse: public RtApi
{
public:
  ~RtApiPulse();
  RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_PULSE; }
  void closeStream( void ) override;
  RtAudioErrorType startStream( void ) override;
  RtAudioErrorType stopStream( void ) override;
  RtAudioErrorType abortStream( void ) override;

  // This function is intended for internal use only.  It must be
  // public because it is called by the internal callback handler,
  // which is not a member of RtAudio.  External use of this function
  // will most likely produce highly undesirable results!
  void callbackEvent( void );

  struct PaDeviceInfo {
    std::string sinkName;
    std::string sourceName;
  };

 private:
  std::vector< PaDeviceInfo > paDeviceList_;

  void probeDevices( void ) override;
  bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                        unsigned int firstChannel, unsigned int sampleRate,
                        RtAudioFormat format, unsigned int *bufferSize,
                        RtAudio::StreamOptions *options ) override;
};

#endif

#if defined(__LINUX_OSS__)

#include <sys/soundcard.h>

class RtApiOss: public RtApi
{
public:

  RtApiOss();
  ~RtApiOss();
  RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_OSS; }
  void closeStream( void ) override;
  RtAudioErrorType startStream( void ) override;
  RtAudioErrorType stopStream( void ) override;
  RtAudioErrorType abortStream( void ) override;

  // This function is intended for internal use only.  It must be
  // public because it is called by the internal callback handler,
  // which is not a member of RtAudio.  External use of this function
  // will most likely produce highly undesirable results!
  void callbackEvent( void );

  private:

  void probeDevices( void ) override;
  bool probeDeviceInfo( RtAudio::DeviceInfo &info, oss_audioinfo &ainfo );
  bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels, 
                        unsigned int firstChannel, unsigned int sampleRate,
                        RtAudioFormat format, unsigned int *bufferSize,
                        RtAudio::StreamOptions *options ) override;
};

#endif

#if defined(__RTAUDIO_DUMMY__)

class RtApiDummy: public RtApi
{
public:

  RtApiDummy() { errorText_ = "RtApiDummy: This class provides no functionality."; error( RTAUDIO_WARNING ); }
  RtAudio::Api getCurrentApi( void ) override { return RtAudio::RTAUDIO_DUMMY; }
  void closeStream( void ) override {}
  RtAudioErrorType startStream( void ) override { return RTAUDIO_NO_ERROR; }
  RtAudioErrorType stopStream( void ) override { return RTAUDIO_NO_ERROR; }
  RtAudioErrorType abortStream( void ) override { return RTAUDIO_NO_ERROR; }

  private:

  bool probeDeviceOpen( unsigned int /*deviceId*/, StreamMode /*mode*/, unsigned int /*channels*/, 
                        unsigned int /*firstChannel*/, unsigned int /*sampleRate*/,
                        RtAudioFormat /*format*/, unsigned int * /*bufferSize*/,
                        RtAudio::StreamOptions * /*options*/ ) override { return false; }
};

#endif

// *************************************************** //
//
// RtAudio definitions.
//
// *************************************************** //

std::string RtAudio :: getVersion( void )
{
  return RTAUDIO_VERSION;
}

// Define API names and display names.
// Must be in same order as API enum.
extern "C" {
const char* rtaudio_api_names[][2] = {
  { "unspecified" , "Unknown" },
  { "core"        , "CoreAudio" },
  { "alsa"        , "ALSA" },
  { "jack"        , "Jack" },
  { "pulse"       , "Pulse" },
  { "oss"         , "OpenSoundSystem" },
  { "asio"        , "ASIO" },
  { "wasapi"      , "WASAPI" },
  { "ds"          , "DirectSound" },
  { "dummy"       , "Dummy" },
};

const unsigned int rtaudio_num_api_names = 
  sizeof(rtaudio_api_names)/sizeof(rtaudio_api_names[0]);

// The order here will control the order of RtAudio's API search in
// the constructor.
extern "C" const RtAudio::Api rtaudio_compiled_apis[] = {
#if defined(__MACOSX_CORE__)
  RtAudio::MACOSX_CORE,
#endif
#if defined(__LINUX_ALSA__)
  RtAudio::LINUX_ALSA,
#endif
#if defined(__UNIX_JACK__)
  RtAudio::UNIX_JACK,
#endif
#if defined(__LINUX_PULSE__)
  RtAudio::LINUX_PULSE,
#endif
#if defined(__LINUX_OSS__)
  RtAudio::LINUX_OSS,
#endif
#if defined(__WINDOWS_ASIO__)
  RtAudio::WINDOWS_ASIO,
#endif
#if defined(__WINDOWS_WASAPI__)
  RtAudio::WINDOWS_WASAPI,
#endif
#if defined(__WINDOWS_DS__)
  RtAudio::WINDOWS_DS,
#endif
#if defined(__RTAUDIO_DUMMY__)
  RtAudio::RTAUDIO_DUMMY,
#endif
  RtAudio::UNSPECIFIED,
};

extern "C" const unsigned int rtaudio_num_compiled_apis =
  sizeof(rtaudio_compiled_apis)/sizeof(rtaudio_compiled_apis[0])-1;
}

// This is a compile-time check that rtaudio_num_api_names == RtAudio::NUM_APIS.
// If the build breaks here, check that they match.
template<bool b> class StaticAssert { private: StaticAssert() {} };
template<> class StaticAssert<true>{ public: StaticAssert() {} };
class StaticAssertions { StaticAssertions() {
  StaticAssert<rtaudio_num_api_names == RtAudio::NUM_APIS>();
}};

void RtAudio :: getCompiledApi( std::vector<RtAudio::Api> &apis )
{
  apis = std::vector<RtAudio::Api>(rtaudio_compiled_apis,
                                   rtaudio_compiled_apis + rtaudio_num_compiled_apis);
}

std::string RtAudio :: getApiName( RtAudio::Api api )
{
  if (api < 0 || api >= RtAudio::NUM_APIS)
    return "";
  return rtaudio_api_names[api][0];
}

std::string RtAudio :: getApiDisplayName( RtAudio::Api api )
{
  if (api < 0 || api >= RtAudio::NUM_APIS)
    return "Unknown";
  return rtaudio_api_names[api][1];
}

RtAudio::Api RtAudio :: getCompiledApiByName( const std::string &name )
{
  unsigned int i=0;
  for (i = 0; i < rtaudio_num_compiled_apis; ++i)
    if (name == rtaudio_api_names[rtaudio_compiled_apis[i]][0])
      return rtaudio_compiled_apis[i];
  return RtAudio::UNSPECIFIED;
}

RtAudio::Api RtAudio :: getCompiledApiByDisplayName( const std::string &name )
{
  unsigned int i=0;
  for (i = 0; i < rtaudio_num_compiled_apis; ++i)
    if (name == rtaudio_api_names[rtaudio_compiled_apis[i]][1])
      return rtaudio_compiled_apis[i];
  return RtAudio::UNSPECIFIED;
}

void RtAudio :: openRtApi( RtAudio::Api api )
{
  if ( rtapi_ )
    delete rtapi_;
  rtapi_ = 0;

#if defined(__UNIX_JACK__)
  if ( api == UNIX_JACK )
    rtapi_ = new RtApiJack();
#endif
#if defined(__LINUX_ALSA__)
  if ( api == LINUX_ALSA )
    rtapi_ = new RtApiAlsa();
#endif
#if defined(__LINUX_PULSE__)
  if ( api == LINUX_PULSE )
    rtapi_ = new RtApiPulse();
#endif
#if defined(__LINUX_OSS__)
  if ( api == LINUX_OSS )
    rtapi_ = new RtApiOss();
#endif
#if defined(__WINDOWS_ASIO__)
  if ( api == WINDOWS_ASIO )
    rtapi_ = new RtApiAsio();
#endif
#if defined(__WINDOWS_WASAPI__)
  if ( api == WINDOWS_WASAPI )
    rtapi_ = new RtApiWasapi();
#endif
#if defined(__WINDOWS_DS__)
  if ( api == WINDOWS_DS )
    rtapi_ = new RtApiDs();
#endif
#if defined(__MACOSX_CORE__)
  if ( api == MACOSX_CORE )
    rtapi_ = new RtApiCore();
#endif
#if defined(__RTAUDIO_DUMMY__)
  if ( api == RTAUDIO_DUMMY )
    rtapi_ = new RtApiDummy();
#endif
}

RtAudio :: RtAudio( RtAudio::Api api, RtAudioErrorCallback&& errorCallback )
{
  rtapi_ = 0;

  std::string errorMessage;
  if ( api != UNSPECIFIED ) {
    // Attempt to open the specified API.
    openRtApi( api );

    if ( rtapi_ ) {
      if ( errorCallback ) rtapi_->setErrorCallback( errorCallback );
      return;
    }

    // No compiled support for specified API value.  Issue a warning
    // and continue as if no API was specified.
    errorMessage = "RtAudio: no compiled support for specified API argument!";
    if ( errorCallback )
      errorCallback( RTAUDIO_INVALID_USE, errorMessage );
    else
      std::cerr << '\n' << errorMessage << '\n' << std::endl;
  }

  // Iterate through the compiled APIs and return as soon as we find
  // one with at least one device or we reach the end of the list.
  std::vector< RtAudio::Api > apis;
  getCompiledApi( apis );
  for ( unsigned int i=0; i<apis.size(); i++ ) {
    openRtApi( apis[i] );
    if ( rtapi_ && (rtapi_->getDeviceNames()).size() > 0 )
      break;
  }

  if ( rtapi_ ) {
    if ( errorCallback ) rtapi_->setErrorCallback( errorCallback );
    return;
  }

  // It should not be possible to get here because the preprocessor
  // definition __RTAUDIO_DUMMY__ is automatically defined in RtAudio.h
  // if no API-specific definitions are passed to the compiler. But just
  // in case something weird happens, issue an error message and abort.
  errorMessage = "RtAudio: no compiled API support found ... critical error!";
  if ( errorCallback )
    errorCallback( RTAUDIO_INVALID_USE, errorMessage );
  else
    std::cerr << '\n' << errorMessage << '\n' << std::endl;
  abort();
}

RtAudio :: ~RtAudio()
{
  if ( rtapi_ )
    delete rtapi_;
}

RtAudioErrorType RtAudio :: openStream( RtAudio::StreamParameters *outputParameters,
                                        RtAudio::StreamParameters *inputParameters,
                                        RtAudioFormat format, unsigned int sampleRate,
                                        unsigned int *bufferFrames,
                                        RtAudioCallback callback, void *userData,
                                        RtAudio::StreamOptions *options )
{
  return rtapi_->openStream( outputParameters, inputParameters, format,
                             sampleRate, bufferFrames, callback,
                             userData, options );
}

// *************************************************** //
//
// Public RtApi definitions (see end of file for
// private or protected utility functions).
//
// *************************************************** //

RtApi :: RtApi()
{
  clearStreamInfo();
  MUTEX_INITIALIZE( &stream_.mutex );
  errorCallback_ = 0;
  showWarnings_ = true;
  currentDeviceId_ = 129;
}

RtApi :: ~RtApi()
{
  MUTEX_DESTROY( &stream_.mutex );
}

RtAudioErrorType RtApi :: openStream( RtAudio::StreamParameters *oParams,
                                      RtAudio::StreamParameters *iParams,
                                      RtAudioFormat format, unsigned int sampleRate,
                                      unsigned int *bufferFrames,
                                      RtAudioCallback callback, void *userData,
                                      RtAudio::StreamOptions *options )
{
  if ( stream_.state != STREAM_CLOSED ) {
    errorText_ = "RtApi::openStream: a stream is already open!";
    return error( RTAUDIO_INVALID_USE );
  }

  // Clear stream information potentially left from a previously open stream.
  clearStreamInfo();

  if ( oParams && oParams->nChannels < 1 ) {
    errorText_ = "RtApi::openStream: a non-NULL output StreamParameters structure cannot have an nChannels value less than one.";
    return error( RTAUDIO_INVALID_PARAMETER );
  }

  if ( iParams && iParams->nChannels < 1 ) {
    errorText_ = "RtApi::openStream: a non-NULL input StreamParameters structure cannot have an nChannels value less than one.";
    return error( RTAUDIO_INVALID_PARAMETER );
  }

  if ( oParams == NULL && iParams == NULL ) {
    errorText_ = "RtApi::openStream: input and output StreamParameters structures are both NULL!";
    return error( RTAUDIO_INVALID_PARAMETER );
  }

  if ( formatBytes(format) == 0 ) {
    errorText_ = "RtApi::openStream: 'format' parameter value is undefined.";
    return error( RTAUDIO_INVALID_PARAMETER );
  }

  // Scan devices if none currently listed.
  if ( deviceList_.size() == 0 ) probeDevices();
  
  unsigned int m, oChannels = 0;
  if ( oParams ) {
    oChannels = oParams->nChannels;
    // Verify that the oParams->deviceId is found in our list
    for ( m=0; m<deviceList_.size(); m++ ) {
      if ( deviceList_[m].ID == oParams->deviceId ) break;
    }
    if ( m == deviceList_.size() ) {
      errorText_ = "RtApi::openStream: output device ID is invalid.";
      return error( RTAUDIO_INVALID_PARAMETER );
    }
  }

  unsigned int iChannels = 0;
  if ( iParams ) {
    iChannels = iParams->nChannels;
    for ( m=0; m<deviceList_.size(); m++ ) {
      if ( deviceList_[m].ID == iParams->deviceId ) break;
    }
    if ( m == deviceList_.size() ) {
      errorText_ = "RtApi::openStream: input device ID is invalid.";
      return error( RTAUDIO_INVALID_PARAMETER );
    }
  }

  bool result;

  if ( oChannels > 0 ) {

    result = probeDeviceOpen( oParams->deviceId, OUTPUT, oChannels, oParams->firstChannel,
                              sampleRate, format, bufferFrames, options );
    if ( result == false )
      return error( RTAUDIO_SYSTEM_ERROR );
  }

  if ( iChannels > 0 ) {

    result = probeDeviceOpen( iParams->deviceId, INPUT, iChannels, iParams->firstChannel,
                              sampleRate, format, bufferFrames, options );
    if ( result == false )
      return error( RTAUDIO_SYSTEM_ERROR );
  }

  stream_.callbackInfo.callback = (void *) callback;
  stream_.callbackInfo.userData = userData;

  if ( options ) options->numberOfBuffers = stream_.nBuffers;
  stream_.state = STREAM_STOPPED;
  return RTAUDIO_NO_ERROR;
}

void RtApi :: probeDevices( void )
{
  // This function MUST be implemented in all subclasses! Within each
  // API, this function will be used to:
  // - enumerate the devices and fill or update our
  //   std::vector< RtAudio::DeviceInfo> deviceList_ class variable
  // - store corresponding (usually API-specific) identifiers that
  //   are needed to open each device
  // - make sure that the default devices are properly identified
  //   within the deviceList_ (unless API-specific functions are
  //   available for this purpose).
  //
  // The function should not reprobe devices that have already been
  // found. The function must properly handle devices that are removed
  // or added.
  //
  // Ideally, we would also configure callback functions to be invoked
  // when devices are added or removed (which could be used to inform
  // clients about changes). However, none of the APIs currently
  // support notification of _new_ devices and I don't see the
  // usefulness of having this work only for device removal.
  return;
}

void RtApi :: listDevices( void )
{    
  probeDevices();
}

unsigned int RtApi :: getDeviceCount( void )
{
  probeDevices();
  return (unsigned int)deviceList_.size();
}

std::vector<unsigned int> RtApi :: getDeviceIds( void )
{
  probeDevices();

  // Copy device IDs into output vector.
  std::vector<unsigned int> deviceIds;
  for ( unsigned int m=0; m<deviceList_.size(); m++ )
    deviceIds.push_back( deviceList_[m].ID );

  return deviceIds;
}

std::vector<unsigned int> RtApi :: getDeviceIdsNoProbe(void)
{
  listDevices();
  std::vector<unsigned int> deviceIds;
  for (unsigned int m = 0; m < deviceList_.size(); m++)
    deviceIds.push_back(deviceList_[m].ID);
  return deviceIds;
}

std::vector<std::string> RtApi :: getDeviceNames( void )
{
  probeDevices();

  // Copy device names into output vector.
  std::vector<std::string> deviceNames;
  for ( unsigned int m=0; m<deviceList_.size(); m++ )
    deviceNames.push_back( deviceList_[m].name );

  return deviceNames;
}

unsigned int RtApi :: getDefaultInputDevice( void )
{
  // Should be reimplemented in subclasses if necessary.
  if ( deviceList_.size() == 0 ) probeDevices();
  for ( unsigned int i = 0; i < deviceList_.size(); i++ ) {
    if ( deviceList_[i].isDefaultInput )
      return deviceList_[i].ID;
  }

  // If not found, find the first device with input channels, set it
  // as the default, and return the ID.
  for ( unsigned int i = 0; i < deviceList_.size(); i++ ) {
    if ( deviceList_[i].inputChannels > 0 ) {
      deviceList_[i].isDefaultInput = true;
      return deviceList_[i].ID;
    }
  }

  return 0;
}

unsigned int RtApi :: getDefaultOutputDevice( void )
{
  // Should be reimplemented in subclasses if necessary.
  if ( deviceList_.size() == 0 ) probeDevices();
  for ( unsigned int i = 0; i < deviceList_.size(); i++ ) {
    if ( deviceList_[i].isDefaultOutput )
      return deviceList_[i].ID;
  }

  // If not found, find the first device with output channels, set it
  // as the default, and return the ID.
  for ( unsigned int i = 0; i < deviceList_.size(); i++ ) {
    if ( deviceList_[i].outputChannels > 0 ) {
      deviceList_[i].isDefaultOutput = true;
      return deviceList_[i].ID;
    }
  }

  return 0;
}

RtAudio::DeviceInfo RtApi :: getDeviceInfo( unsigned int deviceId )
{
  probeDevices();
  for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
    if ( deviceList_[m].ID == deviceId )
      return deviceList_[m];
  }

  errorText_ = "RtApi::getDeviceInfo: deviceId argument not found.";
  error( RTAUDIO_INVALID_PARAMETER );
  return RtAudio::DeviceInfo();
}

RtAudio::DeviceInfo RtApi :: getDeviceInfoNoProbe( unsigned int deviceId )
{
  for (unsigned int m = 0; m < deviceList_.size(); m++) {
    if (deviceList_[m].ID == deviceId)
      return deviceList_[m];
  }

  errorText_ = "RtApi::getDeviceInfo: deviceId argument not found.";
  error(RTAUDIO_INVALID_PARAMETER);
  return RtAudio::DeviceInfo();
}

RtAudio::DeviceInfo RtApi::getDeviceInfoByBusID(std::string busID)
{
    if (deviceList_.size() == 0) probeDevices();
    for (unsigned int m = 0; m < deviceList_.size(); m++) {
        if (deviceList_[m].busID== busID)
            return deviceList_[m];
    }

    errorText_ = "RtApi::getDeviceInfo: deviceId argument not found.";
    error(RTAUDIO_INVALID_PARAMETER);
    return RtAudio::DeviceInfo();
}

void RtApi :: closeStream( void )
{
  // MUST be implemented in subclasses!
  return;
}

bool RtApi :: probeDeviceOpen( unsigned int /*deviceId*/, StreamMode /*mode*/, unsigned int /*channels*/,
                               unsigned int /*firstChannel*/, unsigned int /*sampleRate*/,
                               RtAudioFormat /*format*/, unsigned int * /*bufferSize*/,
                               RtAudio::StreamOptions * /*options*/ )
{
  // MUST be implemented in subclasses!
  return FAILURE;
}

void RtApi :: tickStreamTime( void )
{
  // Subclasses that do not provide their own implementation of
  // getStreamTime should call this function once per buffer I/O to
  // provide basic stream time support.

  stream_.streamTime += ( stream_.bufferSize * 1.0 / stream_.sampleRate );

  /*
#if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
#endif
  */
}

RtAudioErrorType RtApi::registerExtraCallback(RtAudioDeviceCallback callback, void* userData)
{
    return RTAUDIO_UNKNOWN_ERROR;
}

RtAudioErrorType RtApi::unregisterExtraCallback()
{
    return RTAUDIO_UNKNOWN_ERROR;
}

long RtApi :: getStreamLatency( void )
{
  long totalLatency = 0;
  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX )
    totalLatency = stream_.latency[0];
  if ( stream_.mode == INPUT || stream_.mode == DUPLEX )
    totalLatency += stream_.latency[1];

  return totalLatency;
}

/*
double RtApi :: getStreamTime( void )
{
#if defined( HAVE_GETTIMEOFDAY )
  // Return a very accurate estimate of the stream time by
  // adding in the elapsed time since the last tick.
  struct timeval then;
  struct timeval now;

  if ( stream_.state != STREAM_RUNNING || stream_.streamTime == 0.0 )
    return stream_.streamTime;

  gettimeofday( &now, NULL );
  then = stream_.lastTickTimestamp;
  return stream_.streamTime +
    ((now.tv_sec + 0.000001 * now.tv_usec) -
     (then.tv_sec + 0.000001 * then.tv_usec));     
#else
  return stream_.streamTime;
  #endif
}
*/

void RtApi :: setStreamTime( double time )
{
  if ( time >= 0.0 )
    stream_.streamTime = time;
  /*
#if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
#endif
  */
}

unsigned int RtApi :: getStreamSampleRate( void )
{
  if ( isStreamOpen() ) return stream_.sampleRate;
  else return 0;
}


// *************************************************** //
//
// OS/API-specific methods.
//
// *************************************************** //

#if defined(__MACOSX_CORE__)

#include <unistd.h>

// The OS X CoreAudio API is designed to use a separate callback
// procedure for each of its audio devices.  A single RtAudio duplex
// stream using two different devices is supported here, though it
// cannot be guaranteed to always behave correctly because we cannot
// synchronize these two callbacks.
//
// A property listener is installed for over/underrun information.
// However, no functionality is currently provided to allow property
// listeners to trigger user handlers because it is unclear what could
// be done if a critical stream parameter (buffer size, sample rate,
// device disconnect) notification arrived.  The listeners entail
// quite a bit of extra code and most likely, a user program wouldn't
// be prepared for the result anyway.  However, we do provide a flag
// to the client callback function to inform of an over/underrun.

// A structure to hold various information related to the CoreAudio API
// implementation.
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

#if defined( MAC_OS_VERSION_12_0 ) && ( MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0 )
  #define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMain
#else
  #define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMaster // deprecated with macOS 12
#endif

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

unsigned int RtApiCore :: getDefaultOutputDevice( void )
{
  AudioDeviceID id;
  UInt32 dataSize = sizeof( AudioDeviceID );
  AudioObjectPropertyAddress property = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, KAUDIOOBJECTPROPERTYELEMENT };
  OSStatus result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, &dataSize, &id );
  if ( result != noErr ) {
    errorText_ = "RtApiCore::getDefaultOutputDevice: OS-X system error getting device.";
    error( RTAUDIO_SYSTEM_ERROR );
    return 0;
  }

  for ( unsigned int m=0; m<deviceIds_.size(); m++ ) {
    if ( deviceIds_[m] == id ) {
      if ( deviceList_[m].isDefaultOutput == false ) {
        deviceList_[m].isDefaultOutput = true;
        for ( unsigned int j=m+1; j<deviceIds_.size(); j++ ) {
          // make sure any remaining devices are not listed as the default
          deviceList_[j].isDefaultOutput = false;
        }
      }
      return deviceList_[m].ID;
    }
    deviceList_[m].isDefaultOutput = false;
  }

  // If not found above, then do system probe of devices and try again.
  probeDevices();
  for ( unsigned int m=0; m<deviceIds_.size(); m++ ) {
    if ( deviceIds_[m] == id ) return deviceList_[m].ID;
  }
  return 0;
}

unsigned int RtApiCore :: getDefaultInputDevice( void )
{
  AudioDeviceID id;
  UInt32 dataSize = sizeof( AudioDeviceID );
  AudioObjectPropertyAddress property = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, KAUDIOOBJECTPROPERTYELEMENT };
  OSStatus result = AudioObjectGetPropertyData( kAudioObjectSystemObject, &property, 0, NULL, &dataSize, &id );
  if ( result != noErr ) {
    errorText_ = "RtApiCore::getDefaultInputDevice: OS-X system error getting device.";
    error( RTAUDIO_SYSTEM_ERROR );
    return 0;
  }

  for ( unsigned int m=0; m<deviceIds_.size(); m++ ) {
    if ( deviceIds_[m] == id ) {
      if ( deviceList_[m].isDefaultInput == false ) {
        deviceList_[m].isDefaultInput = true;
        for ( unsigned int j=m+1; j<deviceIds_.size(); j++ ) {
          // make sure any remaining devices are not listed as the default
          deviceList_[j].isDefaultInput = false;
        }
      }
      return deviceList_[m].ID;
    }
    deviceList_[m].isDefaultInput = false;
  }

  // If not found above, then do system probe of devices and try again.
  probeDevices();
  for ( unsigned int m=0; m<deviceIds_.size(); m++ ) {
    if ( deviceIds_[m] == id ) return deviceList_[m].ID;
  }
  return 0;
}

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

void RtApiCore :: probeDevices( void )
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

bool RtApiCore :: probeDeviceInfo( AudioDeviceID id, RtAudio::DeviceInfo& info )
{
  // Get the device name.
  info.name.erase();
  CFStringRef cfname;
  UInt32 dataSize = sizeof( CFStringRef );
  AudioObjectPropertyAddress property = { kAudioObjectPropertyManufacturer,
                                          kAudioObjectPropertyScopeGlobal,
                                          KAUDIOOBJECTPROPERTYELEMENT };
  OSStatus result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &cfname );
  if ( result != noErr ) {
    errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting device manufacturer.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  long length = CFStringGetLength(cfname);
  char *mname = (char *)malloc(length * 3 + 1);
#if defined( UNICODE ) || defined( _UNICODE )
  CFStringGetCString(cfname, mname, length * 3 + 1, kCFStringEncodingUTF8);
#else
  CFStringGetCString(cfname, mname, length * 3 + 1, CFStringGetSystemEncoding());
#endif
  info.name.append( (const char *)mname, strlen(mname) );
  info.name.append( ": " );
  CFRelease( cfname );
  free(mname);

  property.mSelector = kAudioObjectPropertyName;
  result = AudioObjectGetPropertyData( id, &property, 0, NULL, &dataSize, &cfname );
  if ( result != noErr ) {
    errorStream_ << "RtApiCore::probeDeviceInfo: system error (" << getErrorCode( result ) << ") getting device name.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  length = CFStringGetLength(cfname);
  char *name = (char *)malloc(length * 3 + 1);
#if defined( UNICODE ) || defined( _UNICODE )
  CFStringGetCString(cfname, name, length * 3 + 1, kCFStringEncodingUTF8);
#else
  CFStringGetCString(cfname, name, length * 3 + 1, CFStringGetSystemEncoding());
#endif
  info.name.append( (const char *)name, strlen(name) );
  CFRelease( cfname );
  free(name);

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

bool RtApiCore :: probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                                   unsigned int firstChannel, unsigned int sampleRate,
                                   RtAudioFormat format, unsigned int *bufferSize,
                                   RtAudio::StreamOptions *options )
{
  AudioDeviceID id = 0;
  for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
    if ( deviceList_[m].ID == deviceId ) {
      id = deviceIds_[m];
      break;
    }
  }

  if ( id == 0 ) {
    errorText_ = "RtApiCore::probeDeviceOpen: the device ID was not found!";
    return FAILURE;
  }

  AudioObjectPropertyAddress property = { kAudioHardwarePropertyDevices,
                                          kAudioDevicePropertyScopeOutput,
                                          KAUDIOOBJECTPROPERTYELEMENT };

  // Setup for stream mode.
  if ( mode == INPUT ) {
    property.mScope = kAudioDevicePropertyScopeInput;
  }

  // Get the stream "configuration".
  AudioBufferList	*bufferList = nil;
  UInt32 dataSize = 0;
  property.mSelector = kAudioDevicePropertyStreamConfiguration;
  OSStatus result = AudioObjectGetPropertyDataSize( id, &property, 0, NULL, &dataSize );
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
#else
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

  /*
  #if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
  #endif
  */

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
                       stream_.userBuffer[0], stream_.convertInfo[0] );
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
        convertBuffer( stream_.deviceBuffer, stream_.userBuffer[0], stream_.convertInfo[0] );
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
                       stream_.convertInfo[1] );
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
                       stream_.convertInfo[1] );
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

  //******************** End of __MACOSX_CORE__ *********************//
#endif

#if defined(__UNIX_JACK__)

// JACK is a low-latency audio server, originally written for the
// GNU/Linux operating system and now also ported to OS-X and
// Windows. It can connect a number of different applications to an
// audio device, as well as allowing them to share audio between
// themselves.
//
// When using JACK with RtAudio, "devices" refer to JACK clients that
// have ports connected to the server, while ports correspond to device
// channels.  The JACK server is typically started  in a terminal as
// follows:
//
// .jackd -d alsa -d hw:0
//
// or through an interface program such as qjackctl.  Many of the
// parameters normally set for a stream are fixed by the JACK server
// and can be specified when the JACK server is started.  In
// particular,
//
// .jackd -d alsa -d hw:0 -r 44100 -p 512 -n 4
//
// specifies a sample rate of 44100 Hz, a buffer size of 512 sample
// frames, and number of buffers = 4.  Once the server is running, it
// is not possible to override these values.  If the values are not
// specified in the command-line, the JACK server uses default values.
//
// The JACK server does not have to be running when an instance of
// RtApiJack is created, though the function getDeviceCount() will
// report 0 devices found until JACK has been started.  When no
// devices are available (i.e., the JACK server is not running), a
// stream cannot be opened.

#include <unistd.h>
#include <cstdio>

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
  //******************** End of __UNIX_JACK__ *********************//
#endif

#if defined(__LINUX_ALSA__)

#include <alsa/asoundlib.h>
#include <unistd.h>

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

static void *alsaCallbackHandler( void * ptr );

RtApiAlsa :: RtApiAlsa()
{
  // Nothing to do here.
}

RtApiAlsa :: ~RtApiAlsa()
{
  if ( stream_.state != STREAM_CLOSED ) closeStream();
}

void RtApiAlsa :: probeDevices( void )
{
  // See list of required functionality in RtApi::probeDevices().
  
  int result, device, card;
  char name[128];
  snd_ctl_t *handle = 0;
  snd_ctl_card_info_t *ctlinfo;
  snd_pcm_info_t *pcminfo;
  snd_ctl_card_info_alloca(&ctlinfo);
  snd_pcm_info_alloca(&pcminfo);
  // First element isthe device hw ID, second is the device "pretty name"
  std::vector<std::pair<std::string, std::string>> deviceID_prettyName;
  snd_pcm_stream_t stream;
  std::string defaultDeviceName;

  // Add the default interface if available.
  result = snd_ctl_open( &handle, "default", 0 );
  if (result == 0) {
    deviceID_prettyName.push_back({"default", "Default ALSA Device"});
    defaultDeviceName = deviceID_prettyName[0].second;
    snd_ctl_close( handle );
  }

  // Add the Pulse interface if available.
  result = snd_ctl_open( &handle, "pulse", 0 );
  if (result == 0) {
    deviceID_prettyName.push_back({"pulse",  "PulseAudio Sound Server"});
    snd_ctl_close( handle );
  }
  
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
      stream = SND_PCM_STREAM_PLAYBACK;
      snd_pcm_info_set_stream( pcminfo, stream );
      result = snd_ctl_pcm_info( handle, pcminfo );
      if ( result < 0 ) {
        if ( result == -ENOENT ) { // try as input stream
          stream = SND_PCM_STREAM_CAPTURE;
          snd_pcm_info_set_stream( pcminfo, stream );
          result = snd_ctl_pcm_info( handle, pcminfo );
          if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::probeDevices: control pcm info, card = " << card << ", device = " << device << ", " << snd_strerror( result ) << ".";
            errorText_ = errorStream_.str();
            error( RTAUDIO_WARNING );
            continue;
          }
        }
        else continue;
      }
      sprintf( name, "hw:%s,%d", snd_ctl_card_info_get_id(ctlinfo), device );
      std::string id(name);
      sprintf( name, "%s (%s)", snd_ctl_card_info_get_name(ctlinfo), snd_pcm_info_get_id(pcminfo) );
      std::string prettyName(name);
      deviceID_prettyName.push_back( {id, prettyName} );
      if ( card == 0 && device == 0 && defaultDeviceName.empty() )
        defaultDeviceName = name;
    }
  nextcard:
    if ( handle )
      snd_ctl_close( handle );
    snd_card_next( &card );
  }

  if ( deviceID_prettyName.size() == 0 ) {
    deviceList_.clear();
    deviceIdPairs_.clear();
    return;
  }

  // Clean removed devices
  for ( auto it = deviceIdPairs_.begin(); it != deviceIdPairs_.end(); ) {
    bool found = false;
    for ( auto& d: deviceID_prettyName ) {
      if ( d.first == (*it).first ) {
        found = true;
        break;
      }
    }

    if ( found )
      ++it;
    else
      it = deviceIdPairs_.erase(it);
  }

  // Fill or update the deviceList_ and also save a corresponding list of Ids.
  for ( auto& d : deviceID_prettyName ) {
    bool found = false;
    for ( auto& dID : deviceIdPairs_ ) {
      if ( d.first == dID.first ) {
        found = true;
        break; // We already have this device.
      }
    }

    if ( found )
      continue;

    // new device
    RtAudio::DeviceInfo info;
    info.name = d.second;
    info.busID = d.first;
    if ( probeDeviceInfo( info, d.first ) == false ) continue; // ignore if probe fails
    info.ID = currentDeviceId_++;  // arbitrary internal device ID
    if ( info.name == defaultDeviceName ) {
      if ( info.outputChannels > 0 ) info.isDefaultOutput = true;
      if ( info.inputChannels > 0 ) info.isDefaultInput = true;
    }
    deviceList_.push_back( info );
    deviceIdPairs_.push_back({d.first, info.ID});
    // I don't see that ALSA provides property listeners to know if
    // devices are removed or added.
  }

  // Remove any devices left in the list that are no longer available.
  for ( std::vector<RtAudio::DeviceInfo>::iterator it=deviceList_.begin(); it!=deviceList_.end(); )
  {
    auto itID = deviceIdPairs_.begin();
    while ( itID != deviceIdPairs_.end() ) {
      if ( (*it).ID == (*itID).second ) {
        break;
      }
      ++itID;
    }

    if ( itID == deviceIdPairs_.end() ) {
      // not found so remove it from our list
      it = deviceList_.erase( it );
    }
    else
      ++it;
  }
}

bool RtApiAlsa :: probeDeviceInfo( RtAudio::DeviceInfo& info, std::string name )
{
  int result, openMode = SND_PCM_ASYNC;
  snd_pcm_stream_t stream;
  snd_pcm_t *phandle;
  snd_pcm_hw_params_t *params;
  snd_pcm_hw_params_alloca( &params );

  // First try for playback
  stream = SND_PCM_STREAM_PLAYBACK;
  result = snd_pcm_open( &phandle, name.c_str(), stream, openMode | SND_PCM_NONBLOCK );
  if ( result < 0 ) {
    if ( result == -16 ) return false; // device busy ... can't probe or use
    if ( result != -2 ) { // device doesn't support playback
      errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open (playback) error for device (" << name << "), " << snd_strerror( result ) << ".";
      errorText_ = errorStream_.str();
      error( RTAUDIO_WARNING );
    }
    goto captureProbe;
  }

  // The device is open ... fill the parameter structure.
  result = snd_pcm_hw_params_any( phandle, params );
  if ( result < 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << name << "), " << snd_strerror( result ) << ".";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto captureProbe;
  }

  // Get output channel information.
  unsigned int value;
  result = snd_pcm_hw_params_get_channels_max( params, &value );
  if ( result < 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting device (" << name << ") output channels, " << snd_strerror( result ) << ".";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto captureProbe;
  }
  info.outputChannels = value;
  snd_pcm_close( phandle );

 captureProbe:
  stream = SND_PCM_STREAM_CAPTURE;
  result = snd_pcm_open( &phandle, name.c_str(), stream, openMode | SND_PCM_NONBLOCK);
  if ( result < 0 && result ) {
    if ( result != -2 && result != -16 ) { // device busy or doesn't support capture
      errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open (capture) error for device (" << name << "), " << snd_strerror( result ) << ".";
      errorText_ = errorStream_.str();
      error( RTAUDIO_WARNING );
    }
    if ( info.outputChannels == 0 ) return false;
    goto probeParameters;
  }

  // The device is open ... fill the parameter structure.
  result = snd_pcm_hw_params_any( phandle, params );
  if ( result < 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << name << "), " << snd_strerror( result ) << ".";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    if ( info.outputChannels == 0 ) return false;
    goto probeParameters;
  }

  result = snd_pcm_hw_params_get_channels_max( params, &value );
  if ( result < 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting device (" << name << ") input channels, " << snd_strerror( result ) << ".";
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

  result = snd_pcm_open( &phandle, name.c_str(), stream, openMode | SND_PCM_NONBLOCK);
  if ( result < 0 ) {
    errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_open error for device (" << name << "), " << snd_strerror( result ) << ".";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  // The device is open ... fill the parameter structure.
  result = snd_pcm_hw_params_any( phandle, params );
  if ( result < 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << name << "), " << snd_strerror( result ) << ".";
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
    errorStream_ << "RtApiAlsa::probeDeviceInfo: no supported sample rates found for device (" << name << ").";
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

  // Check that we have at least one supported format
  if ( info.nativeFormats == 0 ) {
    snd_pcm_close( phandle );
    errorStream_ << "RtApiAlsa::probeDeviceInfo: pcm device (" << name << ") data format not supported by RtAudio.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  // Close the device and return
  snd_pcm_close( phandle );
  return true;
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
  for ( auto& id : deviceIdPairs_) {
    if ( id.second == deviceId ) {
      name = id.first;
      break;
    }
  }

  snd_pcm_stream_t stream;
  if ( mode == OUTPUT )
    stream = SND_PCM_STREAM_PLAYBACK;
  else
    stream = SND_PCM_STREAM_CAPTURE;

  snd_pcm_t *phandle;
  int openMode = SND_PCM_ASYNC;
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
  if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
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
  if ( stream_.state != STREAM_RUNNING ) {
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
  doStopStream = callback( stream_.userBuffer[0], stream_.userBuffer[1],
                           stream_.bufferSize, streamTime, status, stream_.callbackInfo.userData );

  if ( doStopStream == 2 ) {
    abortStream();
    return;
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
    if ( stream_.deviceInterleaved[1] )
      result = snd_pcm_readi( handle[1], buffer, stream_.bufferSize );
    else {
      void *bufs[channels];
      size_t offset = stream_.bufferSize * formatBytes( format );
      for ( int i=0; i<channels; i++ )
        bufs[i] = (void *) (buffer + (i * offset));
      result = snd_pcm_readn( handle[1], bufs, stream_.bufferSize );
    }

    if ( result < (int) stream_.bufferSize ) {
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
      }
      else {
        errorStream_ << "RtApiAlsa::callbackEvent: audio read error, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
      }
      error( RTAUDIO_WARNING );
      goto tryOutput;
    }

    // Do byte swapping if necessary.
    if ( stream_.doByteSwap[1] )
      byteSwapBuffer( buffer, stream_.bufferSize * channels, format );

    // Do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[1] )
      convertBuffer( stream_.userBuffer[1], stream_.deviceBuffer, stream_.convertInfo[1] );

    // Check stream latency
    result = snd_pcm_delay( handle[1], &frames );
    if ( result == 0 && frames > 0 ) stream_.latency[1] = frames;
  }

 tryOutput:

  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

    // Setup parameters and do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[0] ) {
      buffer = stream_.deviceBuffer;
      convertBuffer( buffer, stream_.userBuffer[0], stream_.convertInfo[0] );
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
      byteSwapBuffer(buffer, stream_.bufferSize * channels, format);

    // Write samples to device in interleaved/non-interleaved format.
    if ( stream_.deviceInterleaved[0] )
      result = snd_pcm_writei( handle[0], buffer, stream_.bufferSize );
    else {
      void *bufs[channels];
      size_t offset = stream_.bufferSize * formatBytes( format );
      for ( int i=0; i<channels; i++ )
        bufs[i] = (void *) (buffer + (i * offset));
      result = snd_pcm_writen( handle[0], bufs, stream_.bufferSize );
    }

    if ( result < (int) stream_.bufferSize ) {
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
      }
      else {
        errorStream_ << "RtApiAlsa::callbackEvent: audio write error, " << snd_strerror( result ) << ".";
        errorText_ = errorStream_.str();
      }
      error( RTAUDIO_WARNING );
      goto unlock;
    }

    // Check stream latency
    result = snd_pcm_delay( handle[0], &frames );
    if ( result == 0 && frames > 0 ) stream_.latency[0] = frames;
  }

 unlock:
  MUTEX_UNLOCK( &stream_.mutex );

  RtApi::tickStreamTime();
  if ( doStopStream == 1 ) this->stopStream();
}

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

//******************** End of __LINUX_ALSA__ *********************//
#endif

#if defined(__LINUX_PULSE__)

// Code written by Peter Meerwald, pmeerw@pmeerw.net and Tristan Matthews.
// Updated by Gary Scavone, 2021.

#include <pulse/error.h>
#include <pulse/simple.h>
#include <cstdio>

// A structure needed to pass variables for device probing.
struct PaDeviceProbeInfo {
  pa_mainloop_api *paMainLoopApi;
  std::string defaultSinkName;
  std::string defaultSourceName;
  int defaultRate;
  unsigned int *currentDeviceId;
  std::vector< std::string > deviceNames;
  std::vector< RtApiPulse::PaDeviceInfo > *paDeviceList;
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
  std::string name = pa_proplist_gets( i->proplist, "device.description" );
  paProbeInfo->deviceNames.push_back( name );
  for ( size_t n=0; n<paProbeInfo->rtDeviceList->size(); n++ )
    if ( paProbeInfo->rtDeviceList->at(n).name == name ) return; // we've already probed this one
  
  RtAudio::DeviceInfo info;
  info.name = name;
  // TODO get device id for Pulse devices
  info.busID = "";
  info.outputChannels = i->sample_spec.channels;
  info.preferredSampleRate = i->sample_spec.rate;
  info.isDefaultOutput = ( paProbeInfo->defaultSinkName == i->name );
  for ( const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr )
    info.sampleRates.push_back( *sr );
  for ( const rtaudio_pa_format_mapping_t *fm = supported_sampleformats; fm->rtaudio_format; ++fm )
    info.nativeFormats |= fm->rtaudio_format;
  info.ID = *(paProbeInfo->currentDeviceId);
  *(paProbeInfo->currentDeviceId) = info.ID + 1;
  paProbeInfo->rtDeviceList->push_back( info );

  RtApiPulse::PaDeviceInfo painfo;
  painfo.sinkName = i->name;
  paProbeInfo->paDeviceList->push_back( painfo );
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

  std::string name = pa_proplist_gets( i->proplist, "device.description" );
  paProbeInfo->deviceNames.push_back( name );
  for ( size_t n=0; n<paProbeInfo->rtDeviceList->size(); n++ ) {
    if ( paProbeInfo->rtDeviceList->at(n).name == name ) {
      // Check if we've already probed this as an output.
      if ( !paProbeInfo->paDeviceList->at(n).sinkName.empty() ) {
        // This must be a duplex device. Update the device info.
        paProbeInfo->paDeviceList->at(n).sourceName = i->name;
        paProbeInfo->rtDeviceList->at(n).inputChannels = i->sample_spec.channels;
        paProbeInfo->rtDeviceList->at(n).isDefaultInput = ( paProbeInfo->defaultSourceName == i->name );
        paProbeInfo->rtDeviceList->at(n).duplexChannels = 
          (paProbeInfo->rtDeviceList->at(n).inputChannels < paProbeInfo->rtDeviceList->at(n).outputChannels)
          ? paProbeInfo->rtDeviceList->at(n).inputChannels : paProbeInfo->rtDeviceList->at(n).outputChannels;
      }
      return; // we already have this
    }
  }

  RtAudio::DeviceInfo info;
  info.name = name;
  info.inputChannels = i->sample_spec.channels;
  info.preferredSampleRate = i->sample_spec.rate;
  info.isDefaultInput = ( paProbeInfo->defaultSourceName == i->name );
  for ( const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr )
    info.sampleRates.push_back( *sr );
  for ( const rtaudio_pa_format_mapping_t *fm = supported_sampleformats; fm->rtaudio_format; ++fm )
    info.nativeFormats |= fm->rtaudio_format;
  info.ID = *(paProbeInfo->currentDeviceId);
  *(paProbeInfo->currentDeviceId) = info.ID + 1;
  paProbeInfo->rtDeviceList->push_back( info );

  RtApiPulse::PaDeviceInfo painfo;
  painfo.sourceName = i->name;
  paProbeInfo->paDeviceList->push_back( painfo );
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

RtApiPulse::~RtApiPulse()
{
  if ( stream_.state != STREAM_CLOSED )
    closeStream();
}

void RtApiPulse :: probeDevices( void )
{
  // See list of required functionality in RtApi::probeDevices().
  
  pa_mainloop *ml = NULL;
  pa_context *context = NULL;
  char *server = NULL;
  int ret = 1;
  PaDeviceProbeInfo paProbeInfo;
  if (!(ml = pa_mainloop_new())) {
    errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_new() failed.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto quit;
  }

  paProbeInfo.paMainLoopApi = pa_mainloop_get_api( ml );
  paProbeInfo.currentDeviceId = &currentDeviceId_;
  paProbeInfo.paDeviceList = &paDeviceList_;
  paProbeInfo.rtDeviceList = &deviceList_;

  if (!(context = pa_context_new_with_proplist( paProbeInfo.paMainLoopApi, NULL, NULL ))) {
    errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto quit;
  }

  pa_context_set_state_callback( context, rt_pa_context_state_callback, &paProbeInfo );
  
  if (pa_context_connect( context, server, PA_CONTEXT_NOFLAGS, NULL ) < 0) {
    errorStream_ << "RtApiPulse::probeDevices: pa_context_connect() failed: "
      << pa_strerror(pa_context_errno(context));
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto quit;
  }

  if (pa_mainloop_run( ml, &ret ) < 0) {
    errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_run() failed.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto quit;
  }

  if (ret != 0) {
    errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    goto quit;
  }

  // Check for devices that have been unplugged.
  unsigned int m;
  for ( std::vector<RtAudio::DeviceInfo>::iterator it=deviceList_.begin(); it!=deviceList_.end(); ) {
    for ( m=0; m<paProbeInfo.deviceNames.size(); m++ ) {
      if ( (*it).name == paProbeInfo.deviceNames[m] ) {
        ++it;
        break;
      }
    }
    if ( m == paProbeInfo.deviceNames.size() ) { // not found so remove it from our list
      it = deviceList_.erase( it );
      paDeviceList_.erase( paDeviceList_.begin() + distance(deviceList_.begin(), it ) );
    }
  }
  
quit:
  if (context)
    pa_context_unref(context);

  if (ml)
    pa_mainloop_free(ml);

  pa_xfree(server);
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

bool RtApiPulse::probeDeviceOpen( unsigned int deviceId, StreamMode mode,
                                  unsigned int channels, unsigned int firstChannel,
                                  unsigned int sampleRate, RtAudioFormat format,
                                  unsigned int *bufferSize, RtAudio::StreamOptions *options )
{
  PulseAudioHandle *pah = 0;
  unsigned long bufferBytes = 0;
  pa_sample_spec ss;

  int deviceIdx = -1;
  for ( unsigned int m=0; m<deviceList_.size(); m++ ) {
    if ( deviceList_[m].ID == deviceId ) {
      deviceIdx = m;
      break;
    }
  }

  if ( deviceIdx < 0 ) return false;

  if ( firstChannel != 0 ) {
    errorText_ = "PulseAudio does not support channel offset mapping.";
    return false;
  }

  // These may be NULL for default devices but we already have the names.
  const char *dev_input = NULL;
  const char *dev_output = NULL;
  if ( !paDeviceList_[deviceIdx].sourceName.empty() )
    dev_input = paDeviceList_[deviceIdx].sourceName.c_str();
  if ( !paDeviceList_[deviceIdx].sinkName.empty() )
    dev_output = paDeviceList_[deviceIdx].sinkName.c_str();

  if ( mode==INPUT && deviceList_[deviceIdx].inputChannels < channels ) {
    errorText_ = "PulseAudio device does not support requested input channel count.";
    return false;
  }
  if ( mode==OUTPUT && deviceList_[deviceIdx].outputChannels < channels ) {
    errorText_ = "PulseAudio device does not support requested output channel count.";
    return false;
  }

  ss.channels = channels;

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
    goto error;
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
        goto error;
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
      goto error;
    }

    stream_.apiHandle = pah;
    if ( pthread_cond_init( &pah->runnable_cv, NULL ) != 0 ) {
      errorText_ = "RtApiPulse::probeDeviceOpen: error creating condition variable.";
      goto error;
    }
  }
  pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

  int error;
  struct pa_channel_map mapping;
  mapping = {};
  if (pa_channel_map_init_extend(&mapping, ss.channels, PA_CHANNEL_MAP_WAVEEX) == NULL){
      errorText_ = "RtApiPulse::probeDeviceOpen: error mapping.";
      goto error;
  }

  if ( options && !options->streamName.empty() ) streamName = options->streamName;
  switch ( mode ) {
    pa_buffer_attr buffer_attr;
  case INPUT:
    buffer_attr.fragsize = bufferBytes;
    if ( options && options->numberOfBuffers > 0 )
      buffer_attr.maxlength = bufferBytes * (options->numberOfBuffers + 1);
    else
      buffer_attr.maxlength = bufferBytes * 4;

    pah->s_rec = pa_simple_new( NULL, streamName.c_str(), PA_STREAM_RECORD,
                                dev_input, "Record", &ss, &mapping, &buffer_attr, &error );
    if ( !pah->s_rec ) {
      errorText_ = "RtApiPulse::probeDeviceOpen: error connecting input to PulseAudio server.";
      goto error;
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
                                 dev_output, "Playback", &ss, &mapping, attr_ptr, &error );
    if ( !pah->s_play ) {
      errorText_ = "RtApiPulse::probeDeviceOpen: error connecting output to PulseAudio server.";
      goto error;
    }
    break;
  }
  case DUPLEX:
    /* Note: We could add DUPLEX by synchronizing multiple streams,
       but it would mean moving from Simple API to Asynchronous API:
       https://freedesktop.org/software/pulseaudio/doxygen/streams.html#sync_streams */
    errorText_ = "RtApiPulse::probeDeviceOpen: duplex not supported for PulseAudio.";
    goto error;
  default:
    goto error;
  }

  if ( stream_.mode == UNINITIALIZED )
    stream_.mode = mode;
  else if ( stream_.mode == mode )
    goto error;
  else
    stream_.mode = DUPLEX;

  if ( !stream_.callbackInfo.isRunning ) {
    stream_.callbackInfo.object = this;
    
    stream_.state = STREAM_STOPPED;
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
    int result = pthread_create( &pah->thread, &attr, pulseaudio_callback, (void *)&stream_.callbackInfo);
    pthread_attr_destroy(&attr);
    if(result != 0) {
      // Failed. Try instead with default attributes.
      result = pthread_create( &pah->thread, NULL, pulseaudio_callback, (void *)&stream_.callbackInfo);
      if(result != 0) {
        stream_.callbackInfo.isRunning = false;
        errorText_ = "RtApiPulse::probeDeviceOpen: error creating thread.";
        goto error;
      }
    }
  }

  return SUCCESS;
 
 error:
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
  return FAILURE;
}

void RtApiPulse::closeStream( void )
{
  PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

  stream_.callbackInfo.isRunning = false;
  if ( pah ) {
    MUTEX_LOCK( &stream_.mutex );
    if ( stream_.state == STREAM_STOPPED ) {
      pah->runnable = true;
      pthread_cond_signal( &pah->runnable_cv );
    }
    MUTEX_UNLOCK( &stream_.mutex );

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

  if ( stream_.state == STREAM_STOPPED ) {
    MUTEX_LOCK( &stream_.mutex );
    while ( !pah->runnable )
      pthread_cond_wait( &pah->runnable_cv, &stream_.mutex );

    if ( stream_.state != STREAM_RUNNING ) {
      MUTEX_UNLOCK( &stream_.mutex );
      return;
    }
    MUTEX_UNLOCK( &stream_.mutex );
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
  int doStopStream = callback( stream_.userBuffer[OUTPUT], stream_.userBuffer[INPUT],
                               stream_.bufferSize, streamTime, status,
                               stream_.callbackInfo.userData );

  if ( doStopStream == 2 ) {
    abortStream();
    return;
  }

  MUTEX_LOCK( &stream_.mutex );
  void *pulse_in = stream_.doConvertBuffer[INPUT] ? stream_.deviceBuffer : stream_.userBuffer[INPUT];
  void *pulse_out = stream_.doConvertBuffer[OUTPUT] ? stream_.deviceBuffer : stream_.userBuffer[OUTPUT];

  if ( stream_.state != STREAM_RUNNING )
    goto unlock;

  int pa_error;
  size_t bytes;
  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
    if ( stream_.doConvertBuffer[OUTPUT] ) {
        convertBuffer( stream_.deviceBuffer,
                       stream_.userBuffer[OUTPUT],
                       stream_.convertInfo[OUTPUT] );
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
    }
    if ( stream_.doConvertBuffer[INPUT] ) {
      convertBuffer( stream_.userBuffer[INPUT],
                     stream_.deviceBuffer,
                     stream_.convertInfo[INPUT] );
    }
  }

 unlock:
  MUTEX_UNLOCK( &stream_.mutex );
  RtApi::tickStreamTime();

  if ( doStopStream == 1 )
    stopStream();
}

RtAudioErrorType RtApiPulse::startStream( void )
{
  if ( stream_.state != STREAM_STOPPED ) {
    if ( stream_.state == STREAM_RUNNING )
      errorText_ = "RtApiPulse::startStream(): the stream is already running!";
    else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiPulse::startStream(): the stream is stopping or closed!";
    return error( RTAUDIO_WARNING );
  }
  
  PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

  MUTEX_LOCK( &stream_.mutex );

  /*
  #if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
  #endif
  */
  
  stream_.state = STREAM_RUNNING;

  pah->runnable = true;
  pthread_cond_signal( &pah->runnable_cv );
  MUTEX_UNLOCK( &stream_.mutex );
  return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiPulse::stopStream( void )
{
  if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
    if ( stream_.state == STREAM_STOPPED )
      errorText_ = "RtApiPulse::stopStream(): the stream is already stopped!";
    else if ( stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiPulse::stopStream(): the stream is closed!";
    return error( RTAUDIO_WARNING );
  }
    
  PulseAudioHandle *pah = static_cast<PulseAudioHandle *>( stream_.apiHandle );

  stream_.state = STREAM_STOPPED;
  MUTEX_LOCK( &stream_.mutex );

  if ( pah ) {
    pah->runnable = false;
    if ( pah->s_play ) {
      int pa_error;
      if ( pa_simple_drain( pah->s_play, &pa_error ) < 0 ) {
        errorStream_ << "RtApiPulse::stopStream: error draining output device, " <<
          pa_strerror( pa_error ) << ".";
        errorText_ = errorStream_.str();
        MUTEX_UNLOCK( &stream_.mutex );
        return error( RTAUDIO_SYSTEM_ERROR );
      }
    }
  }

  stream_.state = STREAM_STOPPED;
  MUTEX_UNLOCK( &stream_.mutex );
  return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiPulse::abortStream( void )
{
  if ( stream_.state != STREAM_RUNNING ) {
    if ( stream_.state == STREAM_STOPPED )
      errorText_ = "RtApiPulse::abortStream(): the stream is already stopped!";
    else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiPulse::abortStream(): the stream is stopping or closed!";
    return error( RTAUDIO_WARNING );
  }
  
  PulseAudioHandle *pah = static_cast<PulseAudioHandle*>( stream_.apiHandle );

  stream_.state = STREAM_STOPPED;
  MUTEX_LOCK( &stream_.mutex );

  if ( pah ) {
    pah->runnable = false;
    if ( pah->s_play ) {
      int pa_error;
      if ( pa_simple_flush( pah->s_play, &pa_error ) < 0 ) {
        errorStream_ << "RtApiPulse::abortStream: error flushing output device, " <<
          pa_strerror( pa_error ) << ".";
        errorText_ = errorStream_.str();
        MUTEX_UNLOCK( &stream_.mutex );
        return error( RTAUDIO_SYSTEM_ERROR );
      }
    }
  }

  stream_.state = STREAM_STOPPED;
  MUTEX_UNLOCK( &stream_.mutex );
  return RTAUDIO_NO_ERROR;
}

//******************** End of __LINUX_PULSE__ *********************//
#endif

#if defined(__LINUX_OSS__)

#include <unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

static void *ossCallbackHandler(void * ptr);

// A structure to hold various information related to the OSS API
// implementation.
struct OssHandle {
  int id[2];    // device ids
  bool xrun[2];
  bool triggered;
  pthread_cond_t runnable;

  OssHandle()
    :triggered(false) { id[0] = 0; id[1] = 0; xrun[0] = false; xrun[1] = false; }
};

RtApiOss :: RtApiOss()
{
  // Nothing to do here.
}

RtApiOss :: ~RtApiOss()
{
  if ( stream_.state != STREAM_CLOSED ) closeStream();
}

void RtApiOss :: probeDevices( void )
{
  // See list of required functionality in RtApi::probeDevices().
  
  int mixerfd = open( "/dev/mixer", O_RDWR, 0 );
  if ( mixerfd == -1 ) {
    errorText_ = "RtApiOss::probeDevices: error opening '/dev/mixer'.";
    error( RTAUDIO_SYSTEM_ERROR );
    return;
  }

  oss_sysinfo sysinfo;
  if ( ioctl( mixerfd, SNDCTL_SYSINFO, &sysinfo ) == -1 ) {
    close( mixerfd );
    errorText_ = "RtApiOss::probeDevices: error getting sysinfo, OSS version >= 4.0 is required.";
    error( RTAUDIO_SYSTEM_ERROR );
    return;
  }

  unsigned int nDevices = sysinfo.numaudios;
  if ( nDevices == 0 ) {
    close( mixerfd );
    deviceList_.clear();
    return;
  }

  oss_audioinfo ainfo;
  unsigned int m, n;
  std::vector<std::string> deviceNames;
  for ( n=0; n<nDevices; n++ ) {
    ainfo.dev = n;
    if ( ioctl( mixerfd, SNDCTL_AUDIOINFO, &ainfo ) == -1 ) continue;
    deviceNames.push_back( ainfo.name );
    for ( m=0; m<deviceList_.size(); m++ ) {
      if ( deviceList_[m].name == deviceNames.back() )
        break; // We already have this device.
    }
    if ( m == deviceList_.size() ) { // new device
      RtAudio::DeviceInfo info;
      if ( probeDeviceInfo( info, ainfo ) == false ) continue; // ignore if probe fails
      info.ID = currentDeviceId_++;  // arbitrary internal device ID
      deviceList_.push_back( info );
    }
  }
  close( mixerfd );

  // Remove any devices left in the list that are no longer available.
  for ( std::vector<RtAudio::DeviceInfo>::iterator it=deviceList_.begin(); it!=deviceList_.end(); ) {
    for ( m=0; m<deviceNames.size(); m++ ) {
      if ( (*it).name == deviceNames[m] ) {
        ++it;
        break;
      }
    }
    if ( m == deviceNames.size() ) // not found so remove it from our list
      it = deviceList_.erase( it );
  }

  // I don't think the OSS API supports default devices. Our parent
  // class versions of the getDefault functions will return the first
  // one found.
}

bool RtApiOss :: probeDeviceInfo( RtAudio::DeviceInfo &info, oss_audioinfo &ainfo )
{
  // Probe channels
  if ( ainfo.caps & PCM_CAP_OUTPUT ) info.outputChannels = ainfo.max_channels;
  if ( ainfo.caps & PCM_CAP_INPUT ) info.inputChannels = ainfo.max_channels;
  if ( ainfo.caps & PCM_CAP_DUPLEX ) {
    if ( info.outputChannels > 0 && info.inputChannels > 0 && ainfo.caps & PCM_CAP_DUPLEX )
      info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;
  }

  // Probe data formats ... do for input
  unsigned long mask = ainfo.iformats;
  if ( mask & AFMT_S16_LE || mask & AFMT_S16_BE )
    info.nativeFormats |= RTAUDIO_SINT16;
  if ( mask & AFMT_S8 )
    info.nativeFormats |= RTAUDIO_SINT8;
  if ( mask & AFMT_S32_LE || mask & AFMT_S32_BE )
    info.nativeFormats |= RTAUDIO_SINT32;
#ifdef AFMT_FLOAT
  if ( mask & AFMT_FLOAT )
    info.nativeFormats |= RTAUDIO_FLOAT32;
#endif
  if ( mask & AFMT_S24_LE || mask & AFMT_S24_BE )
    info.nativeFormats |= RTAUDIO_SINT24;

  // Check that we have at least one supported format
  if ( info.nativeFormats == 0 ) {
    errorStream_ << "RtApiOss::probeDeviceInfo: device (" << ainfo.name << ") data format not supported by RtAudio.";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  // Probe the supported sample rates.
  info.sampleRates.clear();
  if ( ainfo.nrates ) {
    for ( unsigned int i=0; i<ainfo.nrates; i++ ) {
      for ( unsigned int k=0; k<MAX_SAMPLE_RATES; k++ ) {
        if ( ainfo.rates[i] == SAMPLE_RATES[k] ) {
          info.sampleRates.push_back( SAMPLE_RATES[k] );

          if ( !info.preferredSampleRate || ( SAMPLE_RATES[k] <= 48000 && SAMPLE_RATES[k] > info.preferredSampleRate ) )
            info.preferredSampleRate = SAMPLE_RATES[k];

          break;
        }
      }
    }
  }
  else {
    // Check min and max rate values;
    for ( unsigned int k=0; k<MAX_SAMPLE_RATES; k++ ) {
      if ( ainfo.min_rate <= (int) SAMPLE_RATES[k] && ainfo.max_rate >= (int) SAMPLE_RATES[k] ) {
        info.sampleRates.push_back( SAMPLE_RATES[k] );

        if ( !info.preferredSampleRate || ( SAMPLE_RATES[k] <= 48000 && SAMPLE_RATES[k] > info.preferredSampleRate ) )
          info.preferredSampleRate = SAMPLE_RATES[k];
      }
    }
  }

  if ( info.sampleRates.size() == 0 ) {
    errorStream_ << "RtApiOss::probeDeviceInfo: no supported sample rates found for device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    error( RTAUDIO_WARNING );
    return false;
  }

  return true;
}


bool RtApiOss :: probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                                  unsigned int firstChannel, unsigned int sampleRate,
                                  RtAudioFormat format, unsigned int *bufferSize,
                                  RtAudio::StreamOptions *options )
{
  int mixerfd = open( "/dev/mixer", O_RDWR, 0 );
  if ( mixerfd == -1 ) {
    errorText_ = "RtApiOss::probeDeviceOpen: error opening '/dev/mixer'.";
    return FAILURE;
  }

  oss_sysinfo sysinfo;
  int result = ioctl( mixerfd, SNDCTL_SYSINFO, &sysinfo );
  if ( result == -1 ) {
    close( mixerfd );
    errorText_ = "RtApiOss::probeDeviceOpen: error getting sysinfo, OSS version >= 4.0 is required.";
    return FAILURE;
  }

  unsigned int nDevices = sysinfo.numaudios;
  if ( nDevices == 0 ) {
    // This should not happen because a check is made before this function is called.
    close( mixerfd );
    errorText_ = "RtApiOss::probeDeviceOpen: no devices found!";
    return FAILURE;
  }

  std::string deviceName;
  unsigned int m, device;
  for ( m=0; m<deviceList_.size(); m++ ) {
    if ( deviceList_[m].ID == deviceId ) {
      deviceName = deviceList_[m].name;
      break;
    }
  }

  if ( deviceName.empty() ) {
    errorText_ = "RtApiOss::probeDeviceOpen: device ID is invalid!";
    return FAILURE;
  }

  oss_audioinfo ainfo;
  for ( device=0; device<nDevices; device++ ) {
    ainfo.dev = device;
    result = ioctl( mixerfd, SNDCTL_AUDIOINFO, &ainfo );
    if ( result == -1 ) continue;
    if ( deviceName == std::string( ainfo.name ) ) break;
  }

  close( mixerfd );
  if ( device == nDevices ) {
    errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") not found.";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Check if device supports input or output
  if ( ( mode == OUTPUT && !( ainfo.caps & PCM_CAP_OUTPUT ) ) ||
       ( mode == INPUT && !( ainfo.caps & PCM_CAP_INPUT ) ) ) {
    if ( mode == OUTPUT )
      errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") does not support output.";
    else
      errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") does not support input.";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  int flags = 0;
  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  if ( mode == OUTPUT )
    flags |= O_WRONLY;
  else { // mode == INPUT
    if (stream_.mode == OUTPUT && stream_.deviceId[0] == device) {
      // We just set the same device for playback ... close and reopen for duplex (OSS only).
      close( handle->id[0] );
      handle->id[0] = 0;
      if ( !( ainfo.caps & PCM_CAP_DUPLEX ) ) {
        errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") does not support duplex mode.";
        errorText_ = errorStream_.str();
        return FAILURE;
      }
      // Check that the number previously set channels is the same.
      if ( stream_.nUserChannels[0] != channels ) {
        errorStream_ << "RtApiOss::probeDeviceOpen: input/output channels must be equal for OSS duplex device (" << ainfo.name << ").";
        errorText_ = errorStream_.str();
        return FAILURE;
      }
      flags |= O_RDWR;
    }
    else
      flags |= O_RDONLY;
  }

  // Set exclusive access if specified.
  if ( options && options->flags & RTAUDIO_HOG_DEVICE ) flags |= O_EXCL;

  // Try to open the device.
  int fd;
  fd = open( ainfo.devnode, flags, 0 );
  if ( fd == -1 ) {
    if ( errno == EBUSY )
      errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") is busy.";
    else
      errorStream_ << "RtApiOss::probeDeviceOpen: error opening device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // For duplex operation, specifically set this mode (this doesn't seem to work).
  /*
    if ( flags | O_RDWR ) {
    result = ioctl( fd, SNDCTL_DSP_SETDUPLEX, NULL );
    if ( result == -1) {
    errorStream_ << "RtApiOss::probeDeviceOpen: error setting duplex mode for device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
    }
    }
  */

  // Check the device channel support.
  stream_.nUserChannels[mode] = channels;
  if ( ainfo.max_channels < (int)(channels + firstChannel) ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: the device (" << ainfo.name << ") does not support requested channel parameters.";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Set the number of channels.
  int deviceChannels = channels + firstChannel;
  result = ioctl( fd, SNDCTL_DSP_CHANNELS, &deviceChannels );
  if ( result == -1 || deviceChannels < (int)(channels + firstChannel) ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: error setting channel parameters on device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }
  stream_.nDeviceChannels[mode] = deviceChannels;

  // Get the data format mask
  int mask;
  result = ioctl( fd, SNDCTL_DSP_GETFMTS, &mask );
  if ( result == -1 ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: error getting device (" << ainfo.name << ") data formats.";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Determine how to set the device format.
  stream_.userFormat = format;
  int deviceFormat = -1;
  stream_.doByteSwap[mode] = false;
  if ( format == RTAUDIO_SINT8 ) {
    if ( mask & AFMT_S8 ) {
      deviceFormat = AFMT_S8;
      stream_.deviceFormat[mode] = RTAUDIO_SINT8;
    }
  }
  else if ( format == RTAUDIO_SINT16 ) {
    if ( mask & AFMT_S16_NE ) {
      deviceFormat = AFMT_S16_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT16;
    }
    else if ( mask & AFMT_S16_OE ) {
      deviceFormat = AFMT_S16_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT16;
      stream_.doByteSwap[mode] = true;
    }
  }
  else if ( format == RTAUDIO_SINT24 ) {
    if ( mask & AFMT_S24_NE ) {
      deviceFormat = AFMT_S24_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT24;
    }
    else if ( mask & AFMT_S24_OE ) {
      deviceFormat = AFMT_S24_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT24;
      stream_.doByteSwap[mode] = true;
    }
  }
  else if ( format == RTAUDIO_SINT32 ) {
    if ( mask & AFMT_S32_NE ) {
      deviceFormat = AFMT_S32_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT32;
    }
    else if ( mask & AFMT_S32_OE ) {
      deviceFormat = AFMT_S32_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT32;
      stream_.doByteSwap[mode] = true;
    }
  }

  if ( deviceFormat == -1 ) {
    // The user requested format is not natively supported by the device.
    if ( mask & AFMT_S16_NE ) {
      deviceFormat = AFMT_S16_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT16;
    }
    else if ( mask & AFMT_S32_NE ) {
      deviceFormat = AFMT_S32_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT32;
    }
    else if ( mask & AFMT_S24_NE ) {
      deviceFormat = AFMT_S24_NE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT24;
    }
    else if ( mask & AFMT_S16_OE ) {
      deviceFormat = AFMT_S16_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT16;
      stream_.doByteSwap[mode] = true;
    }
    else if ( mask & AFMT_S32_OE ) {
      deviceFormat = AFMT_S32_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT32;
      stream_.doByteSwap[mode] = true;
    }
    else if ( mask & AFMT_S24_OE ) {
      deviceFormat = AFMT_S24_OE;
      stream_.deviceFormat[mode] = RTAUDIO_SINT24;
      stream_.doByteSwap[mode] = true;
    }
    else if ( mask & AFMT_S8) {
      deviceFormat = AFMT_S8;
      stream_.deviceFormat[mode] = RTAUDIO_SINT8;
    }
  }

  if ( stream_.deviceFormat[mode] == 0 ) {
    // This really shouldn't happen ...
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") data format not supported by RtAudio.";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Set the data format.
  int temp = deviceFormat;
  result = ioctl( fd, SNDCTL_DSP_SETFMT, &deviceFormat );
  if ( result == -1 || deviceFormat != temp ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: error setting data format on device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Attempt to set the buffer size.  According to OSS, the minimum
  // number of buffers is two.  The supposed minimum buffer size is 16
  // bytes, so that will be our lower bound.  The argument to this
  // call is in the form 0xMMMMSSSS (hex), where the buffer size (in
  // bytes) is given as 2^SSSS and the number of buffers as 2^MMMM.
  // We'll check the actual value used near the end of the setup
  // procedure.
  int ossBufferBytes = *bufferSize * formatBytes( stream_.deviceFormat[mode] ) * deviceChannels;
  if ( ossBufferBytes < 16 ) ossBufferBytes = 16;
  int buffers = 0;
  if ( options ) buffers = options->numberOfBuffers;
  if ( options && options->flags & RTAUDIO_MINIMIZE_LATENCY ) buffers = 2;
  if ( buffers < 2 ) buffers = 3;
  temp = ((int) buffers << 16) + (int)( log10( (double)ossBufferBytes ) / log10( 2.0 ) );
  result = ioctl( fd, SNDCTL_DSP_SETFRAGMENT, &temp );
  if ( result == -1 ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: error setting buffer size on device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }
  stream_.nBuffers = buffers;

  // Save buffer size (in sample frames).
  *bufferSize = ossBufferBytes / ( formatBytes(stream_.deviceFormat[mode]) * deviceChannels );
  stream_.bufferSize = *bufferSize;

  // Set the sample rate.
  int srate = sampleRate;
  result = ioctl( fd, SNDCTL_DSP_SPEED, &srate );
  if ( result == -1 ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: error setting sample rate (" << sampleRate << ") on device (" << ainfo.name << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }

  // Verify the sample rate setup worked.
  if ( abs( srate - (int)sampleRate ) > 100 ) {
    close( fd );
    errorStream_ << "RtApiOss::probeDeviceOpen: device (" << ainfo.name << ") does not support sample rate (" << sampleRate << ").";
    errorText_ = errorStream_.str();
    return FAILURE;
  }
  stream_.sampleRate = sampleRate;

  if ( mode == INPUT && stream_.mode == OUTPUT && stream_.deviceId[0] == device) {
    // We're doing duplex setup here.
    stream_.deviceFormat[0] = stream_.deviceFormat[1];
    stream_.nDeviceChannels[0] = deviceChannels;
  }

  // Set interleaving parameters.
  stream_.userInterleaved = true;
  stream_.deviceInterleaved[mode] =  true;
  if ( options && options->flags & RTAUDIO_NONINTERLEAVED )
    stream_.userInterleaved = false;

  // Set flags for buffer conversion
  stream_.doConvertBuffer[mode] = false;
  if ( stream_.userFormat != stream_.deviceFormat[mode] )
    stream_.doConvertBuffer[mode] = true;
  if ( stream_.nUserChannels[mode] < stream_.nDeviceChannels[mode] )
    stream_.doConvertBuffer[mode] = true;
  if ( stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
       stream_.nUserChannels[mode] > 1 )
    stream_.doConvertBuffer[mode] = true;

  // Allocate the stream handles if necessary and then save.
  if ( stream_.apiHandle == 0 ) {
    try {
      handle = new OssHandle;
    }
    catch ( std::bad_alloc& ) {
      errorText_ = "RtApiOss::probeDeviceOpen: error allocating OssHandle memory.";
      goto error;
    }

    if ( pthread_cond_init( &handle->runnable, NULL ) ) {
      errorText_ = "RtApiOss::probeDeviceOpen: error initializing pthread condition variable.";
      goto error;
    }

    stream_.apiHandle = (void *) handle;
  }
  else {
    handle = (OssHandle *) stream_.apiHandle;
  }
  handle->id[mode] = fd;

  // Allocate necessary internal buffers.
  unsigned long bufferBytes;
  bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes( stream_.userFormat );
  stream_.userBuffer[mode] = (char *) calloc( bufferBytes, 1 );
  if ( stream_.userBuffer[mode] == NULL ) {
    errorText_ = "RtApiOss::probeDeviceOpen: error allocating user buffer memory.";
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
        errorText_ = "RtApiOss::probeDeviceOpen: error allocating device buffer memory.";
        goto error;
      }
    }
  }

  stream_.deviceId[mode] = device;
  stream_.state = STREAM_STOPPED;

  // Setup the buffer conversion information structure.
  if ( stream_.doConvertBuffer[mode] ) setConvertInfo( mode, firstChannel );

  // Setup thread if necessary.
  if ( stream_.mode == OUTPUT && mode == INPUT ) {
    // We had already set up an output stream.
    stream_.mode = DUPLEX;
    if ( stream_.deviceId[0] == device ) handle->id[0] = fd;
  }
  else {
    stream_.mode = mode;

    // Setup callback thread.
    stream_.callbackInfo.object = (void *) this;

    // Set the thread attributes for joinable and realtime scheduling
    // priority.  The higher priority will only take affect if the
    // program is run as root or suid.
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
    result = pthread_create( &stream_.callbackInfo.thread, &attr, ossCallbackHandler, &stream_.callbackInfo );
    pthread_attr_destroy( &attr );
    if ( result ) {
      // Failed. Try instead with default attributes.
      result = pthread_create( &stream_.callbackInfo.thread, NULL, ossCallbackHandler, &stream_.callbackInfo );
      if ( result ) {
        stream_.callbackInfo.isRunning = false;
        errorText_ = "RtApiOss::error creating callback thread!";
        goto error;
      }
    }
  }

  return SUCCESS;

 error:
  if ( handle ) {
    pthread_cond_destroy( &handle->runnable );
    if ( handle->id[0] ) close( handle->id[0] );
    if ( handle->id[1] ) close( handle->id[1] );
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

  stream_.state = STREAM_CLOSED;
  return FAILURE;
}

void RtApiOss :: closeStream()
{
  if ( stream_.state == STREAM_CLOSED ) {
    errorText_ = "RtApiOss::closeStream(): no open stream to close!";
    error( RTAUDIO_WARNING );
    return;
  }

  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  stream_.callbackInfo.isRunning = false;
  MUTEX_LOCK( &stream_.mutex );
  if ( stream_.state == STREAM_STOPPED )
    pthread_cond_signal( &handle->runnable );
  MUTEX_UNLOCK( &stream_.mutex );
  pthread_join( stream_.callbackInfo.thread, NULL );

  if ( stream_.state == STREAM_RUNNING ) {
    if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX )
      ioctl( handle->id[0], SNDCTL_DSP_HALT, 0 );
    else
      ioctl( handle->id[1], SNDCTL_DSP_HALT, 0 );
    stream_.state = STREAM_STOPPED;
  }

  if ( handle ) {
    pthread_cond_destroy( &handle->runnable );
    if ( handle->id[0] ) close( handle->id[0] );
    if ( handle->id[1] ) close( handle->id[1] );
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

  clearStreamInfo();
  //stream_.mode = UNINITIALIZED;
  //stream_.state = STREAM_CLOSED;
}

RtAudioErrorType RtApiOss :: startStream()
{
  if ( stream_.state != STREAM_STOPPED ) {
    if ( stream_.state == STREAM_RUNNING )
      errorText_ = "RtApiOss::startStream(): the stream is already running!";
    else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiOss::startStream(): the stream is stopping or closed!";
    return error( RTAUDIO_WARNING );
  }

  MUTEX_LOCK( &stream_.mutex );

  /*
  #if defined( HAVE_GETTIMEOFDAY )
  gettimeofday( &stream_.lastTickTimestamp, NULL );
  #endif
  */

  stream_.state = STREAM_RUNNING;

  // No need to do anything else here ... OSS automatically starts
  // when fed samples.

  MUTEX_UNLOCK( &stream_.mutex );

  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  pthread_cond_signal( &handle->runnable );
  return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiOss :: stopStream()
{
  if ( stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING ) {
    if ( stream_.state == STREAM_STOPPED )
      errorText_ = "RtApiOss::stopStream(): the stream is already stopped!";
    else if ( stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiOss::stopStream(): the stream is closed!";
    return error( RTAUDIO_WARNING );
  }

  MUTEX_LOCK( &stream_.mutex );

  // The state might change while waiting on a mutex.
  if ( stream_.state == STREAM_STOPPED ) {
    MUTEX_UNLOCK( &stream_.mutex );
    return RTAUDIO_NO_ERROR;
  }

  int result = 0;
  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

    // Flush the output with zeros a few times.
    char *buffer;
    int samples;
    RtAudioFormat format;

    if ( stream_.doConvertBuffer[0] ) {
      buffer = stream_.deviceBuffer;
      samples = stream_.bufferSize * stream_.nDeviceChannels[0];
      format = stream_.deviceFormat[0];
    }
    else {
      buffer = stream_.userBuffer[0];
      samples = stream_.bufferSize * stream_.nUserChannels[0];
      format = stream_.userFormat;
    }

    memset( buffer, 0, samples * formatBytes(format) );
    for ( unsigned int i=0; i<stream_.nBuffers+1; i++ ) {
      result = write( handle->id[0], buffer, samples * formatBytes(format) );
      if ( result == -1 ) {
        errorText_ = "RtApiOss::stopStream: audio write error.";
        error( RTAUDIO_WARNING );
      }
    }

    result = ioctl( handle->id[0], SNDCTL_DSP_HALT, 0 );
    if ( result == -1 ) {
      errorStream_ << "RtApiOss::stopStream: system error stopping callback procedure on device (" << stream_.deviceId[0] << ").";
      errorText_ = errorStream_.str();
      goto unlock;
    }
    handle->triggered = false;
  }

  if ( stream_.mode == INPUT || ( stream_.mode == DUPLEX && handle->id[0] != handle->id[1] ) ) {
    result = ioctl( handle->id[1], SNDCTL_DSP_HALT, 0 );
    if ( result == -1 ) {
      errorStream_ << "RtApiOss::stopStream: system error stopping input callback procedure on device (" << stream_.deviceId[0] << ").";
      errorText_ = errorStream_.str();
      goto unlock;
    }
  }

 unlock:
  stream_.state = STREAM_STOPPED;
  MUTEX_UNLOCK( &stream_.mutex );

  if ( result != -1 ) return RTAUDIO_NO_ERROR;
  return error( RTAUDIO_SYSTEM_ERROR );
}

RtAudioErrorType RtApiOss :: abortStream()
{
  if ( stream_.state != STREAM_RUNNING ) {
    if ( stream_.state == STREAM_STOPPED )
      errorText_ = "RtApiOss::abortStream(): the stream is already stopped!";
    else if ( stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED )
      errorText_ = "RtApiOss::abortStream(): the stream is stopping or closed!";
    return error( RTAUDIO_WARNING );
  }

  MUTEX_LOCK( &stream_.mutex );

  // The state might change while waiting on a mutex.
  if ( stream_.state == STREAM_STOPPED ) {
    MUTEX_UNLOCK( &stream_.mutex );
    return RTAUDIO_NO_ERROR;
  }

  int result = 0;
  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {
    result = ioctl( handle->id[0], SNDCTL_DSP_HALT, 0 );
    if ( result == -1 ) {
      errorStream_ << "RtApiOss::abortStream: system error stopping callback procedure on device (" << stream_.deviceId[0] << ").";
      errorText_ = errorStream_.str();
      goto unlock;
    }
    handle->triggered = false;
  }

  if ( stream_.mode == INPUT || ( stream_.mode == DUPLEX && handle->id[0] != handle->id[1] ) ) {
    result = ioctl( handle->id[1], SNDCTL_DSP_HALT, 0 );
    if ( result == -1 ) {
      errorStream_ << "RtApiOss::abortStream: system error stopping input callback procedure on device (" << stream_.deviceId[0] << ").";
      errorText_ = errorStream_.str();
      goto unlock;
    }
  }

 unlock:
  stream_.state = STREAM_STOPPED;
  MUTEX_UNLOCK( &stream_.mutex );

  if ( result != -1 ) return RTAUDIO_SYSTEM_ERROR;
  return error( RTAUDIO_SYSTEM_ERROR );
}

void RtApiOss :: callbackEvent()
{
  OssHandle *handle = (OssHandle *) stream_.apiHandle;
  if ( stream_.state == STREAM_STOPPED ) {
    MUTEX_LOCK( &stream_.mutex );
    pthread_cond_wait( &handle->runnable, &stream_.mutex );
    if ( stream_.state != STREAM_RUNNING ) {
      MUTEX_UNLOCK( &stream_.mutex );
      return;
    }
    MUTEX_UNLOCK( &stream_.mutex );
  }

  if ( stream_.state == STREAM_CLOSED ) {
    errorText_ = "RtApiOss::callbackEvent(): the stream is closed ... this shouldn't happen!";
    error( RTAUDIO_WARNING );
    return;
  }

  // Invoke user callback to get fresh output data.
  int doStopStream = 0;
  RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
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
  doStopStream = callback( stream_.userBuffer[0], stream_.userBuffer[1],
                           stream_.bufferSize, streamTime, status, stream_.callbackInfo.userData );
  if ( doStopStream == 2 ) {
    this->abortStream();
    return;
  }

  MUTEX_LOCK( &stream_.mutex );

  // The state might change while waiting on a mutex.
  if ( stream_.state == STREAM_STOPPED ) goto unlock;

  int result;
  char *buffer;
  int samples;
  RtAudioFormat format;

  if ( stream_.mode == OUTPUT || stream_.mode == DUPLEX ) {

    // Setup parameters and do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[0] ) {
      buffer = stream_.deviceBuffer;
      convertBuffer( buffer, stream_.userBuffer[0], stream_.convertInfo[0] );
      samples = stream_.bufferSize * stream_.nDeviceChannels[0];
      format = stream_.deviceFormat[0];
    }
    else {
      buffer = stream_.userBuffer[0];
      samples = stream_.bufferSize * stream_.nUserChannels[0];
      format = stream_.userFormat;
    }

    // Do byte swapping if necessary.
    if ( stream_.doByteSwap[0] )
      byteSwapBuffer( buffer, samples, format );

    if ( stream_.mode == DUPLEX && handle->triggered == false ) {
      int trig = 0;
      ioctl( handle->id[0], SNDCTL_DSP_SETTRIGGER, &trig );
      result = write( handle->id[0], buffer, samples * formatBytes(format) );
      trig = PCM_ENABLE_INPUT|PCM_ENABLE_OUTPUT;
      ioctl( handle->id[0], SNDCTL_DSP_SETTRIGGER, &trig );
      handle->triggered = true;
    }
    else
      // Write samples to device.
      result = write( handle->id[0], buffer, samples * formatBytes(format) );

    if ( result == -1 ) {
      // We'll assume this is an underrun, though there isn't a
      // specific means for determining that.
      handle->xrun[0] = true;
      errorText_ = "RtApiOss::callbackEvent: audio write error.";
      error( RTAUDIO_WARNING );
      // Continue on to input section.
    }
  }

  if ( stream_.mode == INPUT || stream_.mode == DUPLEX ) {

    // Setup parameters.
    if ( stream_.doConvertBuffer[1] ) {
      buffer = stream_.deviceBuffer;
      samples = stream_.bufferSize * stream_.nDeviceChannels[1];
      format = stream_.deviceFormat[1];
    }
    else {
      buffer = stream_.userBuffer[1];
      samples = stream_.bufferSize * stream_.nUserChannels[1];
      format = stream_.userFormat;
    }

    // Read samples from device.
    result = read( handle->id[1], buffer, samples * formatBytes(format) );

    if ( result == -1 ) {
      // We'll assume this is an overrun, though there isn't a
      // specific means for determining that.
      handle->xrun[1] = true;
      errorText_ = "RtApiOss::callbackEvent: audio read error.";
      error( RTAUDIO_WARNING );
      goto unlock;
    }

    // Do byte swapping if necessary.
    if ( stream_.doByteSwap[1] )
      byteSwapBuffer( buffer, samples, format );

    // Do buffer conversion if necessary.
    if ( stream_.doConvertBuffer[1] )
      convertBuffer( stream_.userBuffer[1], stream_.deviceBuffer, stream_.convertInfo[1] );
  }

 unlock:
  MUTEX_UNLOCK( &stream_.mutex );

  RtApi::tickStreamTime();
  if ( doStopStream == 1 ) this->stopStream();
}

static void *ossCallbackHandler( void *ptr )
{
  CallbackInfo *info = (CallbackInfo *) ptr;
  RtApiOss *object = (RtApiOss *) info->object;
  bool *isRunning = &info->isRunning;

#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
  if (info->doRealtime) {
    std::cerr << "RtAudio oss: " << 
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

//******************** End of __LINUX_OSS__ *********************//
#endif


// *************************************************** //
//
// Protected common (OS-independent) RtAudio methods.
//
// *************************************************** //

// This method can be modified to control the behavior of error
// message printing.
RtAudioErrorType RtApi :: error( RtAudioErrorType type )
{
  errorStream_.str(""); // clear the ostringstream to avoid repeated messages

  // Don't output warnings if showWarnings_ is false
  if ( type == RTAUDIO_WARNING && showWarnings_ == false ) return type;
  
  if ( errorCallback_ ) {
    //const std::string errorMessage = errorText_;
    //errorCallback_( type, errorMessage );
    errorCallback_( type, errorText_ );
  }
  else
    std::cerr << '\n' << errorText_ << "\n\n";
  return type;
}

/*
void RtApi :: verifyStream()
{
  if ( stream_.state == STREAM_CLOSED ) {
    errorText_ = "RtApi:: a stream is not open!";
    error( RtAudioError::INVALID_USE );
  }
}
*/

void RtApi :: clearStreamInfo()
{
  stream_.mode = UNINITIALIZED;
  stream_.state = STREAM_CLOSED;
  stream_.sampleRate = 0;
  stream_.bufferSize = 0;
  stream_.nBuffers = 0;
  stream_.userFormat = 0;
  stream_.userInterleaved = true;
  stream_.streamTime = 0.0;
  stream_.apiHandle = 0;
  stream_.deviceBuffer = 0;
  stream_.callbackInfo.callback = 0;
  stream_.callbackInfo.userData = 0;
  stream_.callbackInfo.isRunning = false;
  stream_.callbackInfo.deviceDisconnected = false;
  for ( int i=0; i<2; i++ ) {
    stream_.deviceId[i] = 11111;
    stream_.doConvertBuffer[i] = false;
    stream_.deviceInterleaved[i] = true;
    stream_.doByteSwap[i] = false;
    stream_.nUserChannels[i] = 0;
    stream_.nDeviceChannels[i] = 0;
    stream_.channelOffset[i] = 0;
    stream_.deviceFormat[i] = 0;
    stream_.latency[i] = 0;
    stream_.userBuffer[i] = 0;
    stream_.convertInfo[i].channels = 0;
    stream_.convertInfo[i].inJump = 0;
    stream_.convertInfo[i].outJump = 0;
    stream_.convertInfo[i].inFormat = 0;
    stream_.convertInfo[i].outFormat = 0;
    stream_.convertInfo[i].inOffset.clear();
    stream_.convertInfo[i].outOffset.clear();
  }
}

unsigned int RtApi :: formatBytes( RtAudioFormat format )
{
  if ( format == RTAUDIO_SINT16 )
    return 2;
  else if ( format == RTAUDIO_SINT32 || format == RTAUDIO_FLOAT32 )
    return 4;
  else if ( format == RTAUDIO_FLOAT64 )
    return 8;
  else if ( format == RTAUDIO_SINT24 )
    return 3;
  else if ( format == RTAUDIO_SINT8 )
    return 1;

  errorText_ = "RtApi::formatBytes: undefined format.";
  error( RTAUDIO_WARNING );

  return 0;
}

void RtApi :: setConvertInfo( StreamMode mode, unsigned int firstChannel )
{
  if ( mode == INPUT ) { // convert device to user buffer
    stream_.convertInfo[mode].inJump = stream_.nDeviceChannels[1];
    stream_.convertInfo[mode].outJump = stream_.nUserChannels[1];
    stream_.convertInfo[mode].inFormat = stream_.deviceFormat[1];
    stream_.convertInfo[mode].outFormat = stream_.userFormat;
  }
  else { // convert user to device buffer
    stream_.convertInfo[mode].inJump = stream_.nUserChannels[0];
    stream_.convertInfo[mode].outJump = stream_.nDeviceChannels[0];
    stream_.convertInfo[mode].inFormat = stream_.userFormat;
    stream_.convertInfo[mode].outFormat = stream_.deviceFormat[0];
  }

  if ( stream_.convertInfo[mode].inJump < stream_.convertInfo[mode].outJump )
    stream_.convertInfo[mode].channels = stream_.convertInfo[mode].inJump;
  else
    stream_.convertInfo[mode].channels = stream_.convertInfo[mode].outJump;

  // Set up the interleave/deinterleave offsets.
  if ( stream_.deviceInterleaved[mode] != stream_.userInterleaved ) {
    if ( ( mode == OUTPUT && stream_.deviceInterleaved[mode] ) ||
         ( mode == INPUT && stream_.userInterleaved ) ) {
      for ( int k=0; k<stream_.convertInfo[mode].channels; k++ ) {
        stream_.convertInfo[mode].inOffset.push_back( k * stream_.bufferSize );
        stream_.convertInfo[mode].outOffset.push_back( k );
        stream_.convertInfo[mode].inJump = 1;
      }
    }
    else {
      for ( int k=0; k<stream_.convertInfo[mode].channels; k++ ) {
        stream_.convertInfo[mode].inOffset.push_back( k );
        stream_.convertInfo[mode].outOffset.push_back( k * stream_.bufferSize );
        stream_.convertInfo[mode].outJump = 1;
      }
    }
  }
  else { // no (de)interleaving
    if ( stream_.userInterleaved ) {
      for ( int k=0; k<stream_.convertInfo[mode].channels; k++ ) {
        stream_.convertInfo[mode].inOffset.push_back( k );
        stream_.convertInfo[mode].outOffset.push_back( k );
      }
    }
    else {
      for ( int k=0; k<stream_.convertInfo[mode].channels; k++ ) {
        stream_.convertInfo[mode].inOffset.push_back( k * stream_.bufferSize );
        stream_.convertInfo[mode].outOffset.push_back( k * stream_.bufferSize );
        stream_.convertInfo[mode].inJump = 1;
        stream_.convertInfo[mode].outJump = 1;
      }
    }
  }

  // Add channel offset.
  if ( firstChannel > 0 ) {
    if ( stream_.deviceInterleaved[mode] ) {
      if ( mode == OUTPUT ) {
        for ( int k=0; k<stream_.convertInfo[mode].channels; k++ )
          stream_.convertInfo[mode].outOffset[k] += firstChannel;
      }
      else {
        for ( int k=0; k<stream_.convertInfo[mode].channels; k++ )
          stream_.convertInfo[mode].inOffset[k] += firstChannel;
      }
    }
    else {
      if ( mode == OUTPUT ) {
        for ( int k=0; k<stream_.convertInfo[mode].channels; k++ )
          stream_.convertInfo[mode].outOffset[k] += ( firstChannel * stream_.bufferSize );
      }
      else {
        for ( int k=0; k<stream_.convertInfo[mode].channels; k++ )
          stream_.convertInfo[mode].inOffset[k] += ( firstChannel  * stream_.bufferSize );
      }
    }
  }
}

void RtApi :: convertBuffer( char *outBuffer, char *inBuffer, ConvertInfo &info )
{
  // This function does format conversion, input/output channel compensation, and
  // data interleaving/deinterleaving.  24-bit integers are assumed to occupy
  // the lower three bytes of a 32-bit integer.

  // Clear our duplex device output buffer if there are more device outputs than user outputs
  if ( outBuffer == stream_.deviceBuffer && stream_.mode == DUPLEX && info.outJump > info.inJump )
    memset( outBuffer, 0, stream_.bufferSize * info.outJump * formatBytes( info.outFormat ) );

  int j;
  if (info.outFormat == RTAUDIO_FLOAT64) {
    Float64 *out = (Float64 *)outBuffer;

    if (info.inFormat == RTAUDIO_SINT8) {
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float64) in[info.inOffset[j]] / 128.0;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT16) {
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float64) in[info.inOffset[j]] / 32768.0;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float64) in[info.inOffset[j]].asInt() / 8388608.0;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float64) in[info.inOffset[j]] / 2147483648.0;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float64) in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      // Channel compensation and/or (de)interleaving only.
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
  else if (info.outFormat == RTAUDIO_FLOAT32) {
    Float32 *out = (Float32 *)outBuffer;

    if (info.inFormat == RTAUDIO_SINT8) {
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float32) in[info.inOffset[j]] / 128.f;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT16) {
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float32) in[info.inOffset[j]] / 32768.f;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float32) in[info.inOffset[j]].asInt() / 8388608.f;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float32) in[info.inOffset[j]] / 2147483648.f;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      // Channel compensation and/or (de)interleaving only.
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Float32) in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
  else if (info.outFormat == RTAUDIO_SINT32) {
    Int32 *out = (Int32 *)outBuffer;
    if (info.inFormat == RTAUDIO_SINT8) {
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) in[info.inOffset[j]];
          out[info.outOffset[j]] <<= 24;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT16) {
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) in[info.inOffset[j]];
          out[info.outOffset[j]] <<= 16;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) in[info.inOffset[j]].asInt();
          out[info.outOffset[j]] <<= 8;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      // Channel compensation and/or (de)interleaving only.
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          // Use llround() which returns `long long` which is guaranteed to be at least 64 bits.
          out[info.outOffset[j]] = (Int32) std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.f), 2147483647LL), -2147483648LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.0), 2147483647LL), -2147483648LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
  else if (info.outFormat == RTAUDIO_SINT24) {
    Int24 *out = (Int24 *)outBuffer;
    if (info.inFormat == RTAUDIO_SINT8) {
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) (in[info.inOffset[j]] << 16);
          //out[info.outOffset[j]] <<= 16;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT16) {
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) (in[info.inOffset[j]] << 8);
          //out[info.outOffset[j]] <<= 8;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      // Channel compensation and/or (de)interleaving only.
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) (in[info.inOffset[j]] >> 8);
          //out[info.outOffset[j]] >>= 8;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.f), 8388607LL), -8388608LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int32) std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.0), 8388607LL), -8388608LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
  else if (info.outFormat == RTAUDIO_SINT16) {
    Int16 *out = (Int16 *)outBuffer;
    if (info.inFormat == RTAUDIO_SINT8) {
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int16) in[info.inOffset[j]];
          out[info.outOffset[j]] <<= 8;
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT16) {
      // Channel compensation and/or (de)interleaving only.
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int16) (in[info.inOffset[j]].asInt() >> 8);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int16) ((in[info.inOffset[j]] >> 16) & 0x0000ffff);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int16) std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.f), 32767LL), -32768LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (Int16) std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.0), 32767LL), -32768LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
  else if (info.outFormat == RTAUDIO_SINT8) {
    signed char *out = (signed char *)outBuffer;
    if (info.inFormat == RTAUDIO_SINT8) {
      // Channel compensation and/or (de)interleaving only.
      signed char *in = (signed char *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = in[info.inOffset[j]];
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    if (info.inFormat == RTAUDIO_SINT16) {
      Int16 *in = (Int16 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (signed char) ((in[info.inOffset[j]] >> 8) & 0x00ff);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT24) {
      Int24 *in = (Int24 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (signed char) (in[info.inOffset[j]].asInt() >> 16);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_SINT32) {
      Int32 *in = (Int32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (signed char) ((in[info.inOffset[j]] >> 24) & 0x000000ff);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT32) {
      Float32 *in = (Float32 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (signed char) std::max(std::min(std::llround(in[info.inOffset[j]] * 128.f), 127LL), -128LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
    else if (info.inFormat == RTAUDIO_FLOAT64) {
      Float64 *in = (Float64 *)inBuffer;
      for (unsigned int i=0; i<stream_.bufferSize; i++) {
        for (j=0; j<info.channels; j++) {
          out[info.outOffset[j]] = (signed char) std::max(std::min(std::llround(in[info.inOffset[j]] * 128.0), 127LL), -128LL);
        }
        in += info.inJump;
        out += info.outJump;
      }
    }
  }
}

//static inline uint16_t bswap_16(uint16_t x) { return (x>>8) | (x<<8); }
//static inline uint32_t bswap_32(uint32_t x) { return (bswap_16(x&0xffff)<<16) | (bswap_16(x>>16)); }
//static inline uint64_t bswap_64(uint64_t x) { return (((unsigned long long)bswap_32(x&0xffffffffull))<<32) | (bswap_32(x>>32)); }

void RtApi :: byteSwapBuffer( char *buffer, unsigned int samples, RtAudioFormat format )
{
  char val;
  char *ptr;

  ptr = buffer;
  if ( format == RTAUDIO_SINT16 ) {
    for ( unsigned int i=0; i<samples; i++ ) {
      // Swap 1st and 2nd bytes.
      val = *(ptr);
      *(ptr) = *(ptr+1);
      *(ptr+1) = val;

      // Increment 2 bytes.
      ptr += 2;
    }
  }
  else if ( format == RTAUDIO_SINT32 ||
            format == RTAUDIO_FLOAT32 ) {
    for ( unsigned int i=0; i<samples; i++ ) {
      // Swap 1st and 4th bytes.
      val = *(ptr);
      *(ptr) = *(ptr+3);
      *(ptr+3) = val;

      // Swap 2nd and 3rd bytes.
      ptr += 1;
      val = *(ptr);
      *(ptr) = *(ptr+1);
      *(ptr+1) = val;

      // Increment 3 more bytes.
      ptr += 3;
    }
  }
  else if ( format == RTAUDIO_SINT24 ) {
    for ( unsigned int i=0; i<samples; i++ ) {
      // Swap 1st and 3rd bytes.
      val = *(ptr);
      *(ptr) = *(ptr+2);
      *(ptr+2) = val;

      // Increment 2 more bytes.
      ptr += 2;
    }
  }
  else if ( format == RTAUDIO_FLOAT64 ) {
    for ( unsigned int i=0; i<samples; i++ ) {
      // Swap 1st and 8th bytes
      val = *(ptr);
      *(ptr) = *(ptr+7);
      *(ptr+7) = val;

      // Swap 2nd and 7th bytes
      ptr += 1;
      val = *(ptr);
      *(ptr) = *(ptr+5);
      *(ptr+5) = val;

      // Swap 3rd and 6th bytes
      ptr += 1;
      val = *(ptr);
      *(ptr) = *(ptr+3);
      *(ptr+3) = val;

      // Swap 4th and 5th bytes
      ptr += 1;
      val = *(ptr);
      *(ptr) = *(ptr+1);
      *(ptr+1) = val;

      // Increment 5 more bytes.
      ptr += 5;
    }
  }
}

  // Indentation settings for Vim and Emacs
  //
  // Local Variables:
  // c-basic-offset: 2
  // indent-tabs-mode: nil
  // End:
  //
  // vim: et sts=2 sw=2

