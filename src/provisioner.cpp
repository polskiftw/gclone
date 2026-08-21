#include "provisioner.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr size_t kRecentInstallerLineCount = 16;

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

std::wstring CleanInstallerLine(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());

    for (size_t i = 0; i < value.size();) {
        const wchar_t ch = value[i];

        // Strip ANSI/VT CSI color and progress sequences before showing installer output in
        // native UI. The complete unmodified byte stream is still kept in the setup log.
        if (ch == 0x1b && i + 1 < value.size() && value[i + 1] == L'[') {
            i += 2;
            while (i < value.size()) {
                const wchar_t code = value[i++];
                if (code >= 0x40 && code <= 0x7e) break;
            }
            continue;
        }

        ++i;
        if (ch == L'\t' || ch >= L' ') out.push_back(ch);
    }

    const auto first = std::find_if_not(out.begin(), out.end(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    });
    const auto last = std::find_if_not(out.rbegin(), out.rend(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    }).base();
    if (first >= last) return {};
    return std::wstring(first, last);
}

bool Exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

fs::path SetupScript(provision::EngineId engine, const fs::path& executableDirectory) {
    return executableDirectory / L"engines" /
           (engine == provision::EngineId::Qwen ? L"qwen" : L"index") / L"setup.ps1";
}

void RecordInstallerLine(const std::string& bytes,
                         std::deque<std::wstring>& recentLines,
                         const std::function<void(const std::wstring&)>& onLine) {
    const std::wstring line = CleanInstallerLine(BytesToWide(bytes));
    if (line.empty()) return;

    if (recentLines.size() >= kRecentInstallerLineCount) recentLines.pop_front();
    recentLines.push_back(line);
    if (onLine) onLine(line);
}

void ConsumeOutput(const char* data, DWORD size, std::string& pending,
                   std::deque<std::wstring>& recentLines,
                   const std::function<void(const std::wstring&)>& onLine) {
    for (DWORD i = 0; i < size; ++i) {
        const char ch = data[i];
        if (ch == '\r' || ch == '\n') {
            if (!pending.empty()) {
                RecordInstallerLine(pending, recentLines, onLine);
                pending.clear();
            }
        } else {
            pending.push_back(ch);
        }
    }
}

std::wstring RecentInstallerOutput(const std::deque<std::wstring>& lines) {
    std::wstring out;
    for (const auto& line : lines) {
        if (!out.empty()) out += L"\n";
        out += line;
    }
    return out;
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
        const bool ready = Exists(root / L".ready-qwen-1.7b-v3") &&
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
              const std::atomic<bool>* cancel,
              std::wstring& error) {
    const fs::path script = SetupScript(engine, executableDirectory);
    if (!Exists(script)) {
        error = L"The bundled engine installer is missing: " + script.wstring();
        return false;
    }

    const fs::path logDirectory = DataRoot() / L"logs";
    std::error_code logDirectoryError;
    fs::create_directories(logDirectory, logDirectoryError);
    const fs::path logPath = logDirectory /
        (engine == EngineId::Qwen ? L"qwen-setup.log" : L"index-setup.log");
    std::ofstream logFile(logPath, std::ios::binary | std::ios::trunc);

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

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        error = LastErrorMessage(L"Could not create installer process group");
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
        error = LastErrorMessage(L"Could not configure installer process group");
        CloseHandle(job);
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);
        return false;
    }

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
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                        workingDirectory.c_str(), &si, &pi);

    CloseHandle(writePipe);
    if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);

    if (!started) {
        error = LastErrorMessage(L"Could not start the built-in installer");
        CloseHandle(job);
        CloseHandle(readPipe);
        return false;
    }

    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        error = LastErrorMessage(L"Could not attach the installer to its process group");
        TerminateProcess(pi.hProcess, ERROR_CANCELLED);
        WaitForSingleObject(pi.hProcess, 1500);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        CloseHandle(readPipe);
        return false;
    }

    if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        error = LastErrorMessage(L"Could not resume the built-in installer");
        TerminateJobObject(job, ERROR_CANCELLED);
        WaitForSingleObject(pi.hProcess, 1500);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        CloseHandle(readPipe);
        return false;
    }

    std::string pending;
    std::deque<std::wstring> recentLines;
    char buffer[4096];
    bool cancelled = false;

    auto consumeChunk = [&](const char* data, DWORD size) {
        if (logFile.is_open()) {
            logFile.write(data, static_cast<std::streamsize>(size));
            logFile.flush();
        }
        ConsumeOutput(data, size, pending, recentLines, onLine);
    };

    for (;;) {
        if (!cancelled && cancel && cancel->load()) {
            cancelled = true;
            TerminateJobObject(job, ERROR_CANCELLED);
        }

        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available > 0) {
            DWORD read = 0;
            const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(readPipe, buffer, wanted, &read, nullptr) && read > 0) {
                consumeChunk(buffer, read);
            }
            continue;
        }

        if (WaitForSingleObject(pi.hProcess, 60) == WAIT_OBJECT_0) {
            for (;;) {
                available = 0;
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
                DWORD read = 0;
                const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
                if (!ReadFile(readPipe, buffer, wanted, &read, nullptr) || read == 0) break;
                consumeChunk(buffer, read);
            }
            break;
        }
    }

    if (!pending.empty()) {
        RecordInstallerLine(pending, recentLines, onLine);
    }

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // KILL_ON_JOB_CLOSE is intentional: if a child somehow outlived PowerShell, do not leave
    // a hidden uv/python/model download process running after gclone considers setup finished.
    CloseHandle(job);
    if (logFile.is_open()) logFile.close();

    if (cancelled) {
        error = L"Installation cancelled.";
        return false;
    }
    if (exitCode != 0) {
        error = L"Automatic setup failed (exit code " + std::to_wstring(exitCode) + L").";
        const std::wstring recent = RecentInstallerOutput(recentLines);
        if (!recent.empty()) error += L"\n\nRecent installer output:\n" + recent;
        if (!logDirectoryError && Exists(logPath)) error += L"\n\nFull log:\n" + logPath.wstring();
        return false;
    }

    if (GetState(engine) != State::Ready) {
        error = L"Setup completed, but gclone could not verify the installed runtime. Run Generate again to repair it.";
        if (!logDirectoryError && Exists(logPath)) error += L"\n\nFull log:\n" + logPath.wstring();
        return false;
    }
    return true;
}
}  // namespace provision
