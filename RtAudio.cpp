#include "RtAudio.h"
#include "RtAudio.h"
#include "RtAudio.h"
#include "RtAudio.h"
#include "RtAudio.h"
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
const unsigned int RtApi::MAX_SAMPLE_RATES = 16;
const unsigned int RtApi::SAMPLE_RATES[] = {
  4000, 5512, 8000, 9600, 11025, 16000, 22050,
  32000, 44100, 48000, 64000, 88200, 96000, 128000, 176400, 192000
};

// *************************************************** //
//
// RtApi subclass prototypes.
//
// *************************************************** //

#if defined(__MACOSX_CORE__)

#include "core/RtApiCore.h"

#endif

#if defined(__UNIX_JACK__)

#include "jack/RtApiJack.h"

#endif

#if defined(__WINDOWS_ASIO__)

#include "asio/RtApiAsio.h"

#endif

#if defined(__WINDOWS_WASAPI__)

#include "wasapi/RtApiWasapiEnumerator.h"
#include "wasapi/RtApiWasapiProber.h"
#include "wasapi/RtApiWasapiStreamFactory.h"
#include "wasapi/RtApiWasapiStream.h"
#include "wasapi/RtApiWasapiSystemCallback.h"

#endif

#if defined(__LINUX_ALSA__)

#include "alsa/RtApiAlsa.h"

#endif

#if defined(__LINUX_PULSE__)

#include "pulse/RtApiPulse.h"

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
  { "asio"        , "ASIO" },
  { "wasapi"      , "WASAPI" },
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
#if defined(__WINDOWS_ASIO__)
  RtAudio::WINDOWS_ASIO,
#endif
#if defined(__WINDOWS_WASAPI__)
  RtAudio::WINDOWS_WASAPI,
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
#if defined(__WINDOWS_ASIO__)
  if ( api == WINDOWS_ASIO )
    rtapi_ = new RtApiAsio();
#endif
#if defined(__WINDOWS_WASAPI__)
  //if ( api == WINDOWS_WASAPI )
    //rtapi_ = new RtApiWasapi();
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

RtAudioErrorType ErrorBase::error(RtAudioErrorType type, const std::string& message)
{
    errorStream_.str(""); // clear the ostringstream to avoid repeated messages
    errorText_ = message;
    // Don't output warnings if showWarnings_ is false
    if (type == RTAUDIO_WARNING && showWarnings_ == false) return type;

    if (errorCallback_) {
        //const std::string errorMessage = errorText_;
        //errorCallback_( type, errorMessage );
        errorCallback_(type, message);
    }
    else
        std::cerr << '\n' << message << "\n\n";
    return type;
}

RtAudioErrorType ErrorBase::errorThread(RtAudioErrorType type, const std::string& message)
{
    if (type == RTAUDIO_WARNING && showWarnings_ == false) return type;

    if (errorCallback_) {
        //const std::string errorMessage = errorText_;
        //errorCallback_( type, errorMessage );
        errorCallback_(type, message);
    }
    else
        std::cerr << '\n' << message << "\n\n";
    return type;
}

RtAudioErrorType ErrorBase::error(RtAudioErrorType type)
{
    errorStream_.str(""); // clear the ostringstream to avoid repeated messages

    // Don't output warnings if showWarnings_ is false
    if (type == RTAUDIO_WARNING && showWarnings_ == false) return type;

    if (errorCallback_) {
        //const std::string errorMessage = errorText_;
        //errorCallback_( type, errorMessage );
        errorCallback_(type, errorText_);
    }
    else
        std::cerr << '\n' << errorText_ << "\n\n";
    return type;
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
  
  unsigned int m, oChannels = 0;
  if ( oParams ) {
    oChannels = oParams->nChannels;
  }

  unsigned int iChannels = 0;
  if ( iParams ) {
    iChannels = iParams->nChannels;
  }

  bool result = false;

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

std::vector<RtAudio::DeviceInfo> RtApi::getDeviceInfosNoProbe(void)
{
    listDevices();
    return deviceList_;
}

RtAudio::DeviceInfo RtApi::getDeviceInfoByBusID(std::string busID)
{    
    for (unsigned int m = 0; m < deviceList_.size(); m++) {
        if (deviceList_[m].busID == busID) {
            if (probeSingleDeviceInfo(deviceList_[m])==false) {
                break;
            }
            return deviceList_[m];
        }            
    }

    errorText_ = "RtApi::getDeviceInfo: probe error.";
    error(RTAUDIO_INVALID_PARAMETER);
    return RtAudio::DeviceInfo();
}

void RtApi :: closeStream( void )
{
  // MUST be implemented in subclasses!
  return;
}

void RtApi::convertBuffer(const RtApi::RtApiStream stream_, char* outBuffer, char* inBuffer, RtApi::ConvertInfo info, unsigned int samples, RtApi::StreamMode mode)
{
    typedef S24 Int24;
    typedef signed short Int16;
    typedef signed int Int32;
    typedef float Float32;
    typedef double Float64;

    // This function does format conversion, RtApi::INPUT/RtApi::OUTPUT channel compensation, and
    // data interleaving/deinterleaving.  24-bit integers are assumed to occupy
    // the lower three bytes of a 32-bit integer.

    if (stream_.deviceInterleaved[mode] != stream_.userInterleaved) {
        info.inOffset.clear();
        info.outOffset.clear();
        if ((mode == RtApi::OUTPUT && stream_.deviceInterleaved[mode]) ||
            (mode == RtApi::INPUT && stream_.userInterleaved)) {
            for (int k = 0; k < info.channels; k++) {
                info.inOffset.push_back(k * samples);
                info.outOffset.push_back(k);
                info.inJump = 1;
            }
        }
        else {
            for (int k = 0; k < info.channels; k++) {
                info.inOffset.push_back(k);
                info.outOffset.push_back(k * samples);
                info.outJump = 1;
            }
        }
    }
    // Clear our RtApi::DUPLEX device RtApi::OUTPUT buffer if there are more device RtApi::OUTPUTs than user RtApi::OUTPUTs
    if (outBuffer == stream_.deviceBuffer && stream_.mode == RtApi::DUPLEX && info.outJump > info.inJump)
        memset(outBuffer, 0, samples * info.outJump * RtApi::formatBytes(info.outFormat));

    int j;
    if (info.outFormat == RTAUDIO_FLOAT64) {
        Float64* out = (Float64*)outBuffer;

        if (info.inFormat == RTAUDIO_SINT8) {
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 128.0;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT16) {
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 32768.0;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float64)in[info.inOffset[j]].asInt() / 8388608.0;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 2147483648.0;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float64)in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            // Channel compensation and/or (de)interleaving only.
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
    else if (info.outFormat == RTAUDIO_FLOAT32) {
        Float32* out = (Float32*)outBuffer;

        if (info.inFormat == RTAUDIO_SINT8) {
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 128.f;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT16) {
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 32768.f;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float32)in[info.inOffset[j]].asInt() / 8388608.f;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 2147483648.f;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            // Channel compensation and/or (de)interleaving only.
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Float32)in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
    else if (info.outFormat == RTAUDIO_SINT32) {
        Int32* out = (Int32*)outBuffer;
        if (info.inFormat == RTAUDIO_SINT8) {
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)in[info.inOffset[j]];
                    out[info.outOffset[j]] <<= 24;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT16) {
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)in[info.inOffset[j]];
                    out[info.outOffset[j]] <<= 16;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)in[info.inOffset[j]].asInt();
                    out[info.outOffset[j]] <<= 8;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            // Channel compensation and/or (de)interleaving only.
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    // Use llround() which returns `long long` which is guaranteed to be at least 64 bits.
                    out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.f), 2147483647LL), -2147483648LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.0), 2147483647LL), -2147483648LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
    else if (info.outFormat == RTAUDIO_SINT24) {
        Int24* out = (Int24*)outBuffer;
        if (info.inFormat == RTAUDIO_SINT8) {
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] << 16);
                    //out[info.outOffset[j]] <<= 16;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT16) {
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] << 8);
                    //out[info.outOffset[j]] <<= 8;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            // Channel compensation and/or (de)interleaving only.
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] >> 8);
                    //out[info.outOffset[j]] >>= 8;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.f), 8388607LL), -8388608LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.0), 8388607LL), -8388608LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
    else if (info.outFormat == RTAUDIO_SINT16) {
        Int16* out = (Int16*)outBuffer;
        if (info.inFormat == RTAUDIO_SINT8) {
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int16)in[info.inOffset[j]];
                    out[info.outOffset[j]] <<= 8;
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT16) {
            // Channel compensation and/or (de)interleaving only.
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int16)(in[info.inOffset[j]].asInt() >> 8);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int16)((in[info.inOffset[j]] >> 16) & 0x0000ffff);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int16)std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.f), 32767LL), -32768LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (Int16)std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.0), 32767LL), -32768LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
    else if (info.outFormat == RTAUDIO_SINT8) {
        signed char* out = (signed char*)outBuffer;
        if (info.inFormat == RTAUDIO_SINT8) {
            // Channel compensation and/or (de)interleaving only.
            signed char* in = (signed char*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = in[info.inOffset[j]];
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        if (info.inFormat == RTAUDIO_SINT16) {
            Int16* in = (Int16*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (signed char)((in[info.inOffset[j]] >> 8) & 0x00ff);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT24) {
            Int24* in = (Int24*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (signed char)(in[info.inOffset[j]].asInt() >> 16);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_SINT32) {
            Int32* in = (Int32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (signed char)((in[info.inOffset[j]] >> 24) & 0x000000ff);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT32) {
            Float32* in = (Float32*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (signed char)std::max(std::min(std::llround(in[info.inOffset[j]] * 128.f), 127LL), -128LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
        else if (info.inFormat == RTAUDIO_FLOAT64) {
            Float64* in = (Float64*)inBuffer;
            for (unsigned int i = 0; i < samples; i++) {
                for (j = 0; j < info.channels; j++) {
                    out[info.outOffset[j]] = (signed char)std::max(std::min(std::llround(in[info.inOffset[j]] * 128.0), 127LL), -128LL);
                }
                in += info.inJump;
                out += info.outJump;
            }
        }
    }
}

bool RtApi :: probeDeviceOpen( const std::string& /*deviceId*/, StreamMode /*mode*/, unsigned int /*channels*/,
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

RtAudioErrorType RtApi::openAsioControlPanel(void) {
    return RTAUDIO_UNKNOWN_ERROR;
}

unsigned int RtApi::formatBytes(RtAudioFormat format) {
    if (format == RTAUDIO_SINT16)
        return 2;
    else if (format == RTAUDIO_SINT32 || format == RTAUDIO_FLOAT32)
        return 4;
    else if (format == RTAUDIO_FLOAT64)
        return 8;
    else if (format == RTAUDIO_SINT24)
        return 3;
    else if (format == RTAUDIO_SINT8)
        return 1;
    return 0;
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

RtAudioErrorType RtApi::errorText(RtAudioErrorType type, const std::string& errorText)
{
    if ( type == RTAUDIO_WARNING && showWarnings_ == false ) return type;
    if ( errorCallback_ ) {
        errorCallback_( type, errorText );
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

