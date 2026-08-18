#include "media_utils.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {
std::wstring HrMessage(const wchar_t* prefix, HRESULT hr) {
    wchar_t hex[32]{};
    swprintf_s(hex, L"0x%08X", static_cast<unsigned>(hr));
    return std::wstring(prefix) + L" (" + hex + L")";
}

bool Mci(const std::wstring& command, std::wstring& error) {
    const MCIERROR code = mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    if (!code) {
        return true;
    }
    wchar_t buffer[256]{};
    mciGetErrorStringW(code, buffer, static_cast<UINT>(std::size(buffer)));
    error = buffer;
    return false;
}

long MciNumber(const wchar_t* command) {
    wchar_t buffer[64]{};
    if (mciSendStringW(command, buffer, static_cast<UINT>(std::size(buffer)), nullptr) != 0) {
        return 0;
    }
    return _wtol(buffer);
}
}  // namespace

namespace media {
bool PlayWav(const std::wstring& path, std::wstring& error) {
    Stop();
    std::wstring command = L"open \"" + path + L"\" type waveaudio alias gclone_output";
    if (!Mci(command, error)) {
        return false;
    }
    if (!Mci(L"set gclone_output time format milliseconds", error)) {
        Stop();
        return false;
    }
    return Mci(L"play gclone_output", error);
}

void Pause() {
    std::wstring ignored;
    Mci(L"pause gclone_output", ignored);
}

void Resume() {
    std::wstring ignored;
    Mci(L"resume gclone_output", ignored);
}

void Stop() {
    mciSendStringW(L"stop gclone_output", nullptr, 0, nullptr);
    mciSendStringW(L"close gclone_output", nullptr, 0, nullptr);
}

long PositionMs() {
    return MciNumber(L"status gclone_output position");
}

long LengthMs() {
    return MciNumber(L"status gclone_output length");
}

bool SeekMs(long positionMs, std::wstring& error) {
    positionMs = std::max(0L, positionMs);
    return Mci(L"seek gclone_output to " + std::to_wstring(positionMs), error) &&
           Mci(L"play gclone_output", error);
}

bool ExportWav(const std::wstring& source, const std::wstring& destination, std::wstring& error) {
    if (CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
        return true;
    }
    error = L"Windows could not copy the generated WAV (error " + std::to_wstring(GetLastError()) + L").";
    return false;
}

bool ExportMp3(const std::wstring& sourceWav, const std::wstring& destinationMp3,
               unsigned bitrateKbps, std::wstring& error) {
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        error = HrMessage(L"Media Foundation could not start", hr);
        return false;
    }

    bool success = false;
    do {
        ComPtr<IMFSourceReader> reader;
        hr = MFCreateSourceReaderFromURL(sourceWav.c_str(), nullptr, &reader);
        if (FAILED(hr)) { error = HrMessage(L"Could not open generated WAV", hr); break; }

        ComPtr<IMFMediaType> requestedPcm;
        hr = MFCreateMediaType(&requestedPcm);
        if (FAILED(hr)) { error = HrMessage(L"Could not create PCM media type", hr); break; }
        requestedPcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        requestedPcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, requestedPcm.Get());
        if (FAILED(hr)) { error = HrMessage(L"Could not decode WAV as PCM", hr); break; }

        ComPtr<IMFMediaType> inputType;
        hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &inputType);
        if (FAILED(hr)) { error = HrMessage(L"Could not inspect decoded audio", hr); break; }

        UINT32 channels = 0;
        UINT32 sampleRate = 0;
        hr = inputType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        if (FAILED(hr)) { error = HrMessage(L"Could not read channel count", hr); break; }
        hr = inputType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        if (FAILED(hr)) { error = HrMessage(L"Could not read sample rate", hr); break; }

        ComPtr<IMFSinkWriter> writer;
        hr = MFCreateSinkWriterFromURL(destinationMp3.c_str(), nullptr, nullptr, &writer);
        if (FAILED(hr)) { error = HrMessage(L"Could not create MP3 output", hr); break; }

        ComPtr<IMFMediaType> outputType;
        hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) { error = HrMessage(L"Could not create MP3 media type", hr); break; }
        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
        outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
        outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrateKbps * 1000 / 8);
        outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

        DWORD streamIndex = 0;
        hr = writer->AddStream(outputType.Get(), &streamIndex);
        if (FAILED(hr)) { error = HrMessage(L"Windows MP3 encoder rejected the output format", hr); break; }
        hr = writer->SetInputMediaType(streamIndex, inputType.Get(), nullptr);
        if (FAILED(hr)) { error = HrMessage(L"Could not connect PCM decoder to MP3 encoder", hr); break; }
        hr = writer->BeginWriting();
        if (FAILED(hr)) { error = HrMessage(L"Could not begin MP3 export", hr); break; }

        for (;;) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr)) { error = HrMessage(L"Could not decode generated WAV", hr); break; }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                hr = S_OK;
                break;
            }
            if (sample) {
                hr = writer->WriteSample(streamIndex, sample.Get());
                if (FAILED(hr)) { error = HrMessage(L"Could not encode MP3 sample", hr); break; }
            }
        }
        if (FAILED(hr)) break;

        hr = writer->Finalize();
        if (FAILED(hr)) { error = HrMessage(L"Could not finalize MP3 file", hr); break; }
        success = true;
    } while (false);

    MFShutdown();
    return success;
}
}  // namespace media
