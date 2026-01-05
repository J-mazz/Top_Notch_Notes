// miniaudio.h - Placeholder Header
// 
// This is a placeholder for the miniaudio library.
// Download the actual header from: https://github.com/mackron/miniaudio
//
// miniaudio is a single-file audio library that supports:
// - Audio playback and capture
// - Multiple backends (WASAPI, DirectSound, ALSA, PulseAudio, CoreAudio, etc.)
// - Format conversion and resampling
// - Low-latency operation
//
// To use the real miniaudio:
// 1. Download miniaudio.h from the GitHub repository
// 2. Replace this file with the downloaded header
// 3. In ONE .cpp file, define: #define MINIAUDIO_IMPLEMENTATION
//    before including miniaudio.h
//
// For now, the harness uses a stub AudioDevice implementation.

#ifndef MINIAUDIO_H
#define MINIAUDIO_H

// Stub definitions for compilation without the actual library

#ifdef __cplusplus
extern "C" {
#endif

typedef int ma_result;
#define MA_SUCCESS 0

typedef enum {
    ma_device_type_playback = 1,
    ma_device_type_capture  = 2,
    ma_device_type_duplex   = 3
} ma_device_type;

typedef enum {
    ma_format_unknown = 0,
    ma_format_u8      = 1,
    ma_format_s16     = 2,
    ma_format_s24     = 3,
    ma_format_s32     = 4,
    ma_format_f32     = 5
} ma_format;

typedef struct {
    void* pUserData;
} ma_context;

typedef struct {
    void* pUserData;
} ma_device;

typedef struct {
    ma_device_type deviceType;
    ma_format format;
    unsigned int channels;
    unsigned int sampleRate;
} ma_device_config;

// Stub functions
static inline ma_device_config ma_device_config_init(ma_device_type deviceType) {
    ma_device_config config = {0};
    config.deviceType = deviceType;
    config.format = ma_format_f32;
    config.channels = 1;
    config.sampleRate = 48000;
    return config;
}

static inline ma_result ma_device_init(ma_context* pContext, const ma_device_config* pConfig, ma_device* pDevice) {
    (void)pContext;
    (void)pConfig;
    (void)pDevice;
    return MA_SUCCESS;
}

static inline void ma_device_uninit(ma_device* pDevice) {
    (void)pDevice;
}

static inline ma_result ma_device_start(ma_device* pDevice) {
    (void)pDevice;
    return MA_SUCCESS;
}

static inline ma_result ma_device_stop(ma_device* pDevice) {
    (void)pDevice;
    return MA_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif // MINIAUDIO_H
