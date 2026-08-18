#pragma once

#include <string>

namespace media {
bool PlayWav(const std::wstring& path, std::wstring& error);
void Pause();
void Resume();
void Stop();
long PositionMs();
long LengthMs();
bool SeekMs(long positionMs, std::wstring& error);

bool ExportWav(const std::wstring& source, const std::wstring& destination, std::wstring& error);
bool ExportMp3(const std::wstring& sourceWav, const std::wstring& destinationMp3,
               unsigned bitrateKbps, std::wstring& error);
}  // namespace media
