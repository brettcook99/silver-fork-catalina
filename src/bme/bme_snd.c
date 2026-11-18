//
// BME (Blasphemous Multimedia Engine) sound main module
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// On macOS the sound goes straight to Core Audio, through an AUHAL output
// unit. SDL2 plays through an AudioQueue there, which keeps about 30 ms queued
// whatever the buffer size. SDL is still used if the unit can't be opened.
// The plugin gets its sound from the host and has no output of its own
#if defined(__APPLE__) && !defined(GT_PLUGIN)
#define BME_COREAUDIO
#endif

// These come before the BME headers, which define LEFT, RIGHT, MONO and so on
#ifdef BME_COREAUDIO
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#endif

#include <SDL.h>
#include "bme_main.h"
#include "bme_cfg.h"
#include "bme_win.h"
#include "bme_io.h"
#include "bme_err.h"

// Prototypes
int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength, unsigned channels, int usedirectsound);
void snd_uninit(void);
void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples));
static int snd_initmixer(void);
static void snd_uninitmixer(void);
static void snd_mixdata(Uint8 *dest, unsigned bytes);
static void snd_mixchannels(Sint32 *dest, unsigned samples);
static void snd_mixer(void *userdata, Uint8 *stream, int len);

// Lowlevel mixing functions
static void snd_clearclipbuffer(Sint32 *clipbuffer, unsigned clipsamples);
static void snd_mixchannel(CHANNEL *chptr, Sint32 *dest, unsigned samples);
static void snd_16bit_postprocess(Sint32 *src, Sint16 *dest, unsigned samples);
static void snd_8bit_postprocess(Sint32 *src, Uint8 *dest, unsigned samples);

void (*snd_player)(void) = NULL;
CHANNEL *snd_channel = NULL;
int snd_channels = 0;
int snd_sndinitted = 0;
int snd_bpmcount;
int snd_bpmtempo = 125;
unsigned snd_mixmode;
unsigned snd_mixrate;
const char *snd_devicename = NULL;
unsigned snd_buffersamples = 0;
unsigned snd_obtainedsamples = 0;
// The open device (SDL2's device API, so that another than the default
// output can be chosen), 0 = none
static SDL_AudioDeviceID snd_device = 0;

static void (*snd_custommixer)(Sint32 *dest, unsigned samples) = NULL;
static unsigned snd_buffersize;
static unsigned snd_samplesize;
static unsigned snd_previouschannels = 0xffffffff;
static int snd_atexit_registered = 0;
static Sint32 *snd_clipbuffer = NULL;
static SDL_AudioSpec desired;
static SDL_AudioSpec obtained;

#ifdef BME_COREAUDIO

// The main element of a property (kAudioObjectPropertyElementMaster, which
// newer SDKs call kAudioObjectPropertyElementMain)
#define CA_MAINELEMENT 0
#define CA_BYTESPERFRAME 4
// The most samples mixed in one go, longer requests are split
#define CA_MIXFRAMES 4096
#define CA_MAXNAME 512

static AudioUnit snd_caunit = NULL;
static AudioDeviceID snd_cadevice = kAudioObjectUnknown;
// Core Audio is the output in use
static int snd_caactive = 0;
// Playing on the system's default output: follows it when it changes
static int snd_cafollowdefault = 0;
// snd_lock(): held while mixing
static SDL_mutex *snd_camixlock = NULL;
// Held while the unit is opened or closed. The default output can change at
// any time, and that is told on a thread of Core Audio's own
static SDL_mutex *snd_cacontrollock = NULL;

static const AudioObjectPropertyAddress snd_cadefaultaddress =
{
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    CA_MAINELEMENT
};

// Reads a property, returns nonzero if that worked
static int snd_caget(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, void *data, UInt32 size)
{
    AudioObjectPropertyAddress address;

    address.mSelector = selector;
    address.mScope = scope;
    address.mElement = CA_MAINELEMENT;
    return AudioObjectGetPropertyData(object, &address, 0, NULL, &size, data) == noErr;
}

// The number of outputs of a device, 0 = it only has inputs
static unsigned snd_caoutputchannels(AudioDeviceID device)
{
    AudioObjectPropertyAddress address;
    AudioBufferList *list;
    UInt32 size = 0;
    unsigned channels = 0;
    unsigned c;

    address.mSelector = kAudioDevicePropertyStreamConfiguration;
    address.mScope = kAudioObjectPropertyScopeOutput;
    address.mElement = CA_MAINELEMENT;
    if ((AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size) != noErr) || (!size)) return 0;
    list = malloc(size);
    if (!list) return 0;
    if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, list) == noErr)
    {
        for (c = 0; c < list->mNumberBuffers; c++) channels += list->mBuffers[c].mNumberChannels;
    }
    free(list);
    return channels;
}

// The name of a device the way SDL lists it: without spaces at the end
static int snd_cadevicename(AudioDeviceID device, char *name, unsigned size)
{
    CFStringRef cfname = NULL;
    size_t length;
    int ok;

    if (!snd_caget(device, kAudioObjectPropertyName, kAudioObjectPropertyScopeOutput, &cfname, sizeof cfname)) return 0;
    if (!cfname) return 0;
    ok = CFStringGetCString(cfname, name, size, kCFStringEncodingUTF8);
    CFRelease(cfname);
    if (!ok) return 0;
    length = strlen(name);
    while ((length) && (name[length-1] == ' ')) name[--length] = 0;
    return length > 0;
}

// Finds an output by the name shown in the preferences. SDL numbers outputs
// that have the same name: "Name", "Name (2)", "Name (3)"
static AudioDeviceID snd_cafinddevice(const char *name)
{
    AudioObjectPropertyAddress address;
    AudioDeviceID *devices;
    AudioDeviceID found = kAudioObjectUnknown;
    char basename[CA_MAXNAME];
    char devicename[CA_MAXNAME];
    char *number;
    UInt32 size = 0;
    unsigned count;
    unsigned c;
    int wanted = 0;
    int same = 0;

    snprintf(basename, sizeof basename, "%s", name);
    number = strrchr(basename, '(');
    if ((number) && (number > basename) && (number[-1] == ' ') && (basename[strlen(basename)-1] == ')'))
    {
        wanted = atoi(number + 1);
        if (wanted >= 2) number[-1] = 0;
        else wanted = 0;
    }

    address.mSelector = kAudioHardwarePropertyDevices;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = CA_MAINELEMENT;
    if ((AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr) || (!size)) return found;
    devices = malloc(size);
    if (!devices) return found;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices) == noErr)
    {
        count = size / sizeof(AudioDeviceID);
        for (c = 0; (c < count) && (found == kAudioObjectUnknown); c++)
        {
            if (!snd_caoutputchannels(devices[c])) continue;
            if (!snd_cadevicename(devices[c], devicename, sizeof devicename)) continue;
            if (!strcmp(devicename, name)) found = devices[c];
            else if ((wanted) && (!strcmp(devicename, basename)) && (++same == wanted)) found = devices[c];
        }
    }
    free(devices);
    return found;
}

static AudioDeviceID snd_cadefaultdevice(void)
{
    AudioDeviceID device = kAudioObjectUnknown;

    if (!snd_caget(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, &device, sizeof device)) return kAudioObjectUnknown;
    return device;
}

// Asks the device for the buffer length wanted. This counts the device's own
// samples, so it is scaled if the device runs at another rate than the mixer.
// The length only applies to this program, others using the device keep theirs
static void snd_casetbuffer(AudioDeviceID device)
{
    AudioObjectPropertyAddress address;
    AudioValueRange range;
    Float64 rate = 0;
    UInt32 wanted;
    UInt32 actual = 0;
    int c;

    snd_caget(device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeOutput, &rate, sizeof rate);
    if (rate <= 0) rate = desired.freq;
    wanted = (UInt32)(desired.samples * rate / desired.freq + 0.999);
    if (snd_caget(device, kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeOutput, &range, sizeof range))
    {
        if (wanted < range.mMinimum) wanted = (UInt32)range.mMinimum;
        if (wanted > range.mMaximum) wanted = (UInt32)range.mMaximum;
    }

    address.mSelector = kAudioDevicePropertyBufferFrameSize;
    address.mScope = kAudioObjectPropertyScopeOutput;
    address.mElement = CA_MAINELEMENT;
    if (AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof wanted, &wanted) == noErr)
    {
        // The change can take a moment to arrive
        for (c = 0; c < 20; c++)
        {
            if ((snd_caget(device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput, &actual, sizeof actual)) &&
                (actual == wanted)) break;
            SDL_Delay(5);
        }
    }
    snd_caget(device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput, &actual, sizeof actual);

    // What the preferences show, in the mixer's samples
    obtained.samples = desired.samples;
    if ((actual) && (actual != wanted))
    {
        double samples = actual * (double)desired.freq / rate + 0.5;
        obtained.samples = (samples > 65535.0) ? 65535 : (Uint16)samples;
    }
}

// Stereo goes to the outputs chosen as the speakers in Audio MIDI Setup. On
// an audio interface with more than 2 outputs those may not be the first two
static void snd_casetchannelmap(AudioDeviceID device)
{
    UInt32 pair[2] = {1, 2};
    SInt32 *map;
    unsigned channels = snd_caoutputchannels(device);
    unsigned c;

    if (channels < 2) return;
    snd_caget(device, kAudioDevicePropertyPreferredChannelsForStereo, kAudioObjectPropertyScopeOutput, pair, sizeof pair);
    if ((pair[0] < 1) || (pair[0] > channels) || (pair[1] < 1) || (pair[1] > channels) || (pair[0] == pair[1]))
    {
        pair[0] = 1;
        pair[1] = 2;
    }

    // One entry for each output of the device: the channel played on it
    map = malloc(channels * sizeof(SInt32));
    if (!map) return;
    for (c = 0; c < channels; c++) map[c] = -1;
    map[pair[0]-1] = 0;
    map[pair[1]-1] = 1;
    AudioUnitSetProperty(snd_caunit, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Output, 0, map, channels * sizeof(SInt32));
    free(map);
}

// Core Audio asks for sound. This runs on its realtime thread, so it never
// waits: while snd_lock() is held it plays silence. The number of samples
// asked for changes from call to call if the rate is being converted
static OSStatus snd_carender(void *userdata, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *timestamp,
    UInt32 bus, UInt32 frames, AudioBufferList *data)
{
    Uint8 *dest = (Uint8 *)data->mBuffers[0].mData;
    unsigned bytes = frames * CA_BYTESPERFRAME;

    if (bytes > data->mBuffers[0].mDataByteSize) bytes = data->mBuffers[0].mDataByteSize;

    if (SDL_TryLockMutex(snd_camixlock) != 0)
    {
        memset(dest, 0, bytes);
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
    while (bytes)
    {
        unsigned now = bytes;
        if (now > snd_buffersize) now = snd_buffersize;
        snd_mixdata(dest, now);
        dest += now;
        bytes -= now;
    }
    SDL_UnlockMutex(snd_camixlock);
    return noErr;
}

static void snd_caunitclose(void)
{
    if (!snd_caunit) return;
    AudioOutputUnitStop(snd_caunit);
    AudioUnitUninitialize(snd_caunit);
    AudioComponentInstanceDispose(snd_caunit);
    snd_caunit = NULL;
    snd_cadevice = kAudioObjectUnknown;
}

// Opens an output unit on a device, ready to be started
static int snd_caunitopen(AudioDeviceID device)
{
    AudioComponentDescription description;
    AudioComponent component;
    AudioStreamBasicDescription format;
    AURenderCallbackStruct callback;
    UInt32 on = 1;
    UInt32 off = 0;
    UInt32 maxframes = desired.samples * 2;

    memset(&description, 0, sizeof description);
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    component = AudioComponentFindNext(NULL, &description);
    if (!component) return 0;
    if (AudioComponentInstanceNew(component, &snd_caunit) != noErr)
    {
        snd_caunit = NULL;
        return 0;
    }

    // 16-bit stereo at the mixer's rate, the unit converts it for the device
    memset(&format, 0, sizeof format);
    format.mSampleRate = desired.freq;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = CA_BYTESPERFRAME;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = CA_BYTESPERFRAME;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;

    callback.inputProc = snd_carender;
    callback.inputProcRefCon = NULL;

    if (maxframes < CA_MIXFRAMES) maxframes = CA_MIXFRAMES;

    // Output only: with the input on macOS would ask for the microphone
    if ((AudioUnitSetProperty(snd_caunit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &off, sizeof off) != noErr) ||
        (AudioUnitSetProperty(snd_caunit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &on, sizeof on) != noErr) ||
        (AudioUnitSetProperty(snd_caunit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, sizeof device) != noErr) ||
        (AudioUnitSetProperty(snd_caunit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format) != noErr) ||
        (AudioUnitSetProperty(snd_caunit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback) != noErr) ||
        (AudioUnitSetProperty(snd_caunit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxframes, sizeof maxframes) != noErr))
    {
        snd_caunitclose();
        return 0;
    }

    snd_casetchannelmap(device);
    snd_casetbuffer(device);
    if (AudioUnitInitialize(snd_caunit) != noErr)
    {
        snd_caunitclose();
        return 0;
    }
    // Once more, in case initializing the unit changed it
    snd_casetbuffer(device);
    snd_cadevice = device;
    return 1;
}

// With GT_AUDIOLOG set in the environment: tells on stderr what was opened
// and how long the sound takes from the mixer to the output
static void snd_calog(void)
{
    char name[CA_MAXNAME] = "?";
    Float64 rate = 0;
    Float64 unitlatency = 0;
    UInt32 size = sizeof unitlatency;
    UInt32 buffer = 0;
    UInt32 latency = 0;
    UInt32 safety = 0;
    UInt32 streamlatency = 0;
    AudioStreamID stream = 0;

    if (!getenv("GT_AUDIOLOG")) return;

    snd_cadevicename(snd_cadevice, name, sizeof name);
    snd_caget(snd_cadevice, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeOutput, &rate, sizeof rate);
    snd_caget(snd_cadevice, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput, &buffer, sizeof buffer);
    snd_caget(snd_cadevice, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, &latency, sizeof latency);
    snd_caget(snd_cadevice, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, &safety, sizeof safety);
    if (snd_caget(snd_cadevice, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, &stream, sizeof stream))
        snd_caget(stream, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, &streamlatency, sizeof streamlatency);
    AudioUnitGetProperty(snd_caunit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &unitlatency, &size);

    fprintf(stderr, "Core Audio: \"%s\" at %.0f Hz, buffer %u, mixing at %d Hz. Device latency %u + safety offset %u + stream latency %u samples, "
        "rate converter %.2f ms. Output delay %.2f ms\n", name, rate, (unsigned)buffer, desired.freq, (unsigned)latency, (unsigned)safety,
        (unsigned)streamlatency, unitlatency * 1000.0,
        (rate > 0) ? ((double)buffer + latency + safety + streamlatency) * 1000.0 / rate + unitlatency * 1000.0 : 0.0);
}

// The system's default output changed: moves over to the new one
static OSStatus snd_cadefaultchanged(AudioObjectID object, UInt32 addresses, const AudioObjectPropertyAddress *address, void *userdata)
{
    AudioDeviceID device = snd_cadefaultdevice();

    SDL_LockMutex(snd_cacontrollock);
    if ((snd_caactive) && (device != kAudioObjectUnknown) && (device != snd_cadevice))
    {
        snd_caunitclose();
        if (snd_caunitopen(device))
        {
            if (AudioOutputUnitStart(snd_caunit) == noErr)
            {
                snd_obtainedsamples = obtained.samples;
                snd_calog();
            }
            else snd_caunitclose();
        }
    }
    SDL_UnlockMutex(snd_cacontrollock);
    return noErr;
}

// Opens the output chosen in the preferences, returns nonzero if that worked
static int snd_caopen(void)
{
    AudioDeviceID device;

    if ((desired.format != AUDIO_S16SYS) || (desired.channels != 2)) return 0;

    if (!snd_camixlock) snd_camixlock = SDL_CreateMutex();
    if (!snd_cacontrollock) snd_cacontrollock = SDL_CreateMutex();
    if ((!snd_camixlock) || (!snd_cacontrollock)) return 0;

    snd_cafollowdefault = (!snd_devicename) || (!snd_devicename[0]);
    device = snd_cafollowdefault ? snd_cadefaultdevice() : snd_cafinddevice(snd_devicename);
    if (device == kAudioObjectUnknown) return 0;

    obtained = desired;
    obtained.size = ((desired.samples > CA_MIXFRAMES) ? desired.samples : CA_MIXFRAMES) * CA_BYTESPERFRAME;
    return snd_caunitopen(device);
}

static int snd_castart(void)
{
    int ok;

    SDL_LockMutex(snd_cacontrollock);
    ok = AudioOutputUnitStart(snd_caunit) == noErr;
    if (ok)
    {
        snd_caactive = 1;
        snd_calog();
    }
    SDL_UnlockMutex(snd_cacontrollock);

    if ((ok) && (snd_cafollowdefault))
        AudioObjectAddPropertyListener(kAudioObjectSystemObject, &snd_cadefaultaddress, snd_cadefaultchanged, NULL);
    return ok;
}

static void snd_caclose(void)
{
    if (!snd_cacontrollock) return;

    // Not while holding the lock, the function above may be waiting for it
    if ((snd_caactive) && (snd_cafollowdefault))
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &snd_cadefaultaddress, snd_cadefaultchanged, NULL);

    SDL_LockMutex(snd_cacontrollock);
    snd_caunitclose();
    snd_caactive = 0;
    SDL_UnlockMutex(snd_cacontrollock);
}

#endif

// Takes what the output was opened with into use
static int snd_useobtained(void)
{
    snd_sndinitted = 1;
    snd_obtainedsamples = obtained.samples;

    snd_mixmode = 0;
    snd_samplesize = 1;
    if (obtained.channels == 2)
    {
        snd_mixmode |= STEREO;
        snd_samplesize <<= 1;
    }
    if ((obtained.format == AUDIO_S16SYS) ||
       (obtained.format == AUDIO_S16LSB) ||
       (obtained.format == AUDIO_S16MSB))
    {
        snd_mixmode |= SIXTEENBIT;
        snd_samplesize <<= 1;
    }
    snd_buffersize = obtained.size;
    snd_mixrate = obtained.freq;

    // Allocate mixer tables

    return snd_initmixer();
}

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength, unsigned channels, int usedirectsound)
{
    int c;

    // Register snd_uninit as an atexit function

    if (!snd_atexit_registered)
    {
        atexit(snd_uninit);
        snd_atexit_registered = 1;
    }

    // If user wants to re-initialize, shutdown first

    snd_uninit();

    // Check for illegal config

    if ((channels < 1) || (!mixrate) || (!bufferlength))
    {
        bme_error = BME_ILLEGAL_CONFIG;
        snd_uninit();
        return BME_ERROR;
    }

    desired.freq = mixrate;
    desired.format = AUDIO_U8;
    if (mixmode & SIXTEENBIT)
    {
        desired.format = AUDIO_S16SYS;
    }
    desired.channels = 1;
    if (mixmode & STEREO) desired.channels = 2;
    // The length asked for, rounded up (SDL2 doesn't need a power of two).
    // Or a length in samples chosen in the preferences
    {
        unsigned samples = (bufferlength * mixrate + 999) / 1000;
        if (snd_buffersamples) samples = snd_buffersamples;

        if (samples < 32) samples = 32;
        if (samples > 32768) samples = 32768;
        desired.samples = (Uint16)samples;
    }

    desired.callback = snd_mixer;
    desired.userdata = NULL;

    // Init tempo count

    snd_bpmcount = 0;

    // (Re)allocate channels if necessary

    if (snd_previouschannels != channels)
    {
        CHANNEL *chptr;
        if (snd_channel)
        {
            free(snd_channel);
            snd_channel = NULL;
            snd_channels = 0;
        }

        snd_channel = malloc(channels * sizeof(CHANNEL));
        if (!snd_channel)
        {
            bme_error = BME_OUT_OF_MEMORY;
            snd_uninit();
            return BME_ERROR;
        }
        chptr = &snd_channel[0];
        snd_channels = channels;
        snd_previouschannels = channels;

        // Init all channels (no sound played, no sample, mastervolume 64)
        for (c = snd_channels; c > 0; c--)
        {
            chptr->voicemode = VM_OFF;
            chptr->smp = NULL;
            chptr->mastervol = 64;
            chptr++;
        }
    }

    // macOS: Core Audio directly, SDL below only if that doesn't open
    #ifdef BME_COREAUDIO
    if (snd_caopen())
    {
        if (!snd_useobtained())
        {
            bme_error = BME_OUT_OF_MEMORY;
            snd_uninit();
            return BME_ERROR;
        }
        if (snd_castart())
        {
            bme_error = BME_OK;
            return BME_OK;
        }
    }
    snd_caclose();
    #endif

    // SDL2: with no changes allowed the requested format, rate and buffer
    // size are guaranteed (SDL converts if the device wants something else,
    // e.g. float on macOS)
    snd_device = SDL_OpenAudioDevice((snd_devicename && snd_devicename[0]) ? snd_devicename : NULL, 0, &desired, &obtained, 0);
    if (!snd_device)
    {
        bme_error = BME_OPEN_ERROR;
        snd_uninit();
        return BME_ERROR;
    }
    if (!snd_useobtained())
    {
        bme_error = BME_OUT_OF_MEMORY;
        snd_uninit();
        return BME_ERROR;
    }

    SDL_PauseAudioDevice(snd_device, 0);

    bme_error = BME_OK;
    return BME_OK;
}

void snd_uninit(void)
{
    #ifdef BME_COREAUDIO
    snd_caclose();
    #endif
    if (snd_device)
    {
        SDL_CloseAudioDevice(snd_device);
        snd_device = 0;
    }
    snd_sndinitted = 0;
    snd_obtainedsamples = 0;
    snd_uninitmixer();
}

void snd_lock(void)
{
    #ifdef BME_COREAUDIO
    if (snd_caactive) SDL_LockMutex(snd_camixlock);
    #endif
    if (snd_device) SDL_LockAudioDevice(snd_device);
}

void snd_unlock(void)
{
    #ifdef BME_COREAUDIO
    if (snd_caactive) SDL_UnlockMutex(snd_camixlock);
    #endif
    if (snd_device) SDL_UnlockAudioDevice(snd_device);
}

void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples))
{
    snd_custommixer = custommixer;
}

static int snd_initmixer(void)
{
    snd_uninitmixer();

    if (snd_mixmode & STEREO)
    {
        snd_clipbuffer = malloc((snd_buffersize / snd_samplesize) * sizeof(int) * 2);
    }
    else
    {
        snd_clipbuffer = malloc((snd_buffersize / snd_samplesize) * sizeof(int));
    }
    if (!snd_clipbuffer) return 0;

    return 1;
}

static void snd_uninitmixer(void)
{
    if (snd_clipbuffer)
    {
        free(snd_clipbuffer);
        snd_clipbuffer = NULL;
    }
}

static void snd_mixer(void *userdata, Uint8 *stream, int len)
{
    snd_mixdata(stream, len);
}

static void snd_mixdata(Uint8 *dest, unsigned bytes)
{
    unsigned mixsamples = bytes;
    unsigned clipsamples = bytes;
    Sint32 *clipptr = (Sint32 *)snd_clipbuffer;
    if (snd_mixmode & STEREO) mixsamples >>= 1;
    if (snd_mixmode & SIXTEENBIT)
    {
        clipsamples >>= 1;
        mixsamples >>= 1;
    }
    snd_clearclipbuffer(snd_clipbuffer, clipsamples);
    if (snd_player) // Must the player be called?
    {
        int musicsamples;

        while(mixsamples)
        {
            if ((!snd_bpmcount) && (snd_player)) // Player still active?
            {
                // Call player
                snd_player();
                // Reset tempocounter
                snd_bpmcount = ((snd_mixrate * 5) >> 1) / snd_bpmtempo;
            }

            musicsamples = mixsamples;
            if (musicsamples > snd_bpmcount) musicsamples = snd_bpmcount;
            snd_bpmcount -= musicsamples;
            if (!snd_custommixer)
            {
                snd_mixchannels(clipptr, musicsamples);
            }
            else
            {
                snd_custommixer(clipptr, musicsamples);
            }
            if (snd_mixmode & STEREO) clipptr += musicsamples * 2;
            else clipptr += musicsamples;
            mixsamples -= musicsamples;
        }
    }
    else
    {
        if (!snd_custommixer)
        {
            snd_mixchannels(clipptr, mixsamples);
        }
        else
        {
            snd_custommixer(clipptr, mixsamples);
        }
    }

    clipptr = (Sint32 *)snd_clipbuffer;
    if (snd_mixmode & SIXTEENBIT)
    {
        snd_16bit_postprocess(clipptr, (Sint16 *)dest, clipsamples);
    }
    else
    {
        snd_8bit_postprocess(clipptr, dest, clipsamples);
    }
}

static void snd_mixchannels(Sint32 *dest, unsigned samples)
{
    CHANNEL *chptr = &snd_channel[0];
    int c;

    for (c = snd_channels; c; c--)
    {
        snd_mixchannel(chptr, dest, samples);
        chptr++;
    }
}

static void snd_clearclipbuffer(Sint32 *clipbuffer, unsigned clipsamples)
{
    memset(clipbuffer, 0, clipsamples*sizeof(int));
}

static void snd_16bit_postprocess(Sint32 *src, Sint16 *dest, unsigned samples)
{
    while (samples--)
    {
        int sample = *src++;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *dest++ = sample;
    }
}

static void snd_8bit_postprocess(Sint32 *src, Uint8 *dest, unsigned samples)
{
    while (samples--)
    {
          int sample = *src++;
          if (sample > 32767) sample = 32767;
          if (sample < -32768) sample = -32768;
          *dest++ = (sample >> 8) + 128;
    }
}

static void snd_mixchannel(CHANNEL *chptr, Sint32 *dest, unsigned samples)
{
    if (chptr->voicemode & VM_ON)
    {
          unsigned freq, intadd, fractadd;

          freq = chptr->freq;
          if (freq > 535232) freq = 535232;
          intadd = freq / snd_mixrate;
          fractadd = (((freq % snd_mixrate) << 16) / snd_mixrate) & 65535;

          if (snd_mixmode & STEREO)
          {
                int leftvol = (((chptr->vol * chptr->mastervol) >> 6) * (255-chptr->panning)) >> 7;
                int rightvol = (((chptr->vol * chptr->mastervol) >> 6) * (chptr->panning)) >> 7;
                if (leftvol > 255) leftvol = 255;
                if (rightvol > 255) rightvol = 255;
                if (leftvol < 0) leftvol = 0;
                if (rightvol < 0) rightvol = 0;

                if (chptr->voicemode & VM_16BIT)
                {
                    Sint16 *pos = (Sint16 *)chptr->pos;
                    Sint16 *end = (Sint16 *)chptr->end;
                    Sint16 *repeat = (Sint16 *)chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * leftvol) >> 8);
                                dest++;
                                *dest = *dest + ((*pos * rightvol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * leftvol) >> 8);
                                dest++;
                                *dest = *dest + ((*pos * rightvol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
                else
                {
                    Sint8 *pos = (Sint8 *)chptr->pos;
                    Sint8 *end = chptr->end;
                    Sint8 *repeat = chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * leftvol);
                                dest++;
                                *dest = *dest + (*pos * rightvol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * leftvol);
                                dest++;
                                *dest = *dest + (*pos * rightvol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
          }
          else
          {
                int vol = ((chptr->vol * chptr->mastervol) >> 6);
                if (vol > 255) vol = 255;
                if (vol < 0) vol = 0;

                if (chptr->voicemode & VM_16BIT)
                {
                    Sint16 *pos = (Sint16 *)chptr->pos;
                    Sint16 *end = (Sint16 *)chptr->end;
                    Sint16 *repeat = (Sint16 *)chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * vol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * vol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
                else
                {
                    Sint8 *pos = (Sint8 *)chptr->pos;
                    Sint8 *end = chptr->end;
                    Sint8 *repeat = chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * vol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * vol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
          }
    }
}
