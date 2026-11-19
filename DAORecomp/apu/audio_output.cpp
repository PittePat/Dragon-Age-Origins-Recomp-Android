// Host audio output: an SDL3 stream that plays the 5.1 frames the game's XAudio2
// submits (dao::apu::PopFrame). SDL converts to the device's layout (e.g. downmixes
// to stereo) and rate. On Android the 5.1 frame is folded to stereo here, before SDL:
// handed six channels, Android's own downmix drops the centre channel, where the game
// puts dialogue, so speech came out faint (Mass Effect and State of Decay, same code).
#include "stdafx.h"
#include "apu/audio.h"
#include "apu/audio_output.h"

#include <SDL3/SDL.h>

namespace dao::apu
{
    namespace
    {
        SDL_AudioStream* s_stream = nullptr;
#if defined(__ANDROID__)
        constexpr uint32_t kOutputChannels = 2;
#else
        constexpr uint32_t kOutputChannels = kFrameChannels;
#endif
        // 5.1 -> stereo fold (ITU-style, the weights of ME1's and SoD's runtimes): centre and
        // surrounds at -3 dB, LFE dropped, then scaled by 1 / (1 + 0.707) so a full-scale front
        // plus centre doesn't clip. XAudio2's 5.1 order: FL, FR, FC, LFE, BL, BR.
        constexpr float kFoldCenter = 0.70710678f;
        constexpr float kFoldSurround = 0.70710678f;
        constexpr float kFoldScale = 0.58578644f;
        uint64_t s_underruns = 0;
        bool s_gameAudioStarted = false; // (the silence before the game's first frame isn't an underrun)
        FILE* s_dump = nullptr; // DAO_AUDIO_DUMP=<path.wav>: what's played, as float WAV (2 channels on Android)
        uint32_t s_dumpBytes = 0;

        void UpdateDumpHeader()
        {
            struct
            {
                char riff[4]; uint32_t riffSize; char wave[4];
                char fmt[4]; uint32_t fmtSize; uint16_t format, channels; uint32_t rate, byteRate;
                uint16_t blockAlign, bits;
                char data[4]; uint32_t dataSize;
            } header = { { 'R', 'I', 'F', 'F' }, 36 + s_dumpBytes, { 'W', 'A', 'V', 'E' }, { 'f', 'm', 't', ' ' }, 16,
                3, uint16_t(kOutputChannels), 48000, 48000 * kOutputChannels * 4, uint16_t(kOutputChannels * 4), 32,
                { 'd', 'a', 't', 'a' }, s_dumpBytes };
            fseek(s_dump, 0, SEEK_SET);
            fwrite(&header, sizeof(header), 1, s_dump);
            fseek(s_dump, 0, SEEK_END);
        }

        // One planar 5.1 frame as the output's interleaved samples (stereo folded on Android).
        void Interleave(const float* planar, float* out)
        {
            if constexpr (kOutputChannels == 2)
            {
                const float* fl = planar + 0 * kFrameChannelSamples;
                const float* fr = planar + 1 * kFrameChannelSamples;
                const float* fc = planar + 2 * kFrameChannelSamples;
                const float* bl = planar + 4 * kFrameChannelSamples;
                const float* br = planar + 5 * kFrameChannelSamples;
                for (uint32_t s = 0; s < kFrameChannelSamples; s++)
                {
                    const float mid = fc[s] * kFoldCenter;
                    out[s * 2] = std::clamp((fl[s] + mid + bl[s] * kFoldSurround) * kFoldScale, -1.0f, 1.0f);
                    out[s * 2 + 1] = std::clamp((fr[s] + mid + br[s] * kFoldSurround) * kFoldScale, -1.0f, 1.0f);
                }
            }
            else
            {
                for (uint32_t s = 0; s < kFrameChannelSamples; s++)
                {
                    for (uint32_t c = 0; c < kFrameChannels; c++)
                        out[s * kFrameChannels + c] = planar[c * kFrameChannelSamples + s];
                }
            }
        }

        // Called by SDL's audio thread whenever the device needs more data.
        void SDLCALL FeedDevice(void*, SDL_AudioStream* stream, int additionalAmount, int)
        {
            constexpr int kFrameBytes = int(kFrameChannelSamples * kOutputChannels * sizeof(float));
            float planar[kFrameSampleCount];
            float interleaved[kFrameChannelSamples * kOutputChannels];
            while (additionalAmount > 0)
            {
                if (PopFrame(planar))
                {
                    s_gameAudioStarted = true;
                    Interleave(planar, interleaved);
                }
                else
                {
                    // The game hasn't produced audio in time (or isn't running audio yet).
                    memset(interleaved, 0, sizeof(interleaved));
                    if (s_gameAudioStarted)
                    {
                        const uint64_t underruns = ++s_underruns;
                        if (underruns == 100 || underruns == 1000 || underruns % 10000 == 0)
                            LOGW("Audio output: game audio is not keeping up ({} underruns so far)", underruns);
                    }
                }
                SDL_PutAudioStreamData(stream, interleaved, kFrameBytes);
                if (s_dump)
                {
                    fwrite(interleaved, kFrameBytes, 1, s_dump);
                    s_dumpBytes += kFrameBytes;
                    if ((s_dumpBytes / kFrameBytes) % 188 == 0) // ~1 s
                        UpdateDumpHeader();
                }
                additionalAmount -= kFrameBytes;
            }
        }
    }

    bool InitializeAudioOutput()
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            LOGW("Audio output: SDL audio init failed: {}", SDL_GetError());
            return false;
        }
        const SDL_AudioSpec spec{ SDL_AUDIO_F32, int(kOutputChannels), 48000 };
        s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedDevice, nullptr);
        if (!s_stream)
        {
            LOGW("Audio output: can't open the playback device: {}", SDL_GetError());
            return false;
        }
        if (const char* dump = getenv("DAO_AUDIO_DUMP"))
        {
            s_dump = fopen(dump, "wb");
            if (s_dump)
                UpdateDumpHeader();
        }
        SDL_ResumeAudioStreamDevice(s_stream);
        LOGI("Audio output: {} via SDL {}", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
            SDL_GetVersion());
        return true;
    }
}
