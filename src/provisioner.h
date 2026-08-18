#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace provision {
enum class EngineId {
    Qwen,
    Index,
};

enum class State {
    Ready,
    Missing,
    NeedsRepair,
};

const wchar_t* DisplayName(EngineId engine);
std::filesystem::path DataRoot();
std::filesystem::path EngineRoot(EngineId engine);
State GetState(EngineId engine);

bool RunSetup(EngineId engine,
              const std::filesystem::path& executableDirectory,
              const std::function<void(const std::wstring&)>& onLine,
              const std::atomic<bool>* cancel,
              std::wstring& error);
}  // namespace provision
