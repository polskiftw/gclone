#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "backend_process.h"
#include "json_lite.h"
#include "media_utils.h"

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kWindowClass[] = L"gclone.main";
constexpr UINT WM_APP_BACKEND_LINE = WM_APP + 1;
constexpr UINT WM_APP_BACKEND_ERROR = WM_APP + 2;
constexpr UINT WM_APP_SELECT_MODEL = WM_APP + 3;

constexpr int IDC_VOICE = 100;
constexpr int IDC_BROWSE = 101;
constexpr int IDC_TEXT = 102;
constexpr int IDC_MODEL = 103;
constexpr int IDC_LANGUAGE_LABEL = 110;
constexpr int IDC_LANGUAGE = 111;
constexpr int IDC_TRANSCRIPT_LABEL = 112;
constexpr int IDC_TRANSCRIPT = 113;
constexpr int IDC_EMOTION_LABEL = 114;
constexpr int IDC_EMOTION = 115;
constexpr int IDC_EMOTION_STRENGTH_LABEL = 116;
constexpr int IDC_EMOTION_STRENGTH = 117;
constexpr int IDC_DURATION_LABEL = 118;
constexpr int IDC_DURATION = 119;
constexpr int IDC_GENERATE = 120;
constexpr int IDC_PLAY = 121;
constexpr int IDC_SEEK = 122;
constexpr int IDC_TIME = 123;
constexpr int IDC_EXPORT = 124;
constexpr int IDC_ADVANCED = 130;
constexpr int IDC_XVECTOR = 131;
constexpr int IDC_TEMP_LABEL = 132;
constexpr int IDC_TEMP = 133;
constexpr int IDC_TOPP_LABEL = 134;
constexpr int IDC_TOPP = 135;
constexpr int IDC_TOPK_LABEL = 136;
constexpr int IDC_TOPK = 137;
constexpr int IDC_REP_LABEL = 138;
constexpr int IDC_REP = 139;
constexpr int IDC_STATUS = 140;

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring GetText(HWND control) {
    const int len = GetWindowTextLengthW(control);
    std::wstring out(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(control, out.data(), len + 1);
    out.resize(len);
    return out;
}

void SetFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void Show(HWND control, bool visible) {
    ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

std::wstring FormatTime(long ms) {
    if (ms < 0) ms = 0;
    const long totalSeconds = ms / 1000;
    const long minutes = totalSeconds / 60;
    const long seconds = totalSeconds % 60;
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%ld:%02ld", minutes, seconds);
    return buffer;
}

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path().wstring();
}

std::wstring LocalAppDataDirectory() {
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return path;
    }
    return ExecutableDirectory();
}

void CleanupOldSessions(const fs::path& cacheRoot) {
    std::error_code ec;
    if (!fs::exists(cacheRoot, ec)) return;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(48);
    for (const auto& entry : fs::directory_iterator(cacheRoot, ec)) {
        if (ec || !entry.is_directory(ec)) continue;
        if (!entry.path().filename().wstring().starts_with(L"session-")) continue;
        const auto modified = fs::last_write_time(entry.path(), ec);
        if (!ec && modified < cutoff) {
            fs::remove_all(entry.path(), ec);
            ec.clear();
        }
    }
}

struct Capabilities {
    bool language = false;
    bool referenceTranscript = false;
    bool xVectorOnly = false;
    bool emotionText = false;
    bool emotionStrength = false;
    bool durationFactor = false;
    bool temperature = false;
    bool topP = false;
    bool topK = false;
    bool repetitionPenalty = false;
};

class GCloneApp {
public:
    explicit GCloneApp(HINSTANCE instance) : instance_(instance) {}
    ~GCloneApp() { Shutdown(); }

    bool Create(int showCommand) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &GCloneApp::StaticWndProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        window_ = CreateWindowExW(0, kWindowClass, L"gclone", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                      WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 760, 900, nullptr, nullptr, instance_, this);
        if (!window_) return false;
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
        return true;
    }

    int Run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        GCloneApp* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<GCloneApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = hwnd;
        } else {
            self = reinterpret_cast<GCloneApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        return self ? self->WndProc(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_CREATE: OnCreate(); return 0;
            case WM_COMMAND: OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;
            case WM_HSCROLL: OnScroll(reinterpret_cast<HWND>(lParam), LOWORD(wParam)); return 0;
            case WM_TIMER: OnTimer(); return 0;
            case WM_DROPFILES: OnDrop(reinterpret_cast<HDROP>(wParam)); return 0;
            case WM_APP_BACKEND_LINE: {
                std::unique_ptr<std::string> line(reinterpret_cast<std::string*>(lParam));
                OnBackendLine(*line);
                return 0;
            }
            case WM_APP_BACKEND_ERROR: {
                std::unique_ptr<std::wstring> error(reinterpret_cast<std::wstring*>(lParam));
                FinishBusy();
                SetStatus(*error);
                MessageBoxW(window_, error->c_str(), L"gclone backend", MB_OK | MB_ICONERROR);
                return 0;
            }
            case WM_APP_SELECT_MODEL: SelectModel(); return 0;
            case WM_CLOSE:
                DestroyWindow(window_);
                return 0;
            case WM_DESTROY:
                Shutdown();
                PostQuitMessage(0);
                return 0;
            default: return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    HWND MakeStatic(const wchar_t* text, int x, int y, int w, int h, int id = 0) {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                       x, y, w, h, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetFont(control, font_);
        return control;
    }

    HWND MakeEdit(int id, int x, int y, int w, int h, DWORD extraStyle = 0) {
        HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                       ES_AUTOHSCROLL | extraStyle,
                                       x, y, w, h, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetFont(control, font_);
        return control;
    }

    HWND MakeButton(const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = BS_PUSHBUTTON) {
        HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                       x, y, w, h, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetFont(control, font_);
        return control;
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        DragAcceptFiles(window_, TRUE);

        MakeStatic(L"Voice sample", 24, 20, 160, 22);
        voice_ = MakeEdit(IDC_VOICE, 24, 44, 640, 28, ES_READONLY);
        browse_ = MakeButton(L"…", IDC_BROWSE, 672, 43, 48, 30);

        MakeStatic(L"Text", 24, 88, 100, 22);
        text_ = MakeEdit(IDC_TEXT, 24, 112, 696, 190, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN);

        MakeStatic(L"Model", 24, 318, 100, 22);
        model_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 24, 342, 696, 200, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MODEL)), instance_, nullptr);
        SetFont(model_, font_);
        SendMessageW(model_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Qwen3-TTS 12Hz 1.7B Base — Voice Clone"));
        SendMessageW(model_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"IndexTTS 2.5 — Voice Clone"));
        SendMessageW(model_, CB_SETCURSEL, 0, 0);

        languageLabel_ = MakeStatic(L"Language", 24, 382, 110, 22, IDC_LANGUAGE_LABEL);
        language_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                    140, 378, 220, 200, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LANGUAGE)), instance_, nullptr);
        SetFont(language_, font_);

        transcriptLabel_ = MakeStatic(L"Reference transcript", 24, 418, 150, 22, IDC_TRANSCRIPT_LABEL);
        transcript_ = MakeEdit(IDC_TRANSCRIPT, 180, 414, 540, 54, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);

        emotionLabel_ = MakeStatic(L"Emotion description", 24, 382, 150, 22, IDC_EMOTION_LABEL);
        emotion_ = MakeEdit(IDC_EMOTION, 180, 378, 540, 54, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        emotionStrengthLabel_ = MakeStatic(L"Emotion strength", 24, 442, 150, 22, IDC_EMOTION_STRENGTH_LABEL);
        emotionStrength_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ,
                                           180, 438, 300, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EMOTION_STRENGTH)), instance_, nullptr);
        SendMessageW(emotionStrength_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(emotionStrength_, TBM_SETPOS, TRUE, 60);

        durationLabel_ = MakeStatic(L"Speed", 24, 478, 150, 22, IDC_DURATION_LABEL);
        duration_ = MakeEdit(IDC_DURATION, 180, 474, 90, 26);
        SetWindowTextW(duration_, L"1.0");
        durationHint_ = MakeStatic(L"0.5–2.0× duration", 280, 478, 180, 22);

        generate_ = MakeButton(L"Generate", IDC_GENERATE, 292, 522, 170, 38, BS_DEFPUSHBUTTON);

        play_ = MakeButton(L"▶", IDC_PLAY, 24, 584, 50, 32);
        seek_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ,
                                84, 584, 520, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEEK)), instance_, nullptr);
        SendMessageW(seek_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        time_ = MakeStatic(L"0:00 / 0:00", 614, 590, 105, 22, IDC_TIME);
        export_ = MakeButton(L"Export…", IDC_EXPORT, 24, 634, 120, 32);

        advanced_ = MakeButton(L"Advanced", IDC_ADVANCED, 24, 682, 130, 26, BS_AUTOCHECKBOX);
        xVector_ = MakeButton(L"Speaker embedding only (lower fidelity; transcript optional)", IDC_XVECTOR,
                              42, 718, 430, 24, BS_AUTOCHECKBOX);
        tempLabel_ = MakeStatic(L"Temperature", 42, 754, 90, 22, IDC_TEMP_LABEL);
        temp_ = MakeEdit(IDC_TEMP, 136, 750, 70, 26);
        topPLabel_ = MakeStatic(L"Top-p", 230, 754, 52, 22, IDC_TOPP_LABEL);
        topP_ = MakeEdit(IDC_TOPP, 286, 750, 70, 26);
        topKLabel_ = MakeStatic(L"Top-k", 380, 754, 52, 22, IDC_TOPK_LABEL);
        topK_ = MakeEdit(IDC_TOPK, 436, 750, 70, 26);
        repLabel_ = MakeStatic(L"Repetition", 528, 754, 78, 22, IDC_REP_LABEL);
        rep_ = MakeEdit(IDC_REP, 612, 750, 70, 26);

        status_ = MakeStatic(L"Status: Starting…", 24, 816, 696, 24, IDC_STATUS);

        EnableWindow(generate_, FALSE);
        EnableWindow(play_, FALSE);
        EnableWindow(export_, FALSE);
        HideDynamicControls();
        UpdateAdvancedVisibility();

        const fs::path cacheRoot = fs::path(LocalAppDataDirectory()) / L"gclone" / L"cache";
        CleanupOldSessions(cacheRoot);
        sessionDir_ = cacheRoot / (L"session-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        fs::create_directories(sessionDir_, ec);

        SetTimer(window_, 1, 200, nullptr);
        PostMessageW(window_, WM_APP_SELECT_MODEL, 0, 0);
    }

    void OnCommand(int id, int notification) {
        if (id == IDC_BROWSE && notification == BN_CLICKED) BrowseVoice();
        else if (id == IDC_MODEL && notification == CBN_SELCHANGE) SelectModel();
        else if (id == IDC_GENERATE && notification == BN_CLICKED) Generate();
        else if (id == IDC_PLAY && notification == BN_CLICKED) TogglePlayback();
        else if (id == IDC_EXPORT && notification == BN_CLICKED) Export();
        else if (id == IDC_ADVANCED && notification == BN_CLICKED) UpdateAdvancedVisibility();
        else if (id == IDC_XVECTOR && notification == BN_CLICKED) UpdateTranscriptRequirement();
    }

    void OnScroll(HWND control, int code) {
        if (control == seek_ && (code == TB_ENDTRACK || code == TB_THUMBPOSITION)) {
            if (!playbackOpen_) return;
            const long pos = static_cast<long>(SendMessageW(seek_, TBM_GETPOS, 0, 0));
            std::wstring error;
            if (media::SeekMs(pos, error)) {
                playing_ = true;
                SetWindowTextW(play_, L"❚❚");
            }
        }
    }

    void OnTimer() {
        if (!playbackOpen_) return;
        const long length = media::LengthMs();
        const long position = media::PositionMs();
        if (length > 0) {
            SendMessageW(seek_, TBM_SETRANGE, TRUE, MAKELPARAM(0, length));
            SendMessageW(seek_, TBM_SETPOS, TRUE, position);
            const std::wstring display = FormatTime(position) + L" / " + FormatTime(length);
            SetWindowTextW(time_, display.c_str());
            if (position >= length - 100 && playing_) {
                media::Stop();
                playing_ = false;
                playbackOpen_ = false;
                SetWindowTextW(play_, L"▶");
                SendMessageW(seek_, TBM_SETPOS, TRUE, 0);
            }
        }
    }

    void OnDrop(HDROP drop) {
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) {
            SetWindowTextW(voice_, path);
        }
        DragFinish(drop);
    }

    void BrowseVoice() {
        wchar_t path[32768]{};
        OPENFILENAMEW ofn{};
        const wchar_t filter[] = L"Audio files\0*.wav;*.mp3;*.m4a;*.aac;*.flac;*.ogg\0All files\0*.*\0\0";
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = window_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = static_cast<DWORD>(std::size(path));
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) SetWindowTextW(voice_, path);
    }

    void SelectModel() {
        if (busy_) return;
        StopPlayback();
        backend_.Stop();
        JoinTask();
        HideDynamicControls();
        UpdateAdvancedVisibility();
        EnableWindow(generate_, FALSE);
        EnableWindow(model_, FALSE);
        busy_ = true;
        SetStatus(L"Checking selected backend…");

        selectedModel_ = static_cast<int>(SendMessageW(model_, CB_GETCURSEL, 0, 0));
        const fs::path launcher = fs::path(ExecutableDirectory()) / L"engines" /
                                  (selectedModel_ == 0 ? L"qwen" : L"index") / L"launch.cmd";

        task_ = std::thread([this, launcher] {
            std::wstring error;
            if (!backend_.Start(launcher.wstring(), error)) {
                PostError(error);
                return;
            }
            if (!backend_.Request("{\"command\":\"capabilities\"}", [this](const std::string& line) { PostLine(line); }, error)) {
                PostError(error);
            }
        });
    }

    void Generate() {
        if (busy_) return;
        const std::wstring voice = GetText(voice_);
        const std::wstring text = GetText(text_);
        if (voice.empty() || !fs::exists(voice)) {
            MessageBoxW(window_, L"Choose a voice sample first.", L"gclone", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (text.empty()) {
            MessageBoxW(window_, L"Paste or type some text to speak.", L"gclone", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const bool xVectorOnly = SendMessageW(xVector_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const std::wstring transcript = GetText(transcript_);
        if (capabilities_.referenceTranscript && !xVectorOnly && transcript.empty()) {
            MessageBoxW(window_,
                        L"Qwen's high-fidelity clone mode requires the transcript of the reference sample.\n\n"
                        L"If you do not have it, open Advanced and enable speaker-embedding-only mode.",
                        L"Reference transcript needed", MB_OK | MB_ICONINFORMATION);
            return;
        }

        StopPlayback();
        generationCounter_++;
        const fs::path output = sessionDir_ / (L"generation-" + std::to_wstring(generationCounter_) + L".wav");
        currentOutput_.clear();
        EnableWindow(play_, FALSE);
        EnableWindow(export_, FALSE);

        std::string request = "{\"command\":\"generate\"";
        request += ",\"reference_audio\":\"" + JsonEscape(WideToUtf8(voice)) + "\"";
        request += ",\"text\":\"" + JsonEscape(WideToUtf8(text)) + "\"";
        request += ",\"output_path\":\"" + JsonEscape(WideToUtf8(output.wstring())) + "\"";

        if (capabilities_.language) {
            wchar_t language[64]{};
            GetWindowTextW(language_, language, static_cast<int>(std::size(language)));
            request += ",\"language\":\"" + JsonEscape(WideToUtf8(language)) + "\"";
        }
        if (capabilities_.referenceTranscript) {
            request += ",\"reference_transcript\":\"" + JsonEscape(WideToUtf8(transcript)) + "\"";
            request += std::string(",\"x_vector_only\":") + (xVectorOnly ? "true" : "false");
        }
        if (capabilities_.emotionText) {
            const std::wstring emotion = GetText(emotion_);
            request += ",\"emotion_text\":\"" + JsonEscape(WideToUtf8(emotion)) + "\"";
        }
        if (capabilities_.emotionStrength) {
            const int strength = static_cast<int>(SendMessageW(emotionStrength_, TBM_GETPOS, 0, 0));
            request += ",\"emotion_strength\":" + std::to_string(strength / 100.0);
        }
        AppendOptionalNumber(request, "duration_factor", duration_, capabilities_.durationFactor);
        AppendOptionalNumber(request, "temperature", temp_, capabilities_.temperature);
        AppendOptionalNumber(request, "top_p", topP_, capabilities_.topP);
        AppendOptionalNumber(request, "top_k", topK_, capabilities_.topK);
        AppendOptionalNumber(request, "repetition_penalty", rep_, capabilities_.repetitionPenalty);
        request += "}";

        busy_ = true;
        EnableWindow(generate_, FALSE);
        EnableWindow(model_, FALSE);
        SetStatus(L"Preparing sample…");
        JoinTask();
        task_ = std::thread([this, request] {
            std::wstring error;
            if (!backend_.Request(request, [this](const std::string& line) { PostLine(line); }, error)) {
                PostError(error);
            }
        });
    }

    void AppendOptionalNumber(std::string& request, const char* key, HWND edit, bool supported) {
        if (!supported) return;
        const std::wstring value = GetText(edit);
        if (value.empty()) return;
        wchar_t* end = nullptr;
        const double number = wcstod(value.c_str(), &end);
        if (end == value.c_str() || *end != L'\0') return;
        request += ",\"" + std::string(key) + "\":" + std::to_string(number);
    }

    void TogglePlayback() {
        if (currentOutput_.empty()) return;
        if (!playbackOpen_) {
            std::wstring error;
            if (!media::PlayWav(currentOutput_, error)) {
                MessageBoxW(window_, error.c_str(), L"Playback error", MB_OK | MB_ICONERROR);
                return;
            }
            playbackOpen_ = true;
            playing_ = true;
            SetWindowTextW(play_, L"❚❚");
        } else if (playing_) {
            media::Pause();
            playing_ = false;
            SetWindowTextW(play_, L"▶");
        } else {
            media::Resume();
            playing_ = true;
            SetWindowTextW(play_, L"❚❚");
        }
    }

    void StopPlayback() {
        media::Stop();
        playbackOpen_ = false;
        playing_ = false;
        if (play_) SetWindowTextW(play_, L"▶");
        if (seek_) SendMessageW(seek_, TBM_SETPOS, TRUE, 0);
        if (time_) SetWindowTextW(time_, L"0:00 / 0:00");
    }

    void Export() {
        if (currentOutput_.empty()) return;
        wchar_t path[32768]{};
        OPENFILENAMEW ofn{};
        const wchar_t filter[] = L"WAV audio (*.wav)\0*.wav\0MP3 audio (*.mp3)\0*.mp3\0\0";
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = window_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = static_cast<DWORD>(std::size(path));
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = L"wav";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return;

        fs::path destination(path);
        const bool mp3 = ofn.nFilterIndex == 2;
        if (mp3 && destination.extension() != L".mp3") destination.replace_extension(L".mp3");
        if (!mp3 && destination.extension() != L".wav") destination.replace_extension(L".wav");

        SetStatus(mp3 ? L"Exporting MP3…" : L"Exporting WAV…");
        std::wstring error;
        const bool ok = mp3 ? media::ExportMp3(currentOutput_, destination.wstring(), 192, error)
                            : media::ExportWav(currentOutput_, destination.wstring(), error);
        if (!ok) {
            SetStatus(L"Export failed.");
            MessageBoxW(window_, error.c_str(), L"Export error", MB_OK | MB_ICONERROR);
            return;
        }
        SetStatus(L"Export complete.");
    }

    void OnBackendLine(const std::string& line) {
        const std::string event = JsonGetString(line, "event").value_or("");
        if (event == "capabilities") {
            capabilities_.language = JsonGetBool(line, "language");
            capabilities_.referenceTranscript = JsonGetBool(line, "reference_transcript");
            capabilities_.xVectorOnly = JsonGetBool(line, "x_vector_only");
            capabilities_.emotionText = JsonGetBool(line, "emotion_text");
            capabilities_.emotionStrength = JsonGetBool(line, "emotion_strength");
            capabilities_.durationFactor = JsonGetBool(line, "duration_factor");
            capabilities_.temperature = JsonGetBool(line, "temperature");
            capabilities_.topP = JsonGetBool(line, "top_p");
            capabilities_.topK = JsonGetBool(line, "top_k");
            capabilities_.repetitionPenalty = JsonGetBool(line, "repetition_penalty");
            if (const auto options = JsonGetString(line, "language_options")) {
                SetLanguageOptions(Utf8ToWide(*options));
            }
            ApplyCapabilities();
            FinishBusy();
            SetStatus(L"Ready.");
        } else if (event == "status") {
            const auto message = JsonGetString(line, "message");
            if (message) SetStatus(Utf8ToWide(*message));
        } else if (event == "result") {
            const auto output = JsonGetString(line, "output_path");
            if (output) currentOutput_ = Utf8ToWide(*output);
            FinishBusy();
            EnableWindow(play_, !currentOutput_.empty());
            EnableWindow(export_, !currentOutput_.empty());
            SetStatus(L"Complete.");
        } else if (event == "error") {
            const std::wstring message = Utf8ToWide(JsonGetString(line, "message").value_or("Backend error."));
            FinishBusy();
            SetStatus(message);
            MessageBoxW(window_, message.c_str(), L"Generation failed", MB_OK | MB_ICONERROR);
        }
    }

    void SetLanguageOptions(const std::wstring& options) {
        SendMessageW(language_, CB_RESETCONTENT, 0, 0);
        size_t start = 0;
        while (start <= options.size()) {
            const size_t end = options.find(L'|', start);
            const std::wstring item = options.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!item.empty()) SendMessageW(language_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        if (SendMessageW(language_, CB_GETCOUNT, 0, 0) > 0) SendMessageW(language_, CB_SETCURSEL, 0, 0);
    }

    void ApplyCapabilities() {
        Show(languageLabel_, capabilities_.language);
        Show(language_, capabilities_.language);
        Show(transcriptLabel_, capabilities_.referenceTranscript);
        Show(transcript_, capabilities_.referenceTranscript);
        Show(emotionLabel_, capabilities_.emotionText);
        Show(emotion_, capabilities_.emotionText);
        Show(emotionStrengthLabel_, capabilities_.emotionStrength);
        Show(emotionStrength_, capabilities_.emotionStrength);
        Show(durationLabel_, capabilities_.durationFactor);
        Show(duration_, capabilities_.durationFactor);
        Show(durationHint_, capabilities_.durationFactor);
        UpdateTranscriptRequirement();
        UpdateAdvancedVisibility();
    }

    void UpdateTranscriptRequirement() {
        if (!capabilities_.referenceTranscript) return;
        const bool xVectorOnly = SendMessageW(xVector_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SetWindowTextW(transcriptLabel_, xVectorOnly ? L"Reference transcript (optional)" : L"Reference transcript");
    }

    void HideDynamicControls() {
        Show(languageLabel_, false); Show(language_, false);
        Show(transcriptLabel_, false); Show(transcript_, false);
        Show(emotionLabel_, false); Show(emotion_, false);
        Show(emotionStrengthLabel_, false); Show(emotionStrength_, false);
        Show(durationLabel_, false); Show(duration_, false); Show(durationHint_, false);
    }

    void UpdateAdvancedVisibility() {
        const bool open = advanced_ && SendMessageW(advanced_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        Show(xVector_, open && capabilities_.xVectorOnly);
        Show(tempLabel_, open && capabilities_.temperature); Show(temp_, open && capabilities_.temperature);
        Show(topPLabel_, open && capabilities_.topP); Show(topP_, open && capabilities_.topP);
        Show(topKLabel_, open && capabilities_.topK); Show(topK_, open && capabilities_.topK);
        Show(repLabel_, open && capabilities_.repetitionPenalty); Show(rep_, open && capabilities_.repetitionPenalty);
    }

    void FinishBusy() {
        busy_ = false;
        EnableWindow(model_, TRUE);
        EnableWindow(generate_, TRUE);
    }

    void SetStatus(const std::wstring& status) {
        if (!status_) return;
        const std::wstring display = L"Status: " + status;
        SetWindowTextW(status_, display.c_str());
    }

    void PostLine(const std::string& line) {
        if (window_) PostMessageW(window_, WM_APP_BACKEND_LINE, 0, reinterpret_cast<LPARAM>(new std::string(line)));
    }

    void PostError(const std::wstring& error) {
        if (window_) PostMessageW(window_, WM_APP_BACKEND_ERROR, 0, reinterpret_cast<LPARAM>(new std::wstring(error)));
    }

    void JoinTask() {
        if (task_.joinable()) task_.join();
    }

    void Shutdown() {
        if (shuttingDown_.exchange(true)) return;
        KillTimer(window_, 1);
        StopPlayback();
        backend_.Stop();
        JoinTask();
        if (!sessionDir_.empty()) {
            std::error_code ec;
            fs::remove_all(sessionDir_, ec);
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;

    HWND voice_ = nullptr, browse_ = nullptr, text_ = nullptr, model_ = nullptr;
    HWND languageLabel_ = nullptr, language_ = nullptr, transcriptLabel_ = nullptr, transcript_ = nullptr;
    HWND emotionLabel_ = nullptr, emotion_ = nullptr, emotionStrengthLabel_ = nullptr, emotionStrength_ = nullptr;
    HWND durationLabel_ = nullptr, duration_ = nullptr, durationHint_ = nullptr;
    HWND generate_ = nullptr, play_ = nullptr, seek_ = nullptr, time_ = nullptr, export_ = nullptr;
    HWND advanced_ = nullptr, xVector_ = nullptr, tempLabel_ = nullptr, temp_ = nullptr;
    HWND topPLabel_ = nullptr, topP_ = nullptr, topKLabel_ = nullptr, topK_ = nullptr;
    HWND repLabel_ = nullptr, rep_ = nullptr, status_ = nullptr;

    BackendProcess backend_;
    std::thread task_;
    std::atomic<bool> shuttingDown_ = false;
    bool busy_ = false;
    bool playbackOpen_ = false;
    bool playing_ = false;
    int selectedModel_ = 0;
    unsigned generationCounter_ = 0;
    Capabilities capabilities_;
    fs::path sessionDir_;
    std::wstring currentOutput_;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GCloneApp app(instance);
    const int result = app.Create(showCommand) ? app.Run() : 1;
    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
}
