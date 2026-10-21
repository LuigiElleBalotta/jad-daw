#include "lpc/compressed_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace lpc {

int compressedTargetRate(int sampleRate) {
    if (sampleRate == 44100 || sampleRate == 48000) return sampleRate;
    return sampleRate % 44100 == 0 ? 44100 : 48000;
}

#ifdef _WIN32

namespace {

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed (0x" + [&] { char b[16]; snprintf(b, sizeof b, "%08lX", static_cast<unsigned long>(hr)); return std::string(b); }() + ")");
}

struct MfSession {
    bool com = false, mf = false;
    MfSession() {
        const HRESULT c = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com = SUCCEEDED(c);  // S_FALSE: this thread already had COM
        check(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup");
        mf = true;
    }
    ~MfSession() {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }
};

// the sink writer for the container, with the output type of the codec
bool openWriter(const std::filesystem::path& path, CompressedFormat format, int rate, int channels, ComPtr<IMFSinkWriter>& writer, DWORD& stream) {
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(&attributes, 1), "MFCreateAttributes");
    check(attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, format == CompressedFormat::Mp3 ? MFTranscodeContainerType_MP3 : MFTranscodeContainerType_MPEG4), "container");
    check(MFCreateSinkWriterFromURL(path.wstring().c_str(), nullptr, attributes.p, &writer), "creating the file writer");

    ComPtr<IMFMediaType> out;
    check(MFCreateMediaType(&out), "MFCreateMediaType");
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out->SetGUID(MF_MT_SUBTYPE, format == CompressedFormat::Mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC);
    out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(rate));
    out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channels));
    out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, format == CompressedFormat::Mp3 ? 32000 : 24000);  // 256 and 192 kbit/s
    out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
    if (format == CompressedFormat::M4aAac) out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);  // raw AAC in the MP4 container
    if (format == CompressedFormat::Mp3) {
        // the MP3 encoder only accepts one of the types it lists (with its own details): take the one for this rate and channels nearest to 256 kbit/s
        ComPtr<IMFCollection> types;
        if (SUCCEEDED(MFTranscodeGetAudioOutputAvailableTypes(MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL, nullptr, &types))) {
            DWORD count = 0;
            types->GetElementCount(&count);
            IMFMediaType* best = nullptr;
            long long bestDiff = -1;
            for (DWORD i = 0; i < count; ++i) {
                IUnknown* unknown = nullptr;
                if (FAILED(types->GetElement(i, &unknown)) || !unknown) continue;
                IMFMediaType* t = nullptr;
                UINT32 r = 0, c = 0, bytes = 0;
                if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&t))) && SUCCEEDED(t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r)) && SUCCEEDED(t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c)) &&
                    SUCCEEDED(t->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes)) && r == static_cast<UINT32>(rate) && c == static_cast<UINT32>(channels)) {
                    const long long diff = std::llabs(static_cast<long long>(bytes) - 32000);
                    if (bestDiff < 0 || diff < bestDiff) {
                        if (best) best->Release();
                        best = t;
                        t = nullptr;
                        bestDiff = diff;
                    }
                }
                if (t) t->Release();
                unknown->Release();
            }
            if (best) {
                const HRESULT added = writer->AddStream(best, &stream);
                best->Release();
                check(added, "adding the MP3 stream");
                goto input;
            }
        }
    }
    check(writer->AddStream(out.p, &stream), "adding the encoded stream");
input:

    ComPtr<IMFMediaType> in;
    check(MFCreateMediaType(&in), "MFCreateMediaType");
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(rate));
    in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channels));
    in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(2 * channels));
    in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(rate * 2 * channels));
    check(writer->SetInputMediaType(stream, in.p, nullptr), "connecting the encoder");
    return true;
}

}  // namespace

bool compressedEncoderAvailable(CompressedFormat format) {
    try {
        MfSession session;
        MFT_REGISTER_TYPE_INFO in{MFMediaType_Audio, MFAudioFormat_PCM};
        MFT_REGISTER_TYPE_INFO out{MFMediaType_Audio, format == CompressedFormat::Mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in, &out, &activates, &count))) return false;
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);
        return count > 0;
    } catch (const std::exception&) {
        return false;
    }
}

void writeCompressed(const std::filesystem::path& path, CompressedFormat format, int sampleRate, int channels, const std::vector<float>& interleaved) {
    if (channels < 1 || channels > 2) throw std::runtime_error("an MP3 or M4A file has one or two channels");
    if (sampleRate != 44100 && sampleRate != 48000) throw std::runtime_error("the encoders take 44.1 and 48 kHz");
    MfSession session;
    ComPtr<IMFSinkWriter> writer;
    DWORD stream = 0;
    if (!openWriter(path, format, sampleRate, channels, writer, stream)) throw std::runtime_error("this Windows has no encoder for " + std::string(format == CompressedFormat::Mp3 ? "MP3" : "AAC"));
    check(writer->BeginWriting(), "BeginWriting");
    const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
    const std::size_t chunk = static_cast<std::size_t>(sampleRate);  // one second per sample
    for (std::size_t at = 0; at < frames; at += chunk) {
        const std::size_t n = std::min(chunk, frames - at);
        const DWORD bytes = static_cast<DWORD>(n * static_cast<std::size_t>(channels) * 2);
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateMemoryBuffer(bytes, &buffer), "MFCreateMemoryBuffer");
        BYTE* data = nullptr;
        check(buffer->Lock(&data, nullptr, nullptr), "Lock");
        auto* pcm = reinterpret_cast<std::int16_t*>(data);
        for (std::size_t i = 0; i < n * static_cast<std::size_t>(channels); ++i) {
            const float v = std::clamp(interleaved[at * static_cast<std::size_t>(channels) + i], -1.0f, 1.0f);
            pcm[i] = static_cast<std::int16_t>(std::lround(v * 32767.0f));
        }
        buffer->Unlock();
        buffer->SetCurrentLength(bytes);
        ComPtr<IMFSample> sample;
        check(MFCreateSample(&sample), "MFCreateSample");
        sample->AddBuffer(buffer.p);
        sample->SetSampleTime(static_cast<LONGLONG>(at) * 10000000LL / sampleRate);
        sample->SetSampleDuration(static_cast<LONGLONG>(n) * 10000000LL / sampleRate);
        check(writer->WriteSample(stream, sample.p), "WriteSample");
    }
    check(writer->Finalize(), "Finalize");
}

#else

bool compressedEncoderAvailable(CompressedFormat) { return false; }

void writeCompressed(const std::filesystem::path&, CompressedFormat, int, int, const std::vector<float>&) {
    throw std::runtime_error("MP3 and M4A need an encoder, which this platform does not have here");
}

#endif

}  // namespace lpc
