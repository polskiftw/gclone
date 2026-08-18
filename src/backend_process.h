#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class BackendProcess {
public:
    BackendProcess() = default;
    ~BackendProcess();

    BackendProcess(const BackendProcess&) = delete;
    BackendProcess& operator=(const BackendProcess&) = delete;

    bool Start(const std::wstring& launcherPath, std::wstring& error);
    bool Request(const std::string& request,
                 const std::function<void(const std::string&)>& onLine,
                 std::wstring& error);
    void Stop();
    bool IsRunning() const;

private:
    bool ReadLine(std::string& line, std::wstring& error);
    void CloseHandles();

    std::atomic<HANDLE> process_{nullptr};
    HANDLE thread_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    std::mutex ioMutex_;
};
