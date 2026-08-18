#include "provisioner.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::wstring LastErrorMessage(const wchar_t* prefix) {
    const DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring out = prefix;
    out += L" (" + std::to_wstring(code) + L")";
    if (buffer) {
        out += L": ";
        out += buffer;
        LocalFree(buffer);
    }
    return out;
}

std::wstring BytesToWide(const std::string& value) {
    if (value.empty()) return {};

    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    }
    if (count <= 0) return L"(unreadable installer output)";

    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

bool Exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

fs::path SetupScript(provision::EngineId engine, const fs::path& executableDirectory) {
    return executableDirectory / L"engines" /
           (engine == provision::EngineId::Qwen ? L"qwen" : L"index") / L"setup.ps1";
}
}  // namespace

namespace provision {
const wchar_t* DisplayName(EngineId engine) {
    return engine == EngineId::Qwen ? L"Qwen3-TTS 12Hz 1.7B Base" : L"IndexTTS 2.5";
}

fs::path DataRoot() {
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return fs::path(path) / L"gclone";
    }

    wchar_t temp[MAX_PATH]{};
    const DWORD count = GetTempPathW(MAX_PATH, temp);
    if (count > 0 && count < MAX_PATH) {
        return fs::path(temp) / L"gclone";
    }
    return fs::current_path() / L"gclone-data";
}

fs::path EngineRoot(EngineId engine) {
    return DataRoot() / L"runtimes" / (engine == EngineId::Qwen ? L"qwen" : L"index");
}

State GetState(EngineId engine) {
    const fs::path root = EngineRoot(engine);
    if (engine == EngineId::Qwen) {
        const bool ready = Exists(root / L".ready-qwen-1.7b-v1") &&
                           Exists(root / L".venv" / L"Scripts" / L"python.exe");
        if (ready) return State::Ready;
    } else {
        const bool ready = Exists(root / L".ready-index-2.5-v1") &&
                           Exists(root / L".venv" / L"Scripts" / L"python.exe") &&
                           Exists(root / L"source" / L"indextts" / L"infer_v2_5.py") &&
                           Exists(root / L"checkpoints" / L"config.yaml");
        if (ready) return State::Ready;
    }

    return Exists(root) ? State::NeedsRepair : State::Missing;
}

bool RunSetup(EngineId engine,
              const fs::path& executableDirectory,
              const std::function<void(const std::wstring&)>& onLine,
              std::wstring& error) {
    const fs::path script = SetupScript(engine, executableDirectory);
    if (!Exists(script)) {
        error = L"The bundled engine installer is missing: " + script.wstring();
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        error = LastErrorMessage(L"Could not create installer output pipe");
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nulInput == INVALID_HANDLE_VALUE ? GetStdHandle(STD_INPUT_HANDLE) : nulInput;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::wstring command = L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
                           script.wstring() + L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    const std::wstring workingDirectory = script.parent_path().wstring();

    const BOOL started = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &si, &pi);

    CloseHandle(writePipe);
    if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);

    if (!started) {
        error = LastErrorMessage(L"Could not start the built-in installer");
        CloseHandle(readPipe);
        return false;
    }

    std::string pending;
    std::wstring lastLine;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
        if (!ok || read == 0) break;

        for (DWORD i = 0; i < read; ++i) {
            const char ch = buffer[i];
            if (ch == '\r' || ch == '\n') {
                if (!pending.empty()) {
                    lastLine = BytesToWide(pending);
                    if (onLine) onLine(lastLine);
                    pending.clear();
                }
            } else {
                pending.push_back(ch);
            }
        }
    }
    if (!pending.empty()) {
        lastLine = BytesToWide(pending);
        if (onLine) onLine(lastLine);
    }

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        error = L"Automatic setup failed (exit code " + std::to_wstring(exitCode) + L").";
        if (!lastLine.empty()) error += L"\n\nLast installer message:\n" + lastLine;
        return false;
    }

    if (GetState(engine) != State::Ready) {
        error = L"Setup completed, but gclone could not verify the installed runtime. Run Generate again to repair it.";
        return false;
    }
    return true;
}
}  // namespace provision
