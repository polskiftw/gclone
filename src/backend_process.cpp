#include "backend_process.h"

#include <filesystem>
#include <vector>

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

bool LooksTerminal(const std::string& line) {
    return line.find("\"terminal\": true") != std::string::npos ||
           line.find("\"terminal\":true") != std::string::npos;
}
}  // namespace

BackendProcess::~BackendProcess() {
    Stop();
}

bool BackendProcess::Start(const std::wstring& launcherPath, std::wstring& error) {
    std::scoped_lock lock(ioMutex_);
    if (process_) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(process_, &exitCode) && exitCode == STILL_ACTIVE) {
            return true;
        }
        CloseHandles();
    }
    if (!std::filesystem::exists(launcherPath)) {
        error = L"Backend launcher not found: " + launcherPath;
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutWrite = nullptr;
    HANDLE childStdinRead = nullptr;
    if (!CreatePipe(&stdoutRead_, &childStdoutWrite, &sa, 0)) {
        error = LastErrorMessage(L"Could not create backend stdout pipe");
        return false;
    }
    SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&childStdinRead, &stdinWrite_, &sa, 0)) {
        error = LastErrorMessage(L"Could not create backend stdin pipe");
        CloseHandle(childStdoutWrite);
        CloseHandle(stdoutRead_);
        stdoutRead_ = nullptr;
        return false;
    }
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = nul;

    PROCESS_INFORMATION pi{};
    std::wstring command = L"cmd.exe /d /s /c \"\"" + launcherPath + L"\"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    const std::wstring workingDir = std::filesystem::path(launcherPath).parent_path().wstring();

    const BOOL ok = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, workingDir.c_str(), &si, &pi);

    CloseHandle(childStdoutWrite);
    CloseHandle(childStdinRead);
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }

    if (!ok) {
        error = LastErrorMessage(L"Could not start backend");
        CloseHandles();
        return false;
    }

    process_ = pi.hProcess;
    thread_ = pi.hThread;
    return true;
}

bool BackendProcess::Request(const std::string& request,
                             const std::function<void(const std::string&)>& onLine,
                             std::wstring& error) {
    std::scoped_lock lock(ioMutex_);
    if (!process_ || !stdinWrite_ || !stdoutRead_) {
        error = L"Backend is not running.";
        return false;
    }

    std::string wire = request;
    wire.push_back('\n');
    DWORD written = 0;
    if (!WriteFile(stdinWrite_, wire.data(), static_cast<DWORD>(wire.size()), &written, nullptr) ||
        written != wire.size()) {
        error = LastErrorMessage(L"Could not send request to backend");
        return false;
    }
    FlushFileBuffers(stdinWrite_);

    for (;;) {
        std::string line;
        if (!ReadLine(line, error)) {
            return false;
        }
        if (line.empty()) {
            continue;
        }
        onLine(line);
        if (LooksTerminal(line)) {
            return true;
        }
    }
}

bool BackendProcess::ReadLine(std::string& line, std::wstring& error) {
    line.clear();
    for (;;) {
        char c = 0;
        DWORD read = 0;
        if (!ReadFile(stdoutRead_, &c, 1, &read, nullptr) || read == 0) {
            DWORD exitCode = STILL_ACTIVE;
            if (process_) {
                GetExitCodeProcess(process_, &exitCode);
            }
            error = L"Backend stopped unexpectedly (exit code " + std::to_wstring(exitCode) + L").";
            return false;
        }
        if (c == '\n') {
            return true;
        }
        if (c != '\r') {
            line.push_back(c);
        }
    }
}

void BackendProcess::Stop() {
    HANDLE processSnapshot = process_;
    if (processSnapshot) {
        TerminateProcess(processSnapshot, 0);
        WaitForSingleObject(processSnapshot, 1500);
    }
    std::scoped_lock lock(ioMutex_);
    CloseHandles();
}

bool BackendProcess::IsRunning() const {
    if (!process_) {
        return false;
    }
    DWORD exitCode = 0;
    return GetExitCodeProcess(process_, &exitCode) && exitCode == STILL_ACTIVE;
}

void BackendProcess::CloseHandles() {
    if (stdinWrite_) {
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }
    if (stdoutRead_) {
        CloseHandle(stdoutRead_);
        stdoutRead_ = nullptr;
    }
    if (thread_) {
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}
