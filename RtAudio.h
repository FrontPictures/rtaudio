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

/*!
  \file RtAudio.h
 */

#ifndef __RTAUDIO_H
#define __RTAUDIO_H

#define RTAUDIO_VERSION_MAJOR 6
#define RTAUDIO_VERSION_MINOR 0
#define RTAUDIO_VERSION_PATCH 1
#define RTAUDIO_VERSION_BETA  0

#define RTAUDIO_TOSTRING2(n) #n
#define RTAUDIO_TOSTRING(n) RTAUDIO_TOSTRING2(n)

#if RTAUDIO_VERSION_BETA > 0
#define RTAUDIO_VERSION RTAUDIO_TOSTRING(RTAUDIO_VERSION_MAJOR) \
                        "." RTAUDIO_TOSTRING(RTAUDIO_VERSION_MINOR) \
                        "." RTAUDIO_TOSTRING(RTAUDIO_VERSION_PATCH) \
                     "beta" RTAUDIO_TOSTRING(RTAUDIO_VERSION_BETA)
#else
#define RTAUDIO_VERSION RTAUDIO_TOSTRING(RTAUDIO_VERSION_MAJOR) \
                        "." RTAUDIO_TOSTRING(RTAUDIO_VERSION_MINOR) \
                        "." RTAUDIO_TOSTRING(RTAUDIO_VERSION_PATCH)
#endif

#if defined _WIN32 || defined __CYGWIN__
#if defined(RTAUDIO_EXPORT)
#define RTAUDIO_DLL_PUBLIC __declspec(dllexport)
#else
#define RTAUDIO_DLL_PUBLIC
#endif
#else
#if __GNUC__ >= 4
#define RTAUDIO_DLL_PUBLIC __attribute__( (visibility( "default" )) )
#else
#define RTAUDIO_DLL_PUBLIC
#endif
#endif

#include <string>
#include <vector>
#include <iostream>
#include <functional>
#include <optional>

 /*! \typedef typedef unsigned long RtAudioFormat;
     \brief RtAudio data format type.

     Support for signed integers and floats.  Audio data fed to/from an
     RtAudio stream is assumed to ALWAYS be in host byte order.  The
     internal routines will automatically take care of any necessary
     byte-swapping between the host format and the soundcard.  Thus,
     endian-ness is not a concern in the following format definitions.
     Note that there are no range checks for floating-point values that
     extend beyond plus/minus 1.0.

     - \e RTAUDIO_SINT8:   8-bit signed integer.
     - \e RTAUDIO_SINT16:  16-bit signed integer.
     - \e RTAUDIO_SINT24:  24-bit signed integer.
     - \e RTAUDIO_SINT32:  32-bit signed integer.
     - \e RTAUDIO_FLOAT32: Normalized between plus/minus 1.0.
     - \e RTAUDIO_FLOAT64: Normalized between plus/minus 1.0.
 */
typedef unsigned long RtAudioFormat;
static const RtAudioFormat RTAUDIO_SINT8 = 0x1;    // 8-bit signed integer.
static const RtAudioFormat RTAUDIO_SINT16 = 0x2;   // 16-bit signed integer.
static const RtAudioFormat RTAUDIO_SINT24 = 0x4;   // 24-bit signed integer.
static const RtAudioFormat RTAUDIO_SINT32 = 0x8;   // 32-bit signed integer.
static const RtAudioFormat RTAUDIO_FLOAT32 = 0x10; // Normalized between plus/minus 1.0.
static const RtAudioFormat RTAUDIO_FLOAT64 = 0x20; // Normalized between plus/minus 1.0.

/*! \typedef typedef unsigned long RtAudioStreamFlags;
    \brief RtAudio stream option flags.

    The following flags can be OR'ed together to allow a client to
    make changes to the default stream behavior:

    - \e RTAUDIO_NONINTERLEAVED:   Use non-interleaved buffers (default = interleaved).
    - \e RTAUDIO_MINIMIZE_LATENCY: Attempt to set stream parameters for lowest possible latency.
    - \e RTAUDIO_HOG_DEVICE:       Attempt grab device for exclusive use.
    - \e RTAUDIO_ALSA_USE_DEFAULT: Use the "default" PCM device (ALSA only).
    - \e RTAUDIO_JACK_DONT_CONNECT: Do not automatically connect ports (JACK only).

    By default, RtAudio streams pass and receive audio data from the
    client in an interleaved format.  By passing the
    RTAUDIO_NONINTERLEAVED flag to the openStream() function, audio
    data will instead be presented in non-interleaved buffers.  In
    this case, each buffer argument in the RtAudioCallback function
    will point to a single array of data, with \c nFrames samples for
    each channel concatenated back-to-back.  For example, the first
    sample of data for the second channel would be located at index \c
    nFrames (assuming the \c buffer pointer was recast to the correct
    data type for the stream).

    Certain audio APIs offer a number of parameters that influence the
    I/O latency of a stream.  By default, RtAudio will attempt to set
    these parameters internally for robust (glitch-free) performance
    (though some APIs, like Windows DirectSound, make this difficult).
    By passing the RTAUDIO_MINIMIZE_LATENCY flag to the openStream()
    function, internal stream settings will be influenced in an attempt
    to minimize stream latency, though possibly at the expense of stream
    performance.

    If the RTAUDIO_HOG_DEVICE flag is set, RtAudio will attempt to
    open the input and/or output stream device(s) for exclusive use.
    Note that this is not possible with all supported audio APIs.

    If the RTAUDIO_SCHEDULE_REALTIME flag is set, RtAudio will attempt
    to select realtime scheduling (round-robin) for the callback thread.

    If the RTAUDIO_ALSA_USE_DEFAULT flag is set, RtAudio will attempt to
    open the "default" PCM device when using the ALSA API. Note that this
    will override any specified input or output device id.

    If the RTAUDIO_JACK_DONT_CONNECT flag is set, RtAudio will not attempt
    to automatically connect the ports of the client to the audio device.
*/
typedef unsigned int RtAudioStreamFlags;
static const RtAudioStreamFlags RTAUDIO_NONINTERLEAVED = 0x1;    // Use non-interleaved buffers (default = interleaved).
static const RtAudioStreamFlags RTAUDIO_MINIMIZE_LATENCY = 0x2;  // Attempt to set stream parameters for lowest possible latency.
static const RtAudioStreamFlags RTAUDIO_HOG_DEVICE = 0x4;        // Attempt grab device and prevent use by others.
static const RtAudioStreamFlags RTAUDIO_SCHEDULE_REALTIME = 0x8; // Try to select realtime scheduling for callback thread.
static const RtAudioStreamFlags RTAUDIO_ALSA_USE_DEFAULT = 0x10; // Use the "default" PCM device (ALSA only).
static const RtAudioStreamFlags RTAUDIO_JACK_DONT_CONNECT = 0x20; // Do not automatically connect ports (JACK only).
static const RtAudioStreamFlags RTAUDIO_ALSA_NONBLOCK = 0x40; // Use non-block mode for alsa io.

/*! \typedef typedef unsigned long RtAudioStreamStatus;
    \brief RtAudio stream status (over- or underflow) flags.

    Notification of a stream over- or underflow is indicated by a
    non-zero stream \c status argument in the RtAudioCallback function.
    The stream status can be one of the following two options,
    depending on whether the stream is open for output and/or input:

    - \e RTAUDIO_INPUT_OVERFLOW:   Input data was discarded because of an overflow condition at the driver.
    - \e RTAUDIO_OUTPUT_UNDERFLOW: The output buffer ran low, likely producing a break in the output sound.
*/
typedef unsigned int RtAudioStreamStatus;
static const RtAudioStreamStatus RTAUDIO_INPUT_OVERFLOW = 0x1;    // Input data was discarded because of an overflow condition at the driver.
static const RtAudioStreamStatus RTAUDIO_OUTPUT_UNDERFLOW = 0x2;  // The output buffer ran low, likely causing a gap in the output sound.

//! RtAudio callback function prototype.
/*!
   All RtAudio clients must create a function of type RtAudioCallback
   to read and/or write data from/to the audio stream.  When the
   underlying audio system is ready for new input or output data, this
   function will be invoked.

   \param outputBuffer For output (or duplex) streams, the client
          should write \c nFrames of audio sample frames into this
          buffer.  This argument should be recast to the datatype
          specified when the stream was opened.  For input-only
          streams, this argument will be NULL.

   \param inputBuffer For input (or duplex) streams, this buffer will
          hold \c nFrames of input audio sample frames.  This
          argument should be recast to the datatype specified when the
          stream was opened.  For output-only streams, this argument
          will be NULL.

   \param nFrames The number of sample frames of input or output
          data in the buffers.  The actual buffer size in bytes is
          dependent on the data type and number of channels in use.

   \param streamTime The number of seconds that have elapsed since the
          stream was started.

   \param status If non-zero, this argument indicates a data overflow
          or underflow condition for the stream.  The particular
          condition can be determined by comparison with the
          RtAudioStreamStatus flags.

   \param userData A pointer to optional data provided by the client
          when opening the stream (default = NULL).

   \return
   To continue normal stream operation, the RtAudioCallback function
   should return a value of zero.  To stop the stream and drain the
   output buffer, the function should return a value of one.  To abort
   the stream immediately, the client should return a value of two.
 */
typedef int (*RtAudioCallback)(void* outputBuffer, void* inputBuffer,
    unsigned int nFrames,
    double streamTime,
    RtAudioStreamStatus status,
    void* userData);

enum RtAudioDeviceParam {
    DEFAULT_CHANGED,
    DEVICE_ADDED,
    DEVICE_REMOVED,
    DEVICE_STATE_CHANGED,
    DEVICE_PROPERTY_CHANGED
};

typedef void (*RtAudioDeviceCallback)(unsigned int deviceId, RtAudioDeviceParam param, void* userData);
typedef std::function<void(const std::string&, RtAudioDeviceParam)> RtAudioDeviceCallbackLambda;

enum RtAudioErrorType {
    RTAUDIO_NO_ERROR = 0,      /*!< No error. */
    RTAUDIO_WARNING,           /*!< A non-critical error. */
    RTAUDIO_UNKNOWN_ERROR,     /*!< An unspecified error type. */
    RTAUDIO_NO_DEVICES_FOUND,  /*!< No devices found on system. */
    RTAUDIO_INVALID_DEVICE,    /*!< An invalid device ID was specified. */
    RTAUDIO_DEVICE_DISCONNECT, /*!< A device in use was disconnected. */
    RTAUDIO_MEMORY_ERROR,      /*!< An error occurred during memory allocation. */
    RTAUDIO_INVALID_PARAMETER, /*!< An invalid parameter was specified to a function. */
    RTAUDIO_INVALID_USE,       /*!< The function was called incorrectly. */
    RTAUDIO_DRIVER_ERROR,      /*!< A system driver error occurred. */
    RTAUDIO_SYSTEM_ERROR,      /*!< A system error occurred. */
    RTAUDIO_THREAD_ERROR       /*!< A thread error occurred. */
};

//! RtAudio error callback function prototype.
/*!
    \param type Type of error.
    \param errorText Error description.
 */
typedef std::function<void(RtAudioErrorType type,
    const std::string& errorText)>
    RtAudioErrorCallback;

// **************************************************************** //
//
// RtAudio class declaration.
//
// RtAudio is a "controller" used to select an available audio i/o
// interface.  It presents a common API for the user to call but all
// functionality is implemented by the class RtApi and its
// subclasses.  RtAudio creates an instance of an RtApi subclass
// based on the user's API choice.  If no choice is made, RtAudio
// attempts to make a "logical" API selection.
//
// **************************************************************** //

class RtApi;

class RtApiStreamClassFactory;
class RtApiProber;
class RtApiEnumerator;
class RtApiSystemCallback;


class RTAUDIO_DLL_PUBLIC RtAudio
{
public:
    //! Audio API specifier arguments.
    enum Api {
        UNSPECIFIED,    /*!< Search for a working compiled API. */
        MACOSX_CORE,    /*!< Macintosh OS-X Core Audio API. */
        LINUX_ALSA,     /*!< The Advanced Linux Sound Architecture API. */
        UNIX_JACK,      /*!< The Jack Low-Latency Audio Server API. */
        LINUX_PULSE,    /*!< The Linux PulseAudio API. */
        WINDOWS_ASIO,   /*!< The Steinberg Audio Stream I/O API. */
        WINDOWS_WASAPI, /*!< The Microsoft WASAPI API. */
        RTAUDIO_DUMMY,  /*!< A compilable but non-functional API. */
        NUM_APIS        /*!< Number of values in this enum. */
    };

    static std::shared_ptr<RtApiEnumerator> GetRtAudioEnumerator(RtAudio::Api api);

    static std::shared_ptr<RtApiProber> GetRtAudioProber(RtAudio::Api api);

    static std::shared_ptr<RtApiStreamClassFactory> GetRtAudioStreamFactory(RtAudio::Api api);

    static std::shared_ptr<RtApiSystemCallback> GetRtAudioSystemCallback(RtAudio::Api api, RtAudioDeviceCallbackLambda callback);

    struct DeviceInfoPartial {
        std::string name;               /*!< Character string device name. */
        std::string busID;              /*!< Unique ID of device bus. */
        bool supportsOutput = false;
        bool supportsInput = false;
    };
    //! The public device information structure for returning queried values.
    struct DeviceInfo {
        DeviceInfoPartial partial{};
        unsigned int outputChannels{};  /*!< Maximum output channels supported by device. */
        unsigned int inputChannels{};   /*!< Maximum input channels supported by device. */
        unsigned int duplexChannels{};  /*!< Maximum simultaneous input/output channels supported by device. */
        bool isDefaultOutput{ false };         /*!< true if this is the default output device. */
        bool isDefaultInput{ false };          /*!< true if this is the default input device. */
        std::vector<unsigned int> sampleRates; /*!< Supported sample rates (queried from list of standard rates). */
        unsigned int currentSampleRate{};   /*!< Current sample rate, system sample rate as currently configured. */
        unsigned int preferredSampleRate{}; /*!< Preferred sample rate, e.g. for WASAPI the system sample rate. */
        RtAudioFormat nativeFormats{};  /*!< Bit mask of supported data formats. */
    };

    //! The structure for specifying input or output stream parameters.
    struct StreamParameters {
        //std::string deviceName{};     /*!< Device name from device list. */
        std::string deviceId{};     /*!< Device id as provided by getDeviceIds(). */
        unsigned int nChannels{};    /*!< Number of channels. */
        unsigned int firstChannel{}; /*!< First channel index on device (default = 0). */
    };

    //! The structure for specifying stream options.
    /*!
      The following flags can be OR'ed together to allow a client to
      make changes to the default stream behavior:

      - \e RTAUDIO_NONINTERLEAVED:    Use non-interleaved buffers (default = interleaved).
      - \e RTAUDIO_MINIMIZE_LATENCY:  Attempt to set stream parameters for lowest possible latency.
      - \e RTAUDIO_HOG_DEVICE:        Attempt grab device for exclusive use.
      - \e RTAUDIO_SCHEDULE_REALTIME: Attempt to select realtime scheduling for callback thread.
      - \e RTAUDIO_ALSA_USE_DEFAULT:  Use the "default" PCM device (ALSA only).

      By default, RtAudio streams pass and receive audio data from the
      client in an interleaved format.  By passing the
      RTAUDIO_NONINTERLEAVED flag to the openStream() function, audio
      data will instead be presented in non-interleaved buffers.  In
      this case, each buffer argument in the RtAudioCallback function
      will point to a single array of data, with \c nFrames samples for
      each channel concatenated back-to-back.  For example, the first
      sample of data for the second channel would be located at index \c
      nFrames (assuming the \c buffer pointer was recast to the correct
      data type for the stream).

      Certain audio APIs offer a number of parameters that influence the
      I/O latency of a stream.  By default, RtAudio will attempt to set
      these parameters internally for robust (glitch-free) performance
      (though some APIs, like Windows DirectSound, make this difficult).
      By passing the RTAUDIO_MINIMIZE_LATENCY flag to the openStream()
      function, internal stream settings will be influenced in an attempt
      to minimize stream latency, though possibly at the expense of stream
      performance.

      If the RTAUDIO_HOG_DEVICE flag is set, RtAudio will attempt to
      open the input and/or output stream device(s) for exclusive use.
      Note that this is not possible with all supported audio APIs.

      If the RTAUDIO_SCHEDULE_REALTIME flag is set, RtAudio will attempt
      to select realtime scheduling (round-robin) for the callback thread.
      The \c priority parameter will only be used if the RTAUDIO_SCHEDULE_REALTIME
      flag is set. It defines the thread's realtime priority.

      If the RTAUDIO_ALSA_USE_DEFAULT flag is set, RtAudio will attempt to
      open the "default" PCM device when using the ALSA API. Note that this
      will override any specified input or output device id.

      The \c numberOfBuffers parameter can be used to control stream
      latency in the Windows DirectSound, Linux OSS, and Linux Alsa APIs
      only.  A value of two is usually the smallest allowed.  Larger
      numbers can potentially result in more robust stream performance,
      though likely at the cost of stream latency.  The value set by the
      user is replaced during execution of the RtAudio::openStream()
      function by the value actually used by the system.

      The \c streamName parameter can be used to set the client name
      when using the Jack API or the application name when using the
      Pulse API.  By default, the Jack client name is set to RtApiJack.
      However, if you wish to create multiple instances of RtAudio with
      Jack, each instance must have a unique client name. The default
      Pulse application name is set to "RtAudio."
    */
    struct StreamOptions {
        RtAudioStreamFlags flags{};      /*!< A bit-mask of stream flags (RTAUDIO_NONINTERLEAVED, RTAUDIO_MINIMIZE_LATENCY, RTAUDIO_HOG_DEVICE, RTAUDIO_ALSA_USE_DEFAULT). */
        unsigned int numberOfBuffers{};  /*!< Number of stream buffers. */
        std::string streamName;        /*!< A stream name (currently used only in Jack and Pulse). */
        int priority{};                  /*!< Scheduling priority of callback thread (only used with flag RTAUDIO_SCHEDULE_REALTIME). */
    };

    //! A static function to determine the current RtAudio version.
    static std::string getVersion(void);

    //! A static function to determine the available compiled audio APIs.
    /*!
      The values returned in the std::vector can be compared against
      the enumerated list values.  Note that there can be more than one
      API compiled for certain operating systems.
    */
    static void getCompiledApi(std::vector<RtAudio::Api>& apis);

    //! Return the name of a specified compiled audio API.
    /*!
      This obtains a short lower-case name used for identification purposes.
      This value is guaranteed to remain identical across library versions.
      If the API is unknown, this function will return the empty string.
    */
    static std::string getApiName(RtAudio::Api api);

    //! Return the display name of a specified compiled audio API.
    /*!
      This obtains a long name used for display purposes.
      If the API is unknown, this function will return the empty string.
    */
    static std::string getApiDisplayName(RtAudio::Api api);

    //! Return the compiled audio API having the given name.
    /*!
      A case insensitive comparison will check the specified name
      against the list of compiled APIs, and return the one that
      matches. On failure, the function returns UNSPECIFIED.
    */
    static RtAudio::Api getCompiledApiByName(const std::string& name);

    //! Return the compiled audio API having the given display name.
    /*!
      A case sensitive comparison will check the specified display name
      against the list of compiled APIs, and return the one that
      matches. On failure, the function returns UNSPECIFIED.
    */
    static RtAudio::Api getCompiledApiByDisplayName(const std::string& name);

    //! The class constructor.
    /*!
      The constructor attempts to create an RtApi instance.

      If an API argument is specified but that API has not been
      compiled, a warning is issued and an instance of an available API
      is created. If no compiled API is found, the routine will abort
      (though this should be impossible because RtDummy is the default
      if no API-specific preprocessor definition is provided to the
      compiler). If no API argument is specified and multiple API
      support has been compiled, the default order of use is JACK, ALSA,
      OSS (Linux systems) and ASIO, DS (Windows systems).

      An optional errorCallback function can be specified to
      subsequently receive warning and error messages.
    */
    RtAudio(RtAudio::Api api = UNSPECIFIED, RtAudioErrorCallback&& errorCallback = 0);

    //! The destructor.
    /*!
      If a stream is running or open, it will be stopped and closed
      automatically.
    */
    ~RtAudio();

    //! Returns the audio API specifier for the current instance of RtAudio.
    RtAudio::Api getCurrentApi(void);

    std::vector<RtAudio::DeviceInfo> getDeviceInfosNoProbe(void);

    //! Return an RtAudio::DeviceInfo structure for a specified device bus ID.
    RtAudio::DeviceInfo getDeviceInfoByBusID(std::string busID);

    //! A public function for opening a stream with the specified parameters.
    /*!
      An RTAUDIO_SYSTEM_ERROR is returned if a stream cannot be
      opened with the specified parameters or an error occurs during
      processing.  An RTAUDIO_INVALID_USE is returned if a stream
      is already open or any invalid stream parameters are specified.

      \param outputParameters Specifies output stream parameters to use
             when opening a stream, including a device ID, number of channels,
             and starting channel number.  For input-only streams, this
             argument should be NULL.  The device ID is a value returned by
             getDeviceIds().
      \param inputParameters Specifies input stream parameters to use
             when opening a stream, including a device ID, number of channels,
             and starting channel number.  For output-only streams, this
             argument should be NULL.  The device ID is a value returned by
             getDeviceIds().
      \param format An RtAudioFormat specifying the desired sample data format.
      \param sampleRate The desired sample rate (sample frames per second).
      \param bufferFrames A pointer to a value indicating the desired
             internal buffer size in sample frames.  The actual value
             used by the device is returned via the same pointer.  A
             value of zero can be specified, in which case the lowest
             allowable value is determined.
      \param callback A client-defined function that will be invoked
             when input data is available and/or output data is needed.
      \param userData An optional pointer to data that can be accessed
             from within the callback function.
      \param options An optional pointer to a structure containing various
             global stream options, including a list of OR'ed RtAudioStreamFlags
             and a suggested number of stream buffers that can be used to
             control stream latency.  More buffers typically result in more
             robust performance, though at a cost of greater latency.  If a
             value of zero is specified, a system-specific median value is
             chosen.  If the RTAUDIO_MINIMIZE_LATENCY flag bit is set, the
             lowest allowable value is used.  The actual value used is
             returned via the structure argument.  The parameter is API dependent.
    */
    RtAudioErrorType openStream(RtAudio::StreamParameters* outputParameters,
        RtAudio::StreamParameters* inputParameters,
        RtAudioFormat format, unsigned int sampleRate,
        unsigned int* bufferFrames, RtAudioCallback callback,
        void* userData = NULL, RtAudio::StreamOptions* options = NULL);

    //! A function that closes a stream and frees any associated stream memory.
    /*!
      If a stream is not open, an RTAUDIO_WARNING will be passed to the
      user-provided errorCallback function (or otherwise printed to
      stderr).
    */
    void closeStream(void);

    //! A function that starts a stream.
    /*!
      An RTAUDIO_SYSTEM_ERROR is returned if an error occurs during
      processing. An RTAUDIO_WARNING is returned if a stream is not open
      or is already running.
    */
    RtAudioErrorType startStream(void);

    //! Stop a stream, allowing any samples remaining in the output queue to be played.
    /*!
      An RTAUDIO_SYSTEM_ERROR is returned if an error occurs during
      processing.  An RTAUDIO_WARNING is returned if a stream is not
      open or is already stopped.
    */
    RtAudioErrorType stopStream(void);

    //! Stop a stream, discarding any samples remaining in the input/output queue.
    /*!
      An RTAUDIO_SYSTEM_ERROR is returned if an error occurs during
      processing.  An RTAUDIO_WARNING is returned if a stream is not
      open or is already stopped.
    */
    RtAudioErrorType abortStream(void);

    RtAudioErrorType registerExtraCallback(RtAudioDeviceCallback callback, void* userData);
    RtAudioErrorType unregisterExtraCallback();

    //! Retrieve the error message corresponding to the last error or warning condition.
    /*!
      This function can be used to get a detailed error message when a
      non-zero RtAudioErrorType is returned by a function. This is the
      same message sent to the user-provided errorCallback function.
    */
    const std::string getErrorText(void);

    //! Returns true if a stream is open and false if not.
    bool isStreamOpen(void) const;

    //! Returns true if the stream is running and false if it is stopped or not open.
    bool isStreamRunning(void) const;

    //! Returns the number of seconds of processed data since the stream was started.
    /*!
      The stream time is calculated from the number of sample frames
      processed by the underlying audio system, which will increment by
      units of the audio buffer size. It is not an absolute running
      time. If a stream is not open, the returned value may not be
      valid.
    */
    double getStreamTime(void);

    //! Set the stream time to a time in seconds greater than or equal to 0.0.
    void setStreamTime(double time);

    //! Returns the internal stream latency in sample frames.
    /*!
      The stream latency refers to delay in audio input and/or output
      caused by internal buffering by the audio system and/or hardware.
      For duplex streams, the returned value will represent the sum of
      the input and output latencies.  If a stream is not open, the
      returned value will be invalid.  If the API does not report
      latency, the return value will be zero.
    */
    long getStreamLatency(void);

    //! Returns actual sample rate in use by the (open) stream.
    /*!
      On some systems, the sample rate used may be slightly different
      than that specified in the stream parameters.  If a stream is not
      open, a value of zero is returned.
    */
    unsigned int getStreamSampleRate(void);

    //! Set a client-defined function that will be invoked when an error or warning occurs.
    void setErrorCallback(RtAudioErrorCallback errorCallback);

    //! Specify whether warning messages should be output or not.
    /*!
      The default behaviour is for warning messages to be output,
      either to a client-defined error callback function (if specified)
      or to stderr.
    */
    void showWarnings(bool value = true);

    RtAudioErrorType openAsioControlPanel(void);

protected:

    void openRtApi(RtAudio::Api api);
    RtApi* rtapi_;
};

// Operating system dependent thread functionality.
#if defined(_MSC_VER)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#include <stdint.h>

typedef uintptr_t ThreadHandle;
typedef CRITICAL_SECTION StreamMutex;

#else

  // Using pthread library for various flavors of unix.
#include <pthread.h>

typedef pthread_t ThreadHandle;
typedef pthread_mutex_t StreamMutex;

#endif

// Setup for "dummy" behavior if no apis specified.
#if !(defined(__WINDOWS_DS__) || defined(__WINDOWS_ASIO__) || defined(__WINDOWS_WASAPI__) \
      || defined(__LINUX_ALSA__) || defined(__LINUX_PULSE__) || defined(__UNIX_JACK__) \
      || defined(__LINUX_OSS__) || defined(__MACOSX_CORE__))

#define __RTAUDIO_DUMMY__

#endif

// This global structure type is used to pass callback information
// between the private RtAudio stream structure and global callback
// handling functions.
struct CallbackInfo {
    void* object{};    // Used as a "this" pointer.
    ThreadHandle thread{};
    void* callback{};
    void* userData{};
    void* apiInfo{};   // void pointer for API specific callback information
    bool isRunning{ false };
    bool doRealtime{ false };
    int priority{};
    bool deviceDisconnected{ false };
};

// **************************************************************** //
//
// RtApi class declaration.
//
// Subclasses of RtApi contain all API- and OS-specific code necessary
// to fully implement the RtAudio API.
//
// Note that RtApi is an abstract base class and cannot be
// explicitly instantiated.  The class RtAudio will create an
// instance of an RtApi subclass (RtApiOss, RtApiAlsa,
// RtApiJack, RtApiCore, RtApiDs, or RtApiAsio).
//
// **************************************************************** //

#pragma pack(push, 1)
class S24 {

protected:
    unsigned char c3[3];

public:
    S24() {}

    S24& operator = (const int& i) {
        c3[0] = (unsigned char)(i & 0x000000ff);
        c3[1] = (unsigned char)((i & 0x0000ff00) >> 8);
        c3[2] = (unsigned char)((i & 0x00ff0000) >> 16);
        return *this;
    }

    S24(const double& d) { *this = (int)d; }
    S24(const float& f) { *this = (int)f; }
    S24(const signed short& s) { *this = (int)s; }
    S24(const char& c) { *this = (int)c; }

    int asInt() {
        int i = c3[0] | (c3[1] << 8) | (c3[2] << 16);
        if (i & 0x800000) i |= ~0xffffff;
        return i;
    }
};
#pragma pack(pop)

#if defined( HAVE_GETTIMEOFDAY )
#include <sys/time.h>
#endif

#include <sstream>

class RTAUDIO_DLL_PUBLIC ErrorBase {
public:
    virtual ~ErrorBase() {}

    void setErrorCallback(RtAudioErrorCallback errorCallback) { errorCallback_ = errorCallback; }
    void showWarnings(bool value) { showWarnings_ = value; }
protected:
    std::ostringstream errorStream_;
    RtAudioErrorCallback errorCallback_ = nullptr;
    RtAudioErrorType error(RtAudioErrorType type, const std::string& message);
    RtAudioErrorType errorThread(RtAudioErrorType type, const std::string& message);
    RtAudioErrorType error(RtAudioErrorType type);
private:
    std::string errorText_;
    bool showWarnings_ = false;
};

class RTAUDIO_DLL_PUBLIC RtApiEnumerator : public ErrorBase {
public:
    virtual RtAudio::Api getCurrentApi(void) = 0;
    virtual std::vector<RtAudio::DeviceInfoPartial> listDevices(void) = 0;
};

class RTAUDIO_DLL_PUBLIC RtApiProber : public ErrorBase {
public:
    RtApiProber() {}
    virtual ~RtApiProber() {}
    virtual RtAudio::Api getCurrentApi(void) = 0;
    virtual std::optional<RtAudio::DeviceInfo> probeDevice(const std::string& busId) = 0;
};

class RTAUDIO_DLL_PUBLIC RtApiSystemCallback : public ErrorBase {
public:
    RtApiSystemCallback() {}
    virtual ~RtApiSystemCallback() {}
    virtual RtAudio::Api getCurrentApi(void) = 0;
};

class RTAUDIO_DLL_PUBLIC RtApi
{
public:
    RtApi();
    virtual ~RtApi();
    virtual RtAudio::Api getCurrentApi(void) = 0;
    std::vector<RtAudio::DeviceInfo> getDeviceInfosNoProbe(void);
    RtAudio::DeviceInfo getDeviceInfoByBusID(std::string busID);
    RtAudioErrorType openStream(RtAudio::StreamParameters* outputParameters,
        RtAudio::StreamParameters* inputParameters,
        RtAudioFormat format, unsigned int sampleRate,
        unsigned int* bufferFrames, RtAudioCallback callback,
        void* userData, RtAudio::StreamOptions* options);
    virtual void closeStream(void);
    virtual RtAudioErrorType startStream(void) = 0;
    virtual RtAudioErrorType stopStream(void) = 0;
    virtual RtAudioErrorType abortStream(void) = 0;
    virtual RtAudioErrorType registerExtraCallback(RtAudioDeviceCallback callback, void* userData);
    virtual RtAudioErrorType unregisterExtraCallback();
    const std::string getErrorText(void) const { return errorText_; }
    long getStreamLatency(void);
    unsigned int getStreamSampleRate(void);
    virtual double getStreamTime(void) const { return stream_.streamTime; }
    virtual void setStreamTime(double time);
    bool isStreamOpen(void) const { return stream_.state != STREAM_CLOSED; }
    bool isStreamRunning(void) const { return stream_.state == STREAM_RUNNING; }
    void setErrorCallback(RtAudioErrorCallback errorCallback) { errorCallback_ = errorCallback; }
    void showWarnings(bool value) { showWarnings_ = value; }
    virtual RtAudioErrorType openAsioControlPanel(void);
    static unsigned int formatBytes(RtAudioFormat format);

    enum StreamMode {
        OUTPUT,
        INPUT,
        DUPLEX,
        UNINITIALIZED = -75
    };
    enum { FAILURE, SUCCESS };

    enum StreamState {
        STREAM_STOPPED,
        STREAM_STOPPING,
        STREAM_RUNNING,
        STREAM_ERROR,
        STREAM_CLOSED = -50
    };
    // A protected structure used for buffer conversion.
    struct ConvertInfo {
        int channels;
        int inJump, outJump;
        RtAudioFormat inFormat, outFormat;
        std::vector<int> inOffset;
        std::vector<int> outOffset;
    };

    struct RtApiStream {
        std::string deviceId[2];  // Playback and record, respectively.
        void* apiHandle;           // void pointer for API specific stream handle information
        StreamMode mode;           // OUTPUT, INPUT, or DUPLEX.
        StreamState state;         // STOPPED, RUNNING, or CLOSED
        char* userBuffer[2];       // Playback and record, respectively.
        char* deviceBuffer;
        bool doConvertBuffer[2];   // Playback and record, respectively.
        bool userInterleaved;
        bool deviceInterleaved[2]; // Playback and record, respectively.
        bool doByteSwap[2];        // Playback and record, respectively.
        unsigned int sampleRate;
        unsigned int bufferSize;
        unsigned int nBuffers;
        unsigned int nUserChannels[2];    // Playback and record, respectively.
        unsigned int nDeviceChannels[2];  // Playback and record channels, respectively.
        unsigned int channelOffset[2];    // Playback and record, respectively.
        unsigned long latency[2];         // Playback and record, respectively.
        RtAudioFormat userFormat;
        RtAudioFormat deviceFormat[2];    // Playback and record, respectively.
        StreamMutex mutex;
        CallbackInfo callbackInfo;
        ConvertInfo convertInfo[2];
        double streamTime;         // Number of elapsed seconds since the stream started.

#if defined(HAVE_GETTIMEOFDAY)
        struct timeval lastTickTimestamp;
#endif
    };

    static void convertBuffer(const RtApi::RtApiStream stream_, char* outBuffer, char* inBuffer, RtApi::ConvertInfo info, unsigned int samples, RtApi::StreamMode mode);

protected:

    static const unsigned int MAX_SAMPLE_RATES;
    static const unsigned int SAMPLE_RATES[];

    // A protected structure for audio streams.


    typedef S24 Int24;
    typedef signed short Int16;
    typedef signed int Int32;
    typedef float Float32;
    typedef double Float64;

    std::ostringstream errorStream_;
    std::string errorText_;
    RtAudioErrorCallback errorCallback_;
    bool showWarnings_;
    std::vector<RtAudio::DeviceInfo> deviceList_;
    unsigned int currentDeviceId_;
    RtApiStream stream_;

    /*!
    Protected, api-specific method that attempts to probe device
    information and fill info with params
    */
    virtual bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) = 0;

    /*!
      Protected, api-specific method that attempts to list all device
      in a system without probing.
    */
    virtual void listDevices(void) = 0;

    /*!
      Protected, api-specific method that attempts to open a device
      with the given parameters.  This function MUST be implemented by
      all subclasses.  If an error is encountered during the probe, a
      "warning" message is reported and FAILURE is returned. A
      successful probe is indicated by a return value of SUCCESS.
    */
    virtual bool probeDeviceOpen(const std::string& deviceId, StreamMode mode, unsigned int channels,
        unsigned int firstChannel, unsigned int sampleRate,
        RtAudioFormat format, unsigned int* bufferSize,
        RtAudio::StreamOptions* options);

    //! A protected function used to increment the stream time.
    void tickStreamTime(void);

    //! Protected common method to clear an RtApiStream structure.
    void clearStreamInfo();

    //! Protected common error method to allow global control over error handling.
    RtAudioErrorType error(RtAudioErrorType type);

    RtAudioErrorType errorText(RtAudioErrorType type, const std::string& errorText);

    //! Protected common method used to perform byte-swapping on buffers.
    void byteSwapBuffer(char* buffer, unsigned int samples, RtAudioFormat format);

    //! Protected common method that sets up the parameters for buffer conversion.
    void setConvertInfo(StreamMode mode, unsigned int firstChannel);
};

class RTAUDIO_DLL_PUBLIC RtApiStreamClass : public ErrorBase {
public:
    RtApiStreamClass(RtApi::RtApiStream stream);
    virtual ~RtApiStreamClass();
    virtual RtAudio::Api getCurrentApi(void) = 0;

    virtual RtAudioErrorType startStream(void) = 0;
    virtual RtAudioErrorType stopStream(void) = 0;

    bool isStreamRunning();

    double getStreamTime(void) const { return stream_.streamTime; }
    void tickStreamTime(void) { stream_.streamTime += (stream_.bufferSize * 1.0 / stream_.sampleRate); }
protected:
    RtApi::RtApiStream stream_;
};

class RTAUDIO_DLL_PUBLIC RtApiStreamClassFactory : public ErrorBase {
public:
    RtApiStreamClassFactory() {}
    virtual ~RtApiStreamClassFactory() {}
    virtual RtAudio::Api getCurrentApi(void) = 0;
    virtual std::shared_ptr<RtApiStreamClass> createStream(const std::string& busId, RtApi::StreamMode mode, unsigned int channels, unsigned int sampleRate, RtAudioFormat format, unsigned int bufferSize, RtAudioCallback callback,
        void* userData, RtAudio::StreamOptions* options) = 0;
};

// **************************************************************** //
//
// Inline RtAudio definitions.
//
// **************************************************************** //

inline RtAudio::Api RtAudio::getCurrentApi(void) { if (rtapi_)return rtapi_->getCurrentApi(); return RtAudio::UNSPECIFIED; }
inline RtAudio::DeviceInfo RtAudio::getDeviceInfoByBusID(std::string busID) { return rtapi_->getDeviceInfoByBusID(busID); }
inline std::vector<RtAudio::DeviceInfo> RtAudio::getDeviceInfosNoProbe(void) { return rtapi_->getDeviceInfosNoProbe(); }
inline void RtAudio::closeStream(void) { return rtapi_->closeStream(); }
inline RtAudioErrorType RtAudio::startStream(void) { return rtapi_->startStream(); }
inline RtAudioErrorType RtAudio::stopStream(void) { return rtapi_->stopStream(); }
inline RtAudioErrorType RtAudio::abortStream(void) { return rtapi_->abortStream(); }
inline RtAudioErrorType RtAudio::registerExtraCallback(RtAudioDeviceCallback callback, void* userData)
{
    return rtapi_->registerExtraCallback(callback, userData);
}
inline RtAudioErrorType RtAudio::unregisterExtraCallback()
{
    return rtapi_->unregisterExtraCallback();
}
inline const std::string RtAudio::getErrorText(void) { return rtapi_->getErrorText(); }
inline bool RtAudio::isStreamOpen(void) const { return rtapi_->isStreamOpen(); }
inline bool RtAudio::isStreamRunning(void) const { return rtapi_->isStreamRunning(); }
inline long RtAudio::getStreamLatency(void) { return rtapi_->getStreamLatency(); }
inline unsigned int RtAudio::getStreamSampleRate(void) { return rtapi_->getStreamSampleRate(); }
inline double RtAudio::getStreamTime(void) { return rtapi_->getStreamTime(); }
inline void RtAudio::setStreamTime(double time) { return rtapi_->setStreamTime(time); }
inline void RtAudio::setErrorCallback(RtAudioErrorCallback errorCallback) { rtapi_->setErrorCallback(errorCallback); }
inline void RtAudio::showWarnings(bool value) { rtapi_->showWarnings(value); }
inline RtAudioErrorType RtAudio::openAsioControlPanel(void) { return rtapi_->openAsioControlPanel(); };
#endif

// Indentation settings for Vim and Emacs
//
// Local Variables:
// c-basic-offset: 2
// indent-tabs-mode: nil
// End:
//
// vim: et sts=2 sw=2
