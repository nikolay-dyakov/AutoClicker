#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include "resource.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace {

constexpr wchar_t WindowClassName[] = L"AutoClickerNativeWindow";
constexpr wchar_t RegistryPath[] = L"Software\\AutoClicker";
constexpr UINT_PTR SettingsSaveTimerId = 1;
constexpr int StartHotkeyId = 0xAC01;
constexpr int StopHotkeyId = 0xAC02;
constexpr int ToggleHotkeyId = 0xAC03;
constexpr UINT FinishPositionCaptureMessage = WM_APP + 1;
constexpr UINT CancelPositionCaptureMessage = WM_APP + 2;
constexpr UINT PerformClickMessage = WM_APP + 3;
constexpr DWORD HighResolutionTimerFlag = 0x00000002;
constexpr int CpsSliderSteps = 500;

enum ControlId : int {
    IntervalModeId = 100,
    CpsModeId,
    HoursEditId,
    MinutesEditId,
    SecondsEditId,
    MillisecondsEditId,
    CpsEditId,
    InfiniteRepeatId,
    FiniteRepeatId,
    RepeatCountEditId,
    CurrentPositionId,
    FixedPositionId,
    XEditId,
    YEditId,
    CapturePositionId,
    MouseLeftId,
    MouseRightId,
    StartButtonId,
    StopButtonId,
    ToggleButtonId,
    AlwaysOnTopId,
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    int dpi = 96;

    HFONT normalFont = nullptr;
    HFONT boldFont = nullptr;
    HFONT smallFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HWND statusBar = nullptr;

    HWND intervalModeButton = nullptr;
    HWND cpsModeButton = nullptr;
    std::array<HWND, 4> timingUnitLabels{};
    HWND cpsUnitLabel = nullptr;
    HWND cpsHintLabel = nullptr;
    HWND cpsSlider = nullptr;
    HWND timingSummaryLabel = nullptr;
    HWND hoursEdit = nullptr;
    HWND minutesEdit = nullptr;
    HWND secondsEdit = nullptr;
    HWND millisecondsEdit = nullptr;
    HWND cpsEdit = nullptr;
    HWND infiniteRepeatRadio = nullptr;
    HWND finiteRepeatRadio = nullptr;
    HWND repeatCountEdit = nullptr;

    HWND currentPositionRadio = nullptr;
    HWND fixedPositionRadio = nullptr;
    HWND xEdit = nullptr;
    HWND yEdit = nullptr;
    HWND captureButton = nullptr;

    std::array<HWND, 2> mouseButtons{};

    HWND startButton = nullptr;
    HWND stopButton = nullptr;
    HWND toggleButton = nullptr;
    HWND alwaysOnTopCheck = nullptr;
    HHOOK captureMouseHook = nullptr;
    HANDLE clickTimer = nullptr;
    HANDLE clickStopEvent = nullptr;
    HANDLE clickTimerThread = nullptr;

    bool updatingTiming = false;
    bool loadingSettings = false;
    bool cpsMode = false;
    bool infiniteRepeat = true;
    bool fixedPosition = false;
    bool clicking = false;
    bool timerResolutionRaised = false;
    bool capturingPosition = false;
    bool captureButtonDown = false;
    int mouseButton = 0;
    int completedRepeats = 0;
    std::uint64_t successfulClicks = 0;
    ULONGLONG clickRunStart = 0;
    ULONGLONG lastStatsUpdate = 0;
    std::wstring status = L"Ready";
};

AppState app;

int Scale(int value) {
    return MulDiv(value, app.dpi, 96);
}

void SetControlFont(HWND control, HFONT font = nullptr) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : app.normalFont), TRUE);
}

HWND CreateButton(int id, const wchar_t *text, int x, int y, int width, int height, DWORD extraStyle = 0) {
    HWND control = CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extraStyle,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
    SetControlFont(control);
    return control;
}

HWND CreateToggleButton(int id, const wchar_t *text, int x, int y, int width, int height, bool startsGroup) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE;
    if (startsGroup) {
        style |= WS_GROUP;
    }
    HWND control = CreateWindowExW(
        0, L"BUTTON", text, style,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
    SetControlFont(control);
    return control;
}

HWND CreateGroupBox(const wchar_t *text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        nullptr, app.instance, nullptr);
    SetControlFont(control);
    return control;
}

HWND CreateLabel(const wchar_t *text, int x, int y, int width, int height,
                 DWORD style = SS_LEFT, HFONT font = nullptr) {
    HWND control = CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | style,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        nullptr, app.instance, nullptr);
    SetControlFont(control, font);
    return control;
}

LRESULT CALLBACK NumericEditSubclass(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR referenceData) {
    const int id = static_cast<int>(referenceData);
    if (message == WM_SETFOCUS) {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        SendMessageW(control, EM_SETSEL, 0, -1);
        return result;
    }

    int direction = 0;
    if (message == WM_KEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
        direction = wParam == VK_UP ? 1 : -1;
    } else if (message == WM_MOUSEWHEEL) {
        direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
    }

    if (direction != 0) {
        wchar_t text[64]{};
        GetWindowTextW(control, text, static_cast<int>(std::size(text)));
        if (id == CpsEditId) {
            wchar_t *end = nullptr;
            double value = std::wcstod(text, &end);
            if (end == text) {
                value = 10.0;
            }
            value = std::clamp(value + direction, 0.01, 1000.0);
            wchar_t formatted[32]{};
            std::swprintf(formatted, std::size(formatted), L"%.2f", value);
            SetWindowTextW(control, formatted);
        } else {
            wchar_t *end = nullptr;
            long long value = std::wcstoll(text, &end, 10);
            if (end == text) {
                value = 0;
            }
            long long minimum = 0;
            long long maximum = 999999999;
            if (id == MinutesEditId || id == SecondsEditId) {
                maximum = 59;
            } else if (id == MillisecondsEditId) {
                maximum = 999;
            } else if (id == HoursEditId) {
                maximum = 596;
            } else if (id == XEditId || id == YEditId) {
                minimum = -100000;
                maximum = 100000;
            } else if (id == RepeatCountEditId) {
                minimum = 1;
            }
            value = std::clamp(value + direction, minimum, maximum);
            SetWindowTextW(control, std::to_wstring(value).c_str());
        }
        return 0;
    }
    return DefSubclassProc(control, message, wParam, lParam);
}

HWND CreateNumberEdit(int id, int x, int y, int width, int height, bool allowNegative = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL;
    if (!allowNegative && id != CpsEditId) {
        style |= ES_NUMBER;
    }
    HWND control = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"0", style,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
    SetControlFont(control, app.boldFont);
    SetWindowSubclass(control, NumericEditSubclass, 1, static_cast<DWORD_PTR>(id));
    return control;
}

HWND CreateRadio(int id, const wchar_t *text, int x, int y, int width, int height, bool startsGroup) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON;
    if (startsGroup) {
        style |= WS_GROUP;
    }
    HWND control = CreateWindowExW(
        0, L"BUTTON", text, style,
        Scale(x), Scale(y), Scale(width), Scale(height), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
    SetControlFont(control);
    return control;
}

void SetEditInteger(HWND edit, long long value) {
    SetWindowTextW(edit, std::to_wstring(value).c_str());
}

long long ReadInteger(HWND edit, long long minimum, long long maximum, long long fallback = 0) {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    wchar_t *end = nullptr;
    const long long parsed = std::wcstoll(text, &end, 10);
    if (end == text) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

double ReadDouble(HWND edit, double minimum, double maximum, double fallback) {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    wchar_t *end = nullptr;
    const double parsed = std::wcstod(text, &end);
    if (end == text || !std::isfinite(parsed)) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

std::int64_t IntervalMilliseconds() {
    return ReadInteger(app.hoursEdit, 0, 596) * 3'600'000LL
         + ReadInteger(app.minutesEdit, 0, 59) * 60'000LL
         + ReadInteger(app.secondsEdit, 0, 59) * 1'000LL
         + ReadInteger(app.millisecondsEdit, 0, 999);
}

void SetRadioPair(HWND first, HWND second, bool firstChecked);

int CpsSliderPosition(double cps) {
    cps = std::clamp(cps, 0.01, 1000.0);
    return static_cast<int>(std::llround((std::log10(cps) + 2.0) * 100.0));
}

double CpsFromSliderPosition(int position) {
    return std::pow(10.0, -2.0 + static_cast<double>(position) / 100.0);
}

void UpdateTimingSummary() {
    if (!app.timingSummaryLabel) {
        return;
    }
    const std::int64_t milliseconds = std::max<std::int64_t>(1, IntervalMilliseconds());
    const double cps = 1000.0 / static_cast<double>(milliseconds);
    wchar_t summary[96]{};
    if (milliseconds < 1000) {
        std::swprintf(summary, std::size(summary), L"%lld ms  |  %.2f CPS",
                      static_cast<long long>(milliseconds), cps);
    } else if (milliseconds % 1000 == 0 && milliseconds < 60'000) {
        std::swprintf(summary, std::size(summary), L"%lld sec  |  %.2f CPS",
                      static_cast<long long>(milliseconds / 1000), cps);
    } else {
        std::swprintf(summary, std::size(summary), L"%lld ms  |  %.2f CPS",
                      static_cast<long long>(milliseconds), cps);
    }
    SetWindowTextW(app.timingSummaryLabel, summary);
}

void SetIntervalMilliseconds(std::int64_t milliseconds) {
    app.updatingTiming = true;
    milliseconds = std::clamp<std::int64_t>(milliseconds, 1, 596LL * 3'600'000LL + 3'599'999LL);
    SetEditInteger(app.hoursEdit, milliseconds / 3'600'000LL);
    milliseconds %= 3'600'000LL;
    SetEditInteger(app.minutesEdit, milliseconds / 60'000LL);
    milliseconds %= 60'000LL;
    SetEditInteger(app.secondsEdit, milliseconds / 1'000LL);
    SetEditInteger(app.millisecondsEdit, milliseconds % 1'000LL);
    app.updatingTiming = false;
}

void UpdateCpsFromInterval() {
    const std::int64_t milliseconds = IntervalMilliseconds();
    if (milliseconds <= 0) {
        return;
    }
    app.updatingTiming = true;
    wchar_t value[32]{};
    const double cps = 1000.0 / static_cast<double>(milliseconds);
    std::swprintf(value, std::size(value), L"%.2f", cps);
    SetWindowTextW(app.cpsEdit, value);
    SendMessageW(app.cpsSlider, TBM_SETPOS, TRUE, CpsSliderPosition(cps));
    app.updatingTiming = false;
    UpdateTimingSummary();
}

void UpdateIntervalFromCps() {
    const double cps = ReadDouble(app.cpsEdit, 0.01, 1000.0, 10.0);
    app.updatingTiming = true;
    SendMessageW(app.cpsSlider, TBM_SETPOS, TRUE, CpsSliderPosition(cps));
    app.updatingTiming = false;
    SetIntervalMilliseconds(std::max<std::int64_t>(1, std::llround(1000.0 / cps)));
    UpdateTimingSummary();
}

void UpdateCpsFromSlider() {
    const int position = static_cast<int>(SendMessageW(app.cpsSlider, TBM_GETPOS, 0, 0));
    const double cps = CpsFromSliderPosition(position);
    app.updatingTiming = true;
    wchar_t value[32]{};
    std::swprintf(value, std::size(value), L"%.2f", cps);
    SetWindowTextW(app.cpsEdit, value);
    app.updatingTiming = false;
    SetIntervalMilliseconds(std::max<std::int64_t>(1, std::llround(1000.0 / cps)));
    UpdateTimingSummary();
}

void SetTimingMode(bool cpsMode) {
    app.cpsMode = cpsMode;
    SetRadioPair(app.intervalModeButton, app.cpsModeButton, !cpsMode);
    for (HWND edit : {app.hoursEdit, app.minutesEdit, app.secondsEdit, app.millisecondsEdit}) {
        ShowWindow(edit, cpsMode ? SW_HIDE : SW_SHOW);
    }
    for (HWND label : app.timingUnitLabels) {
        ShowWindow(label, cpsMode ? SW_HIDE : SW_SHOW);
    }
    ShowWindow(app.cpsEdit, cpsMode ? SW_SHOW : SW_HIDE);
    ShowWindow(app.cpsUnitLabel, cpsMode ? SW_SHOW : SW_HIDE);
    ShowWindow(app.cpsHintLabel, cpsMode ? SW_SHOW : SW_HIDE);
    ShowWindow(app.cpsSlider, cpsMode ? SW_SHOW : SW_HIDE);
}

void SetStatus(std::wstring text) {
    app.status = std::move(text);
    if (app.statusBar) {
        SendMessageW(app.statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(app.status.c_str()));
    }
}

void UpdateClickStatistics(bool force = false) {
    if (!app.statusBar) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!force && app.lastStatsUpdate != 0 && now - app.lastStatsUpdate < 250) {
        return;
    }
    app.lastStatsUpdate = now;
    const ULONGLONG elapsed = app.clickRunStart == 0 ? 0 : now - app.clickRunStart;
    const double actualCps = elapsed == 0
        ? 0.0
        : static_cast<double>(app.successfulClicks) * 1000.0 / static_cast<double>(elapsed);
    wchar_t statistics[96]{};
    std::swprintf(statistics, std::size(statistics), L"Clicks: %llu  |  %.2f CPS",
                  static_cast<unsigned long long>(app.successfulClicks), actualCps);
    SendMessageW(app.statusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(statistics));
}

void SetRadioPair(HWND first, HWND second, bool firstChecked) {
    SendMessageW(first, BM_SETCHECK, firstChecked ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(second, BM_SETCHECK, firstChecked ? BST_UNCHECKED : BST_CHECKED, 0);
}

bool IsEditableControl(HWND control) {
    const std::array controls{
        app.intervalModeButton, app.cpsModeButton, app.hoursEdit, app.minutesEdit,
        app.secondsEdit, app.millisecondsEdit, app.cpsEdit, app.cpsSlider,
        app.infiniteRepeatRadio, app.finiteRepeatRadio, app.repeatCountEdit,
        app.currentPositionRadio, app.fixedPositionRadio, app.xEdit, app.yEdit, app.captureButton,
        app.mouseButtons[0], app.mouseButtons[1]};
    return std::find(controls.begin(), controls.end(), control) != controls.end();
}

void UpdateControlStates() {
    EnumChildWindows(app.window, [](HWND control, LPARAM) -> BOOL {
        if (IsEditableControl(control)) {
            EnableWindow(control, !app.clicking);
        }
        return TRUE;
    }, 0);

    EnableWindow(app.repeatCountEdit, !app.clicking && !app.infiniteRepeat);
    EnableWindow(app.xEdit, !app.clicking && app.fixedPosition);
    EnableWindow(app.yEdit, !app.clicking && app.fixedPosition);
    EnableWindow(app.startButton, !app.clicking);
    EnableWindow(app.stopButton, app.clicking);
    InvalidateRect(app.window, nullptr, TRUE);
}

DWORD WINAPI ClickTimerThreadProcedure(LPVOID) {
    const HANDLE waitHandles[]{app.clickStopEvent, app.clickTimer};
    while (true) {
        const DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) {
            return 0;
        }
        if (result != WAIT_OBJECT_0 + 1 || !PostMessageW(app.window, PerformClickMessage, 0, 0)) {
            return 1;
        }
    }
}

void StopClickTimer() {
    if (app.clickStopEvent) {
        SetEvent(app.clickStopEvent);
    }
    if (app.clickTimer) {
        CancelWaitableTimer(app.clickTimer);
    }
    if (app.clickTimerThread) {
        WaitForSingleObject(app.clickTimerThread, INFINITE);
        CloseHandle(app.clickTimerThread);
        app.clickTimerThread = nullptr;
    }
    if (app.clickTimer) {
        CloseHandle(app.clickTimer);
        app.clickTimer = nullptr;
    }
    if (app.clickStopEvent) {
        CloseHandle(app.clickStopEvent);
        app.clickStopEvent = nullptr;
    }
    if (app.timerResolutionRaised) {
        timeEndPeriod(1);
        app.timerResolutionRaised = false;
    }
}

bool StartClickTimer(DWORD intervalMilliseconds) {
    StopClickTimer();
    intervalMilliseconds = std::max<DWORD>(1, intervalMilliseconds);
    app.timerResolutionRaised = timeBeginPeriod(1) == TIMERR_NOERROR;
    app.clickStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    app.clickTimer = CreateWaitableTimerExW(
        nullptr, nullptr, HighResolutionTimerFlag, TIMER_ALL_ACCESS);
    if (!app.clickTimer) {
        app.clickTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (!app.clickStopEvent || !app.clickTimer) {
        StopClickTimer();
        return false;
    }

    LARGE_INTEGER dueTime{};
    dueTime.QuadPart = -static_cast<LONGLONG>(intervalMilliseconds) * 10'000LL;
    if (!SetWaitableTimer(app.clickTimer, &dueTime, static_cast<LONG>(intervalMilliseconds),
                          nullptr, nullptr, FALSE)) {
        StopClickTimer();
        return false;
    }
    app.clickTimerThread = CreateThread(nullptr, 0, ClickTimerThreadProcedure, nullptr, 0, nullptr);
    if (!app.clickTimerThread) {
        StopClickTimer();
        return false;
    }
    return true;
}

void StopClicking();

void PerformClick() {
    if (!app.clicking) {
        return;
    }
    if (app.fixedPosition) {
        SetCursorPos(
            static_cast<int>(ReadInteger(app.xEdit, -100000, 100000)),
            static_cast<int>(ReadInteger(app.yEdit, -100000, 100000)));
    }

    DWORD downFlag = MOUSEEVENTF_LEFTDOWN;
    DWORD upFlag = MOUSEEVENTF_LEFTUP;
    if (app.mouseButton == 1) {
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag = MOUSEEVENTF_RIGHTUP;
    }

    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = downFlag;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = upFlag;
    if (SendInput(2, inputs, sizeof(INPUT)) == 2) {
        ++app.successfulClicks;
    }
    UpdateClickStatistics();

    ++app.completedRepeats;
    const int repeatCount = static_cast<int>(ReadInteger(app.repeatCountEdit, 1, 999999999, 10));
    if (!app.infiniteRepeat && app.completedRepeats >= repeatCount) {
        StopClicking();
    }
}

void StartClicking() {
    if (app.clicking) {
        return;
    }
    std::int64_t interval = IntervalMilliseconds();
    if (interval <= 0) {
        interval = 1;
        SetIntervalMilliseconds(interval);
        UpdateCpsFromInterval();
    }
    const UINT timerInterval = static_cast<UINT>(std::min<std::int64_t>(interval, USER_TIMER_MAXIMUM));
    app.completedRepeats = 0;
    app.successfulClicks = 0;
    app.clickRunStart = GetTickCount64();
    app.lastStatsUpdate = 0;
    app.clicking = true;
    if (!StartClickTimer(std::max<UINT>(1, timerInterval))) {
        app.clicking = false;
        SetStatus(L"Could not start the high-resolution timer");
        UpdateClickStatistics(true);
        UpdateControlStates();
        return;
    }
    SetStatus(L"Running - " + std::to_wstring(interval) + L" ms");
    UpdateClickStatistics(true);
    UpdateControlStates();
}

void StopClicking() {
    if (!app.clicking) {
        return;
    }
    app.clicking = false;
    StopClickTimer();
    UpdateClickStatistics(true);
    SetStatus(L"Stopped - " + std::to_wstring(app.completedRepeats) + L" cycle(s)");
    UpdateControlStates();
}

void ToggleClicking() {
    app.clicking ? StopClicking() : StartClicking();
}

void WriteRegistryDword(HKEY key, const wchar_t *name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
}

DWORD ReadRegistryDword(HKEY key, const wchar_t *name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &size) != ERROR_SUCCESS
        || type != REG_DWORD) {
        return fallback;
    }
    return value;
}

void SaveSettings() {
    KillTimer(app.window, SettingsSaveTimerId);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return;
    }
    WriteRegistryDword(key, L"IntervalMilliseconds", static_cast<DWORD>(IntervalMilliseconds()));
    WriteRegistryDword(key, L"CpsMode", app.cpsMode);
    WriteRegistryDword(key, L"InfiniteRepeat", app.infiniteRepeat);
    WriteRegistryDword(key, L"RepeatCount", static_cast<DWORD>(ReadInteger(app.repeatCountEdit, 1, 999999999, 10)));
    WriteRegistryDword(key, L"FixedPosition", app.fixedPosition);
    WriteRegistryDword(key, L"PositionX", static_cast<DWORD>(static_cast<std::int32_t>(ReadInteger(app.xEdit, -100000, 100000))));
    WriteRegistryDword(key, L"PositionY", static_cast<DWORD>(static_cast<std::int32_t>(ReadInteger(app.yEdit, -100000, 100000))));
    WriteRegistryDword(key, L"MouseButton", static_cast<DWORD>(app.mouseButton));
    WriteRegistryDword(key, L"AlwaysOnTop", SendMessageW(app.alwaysOnTopCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    RegCloseKey(key);
}

void ScheduleSettingsSave() {
    if (app.loadingSettings) {
        return;
    }
    KillTimer(app.window, SettingsSaveTimerId);
    SetTimer(app.window, SettingsSaveTimerId, 500, nullptr);
}

void LoadSettings() {
    app.loadingSettings = true;
    DWORD interval = 100;
    DWORD repeatCount = 10;
    DWORD x = 0;
    DWORD y = 0;
    DWORD alwaysOnTop = 0;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RegistryPath, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        interval = ReadRegistryDword(key, L"IntervalMilliseconds", 100);
        app.cpsMode = ReadRegistryDword(key, L"CpsMode", 0) != 0;
        app.infiniteRepeat = ReadRegistryDword(key, L"InfiniteRepeat", 1) != 0;
        repeatCount = ReadRegistryDword(key, L"RepeatCount", 10);
        app.fixedPosition = ReadRegistryDword(key, L"FixedPosition", 0) != 0;
        x = ReadRegistryDword(key, L"PositionX", 0);
        y = ReadRegistryDword(key, L"PositionY", 0);
        app.mouseButton = static_cast<int>(std::min<DWORD>(ReadRegistryDword(key, L"MouseButton", 0), 1));
        alwaysOnTop = ReadRegistryDword(key, L"AlwaysOnTop", 0);
        RegCloseKey(key);
    }

    SetIntervalMilliseconds(std::max<DWORD>(1, interval));
    UpdateCpsFromInterval();
    SetEditInteger(app.repeatCountEdit, std::clamp<DWORD>(repeatCount, 1, 999999999));
    SetEditInteger(app.xEdit, static_cast<std::int32_t>(x));
    SetEditInteger(app.yEdit, static_cast<std::int32_t>(y));
    SetRadioPair(app.infiniteRepeatRadio, app.finiteRepeatRadio, app.infiniteRepeat);
    SetRadioPair(app.currentPositionRadio, app.fixedPositionRadio, !app.fixedPosition);
    for (int index = 0; index < static_cast<int>(app.mouseButtons.size()); ++index) {
        SendMessageW(app.mouseButtons[index], BM_SETCHECK,
                     index == app.mouseButton ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    SendMessageW(app.alwaysOnTopCheck, BM_SETCHECK, alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
    SetTimingMode(app.cpsMode);
    if (alwaysOnTop) {
        SetWindowPos(app.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    UpdateControlStates();
    app.loadingSettings = false;
}

void DrawInterface(HDC dc, const RECT &client) {
    FillRect(dc, &client, app.backgroundBrush);
}

void CreateControls() {
    CreateGroupBox(L"Click interval", 8, 7, 424, 97);
    CreateGroupBox(L"Click repeat", 8, 110, 206, 101);
    CreateGroupBox(L"Click position", 220, 110, 212, 101);
    CreateGroupBox(L"Mouse button", 8, 217, 424, 55);

    app.intervalModeButton = CreateToggleButton(IntervalModeId, L"Interval", 20, 27, 70, 24, true);
    app.cpsModeButton = CreateToggleButton(CpsModeId, L"CPS", 93, 27, 54, 24, false);
    app.timingSummaryLabel = CreateLabel(L"", 257, 27, 162, 24,
                                         SS_SUNKEN | SS_CENTER | SS_CENTERIMAGE, app.smallFont);

    app.timingUnitLabels = {
        CreateLabel(L"HOURS", 20, 54, 84, 15, SS_CENTER, app.smallFont),
        CreateLabel(L"MINUTES", 109, 54, 84, 15, SS_CENTER, app.smallFont),
        CreateLabel(L"SECONDS", 198, 54, 84, 15, SS_CENTER, app.smallFont),
        CreateLabel(L"MILLISECONDS", 287, 54, 132, 15, SS_CENTER, app.smallFont)};
    app.hoursEdit = CreateNumberEdit(HoursEditId, 20, 70, 84, 25);
    app.minutesEdit = CreateNumberEdit(MinutesEditId, 109, 70, 84, 25);
    app.secondsEdit = CreateNumberEdit(SecondsEditId, 198, 70, 84, 25);
    app.millisecondsEdit = CreateNumberEdit(MillisecondsEditId, 287, 70, 132, 25);
    app.cpsUnitLabel = CreateLabel(L"CPS", 20, 54, 88, 15, SS_CENTER, app.smallFont);
    app.cpsEdit = CreateNumberEdit(CpsEditId, 20, 70, 88, 25);
    app.cpsSlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        Scale(116), Scale(64), Scale(213), Scale(31), app.window,
        nullptr, app.instance, nullptr);
    SendMessageW(app.cpsSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, CpsSliderSteps));
    SendMessageW(app.cpsSlider, TBM_SETPAGESIZE, 0, 25);
    app.cpsHintLabel = CreateLabel(L"0.01 - 1000", 333, 67, 86, 25,
                                   SS_CENTER | SS_CENTERIMAGE, app.smallFont);

    app.infiniteRepeatRadio = CreateRadio(InfiniteRepeatId, L"Repeat until stopped", 20, 130, 181, 23, true);
    app.finiteRepeatRadio = CreateRadio(FiniteRepeatId, L"Repeat", 20, 164, 65, 25, false);
    app.repeatCountEdit = CreateNumberEdit(RepeatCountEditId, 86, 163, 78, 26);
    CreateLabel(L"times", 169, 164, 38, 25, SS_LEFT | SS_CENTERIMAGE);

    app.currentPositionRadio = CreateRadio(CurrentPositionId, L"Current cursor position", 232, 130, 184, 23, true);
    app.fixedPositionRadio = CreateRadio(FixedPositionId, L"Fixed position", 232, 153, 140, 23, false);
    CreateLabel(L"X:", 232, 177, 16, 25, SS_LEFT | SS_CENTERIMAGE);
    app.xEdit = CreateNumberEdit(XEditId, 248, 176, 47, 26, true);
    CreateLabel(L"Y:", 300, 177, 16, 25, SS_LEFT | SS_CENTERIMAGE);
    app.yEdit = CreateNumberEdit(YEditId, 316, 176, 47, 26, true);
    app.captureButton = CreateButton(CapturePositionId, L"Pick...", 368, 175, 51, 27);

    app.mouseButtons = {
        CreateToggleButton(MouseLeftId, L"Left", 20, 237, 76, 26, true),
        CreateToggleButton(MouseRightId, L"Right", 99, 237, 76, 26, false)};

    app.startButton = CreateButton(StartButtonId, L"&Start [F6]", 8, 279, 134, 30, BS_DEFPUSHBUTTON);
    app.stopButton = CreateButton(StopButtonId, L"S&top [F7]", 153, 279, 134, 30);
    app.toggleButton = CreateButton(ToggleButtonId, L"&Toggle [F8]", 298, 279, 134, 30);

    app.alwaysOnTopCheck = CreateWindowExW(
        0, L"BUTTON", L"Always on top",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        Scale(307), Scale(238), Scale(112), Scale(18), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(AlwaysOnTopId)), app.instance, nullptr);
    SetControlFont(app.alwaysOnTopCheck);

    app.statusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, app.status.c_str(),
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, app.window, nullptr, app.instance, nullptr);
    SetControlFont(app.statusBar, app.smallFont);
    const int statusParts[] = {Scale(230), -1};
    SendMessageW(app.statusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(statusParts));
    SendMessageW(app.statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(app.status.c_str()));
    SendMessageW(app.statusBar, SB_SETTEXTW, 1,
                 reinterpret_cast<LPARAM>(L"Clicks: 0  |  0.00 CPS"));
}

LRESULT CALLBACK CaptureMouseProcedure(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && app.capturingPosition) {
        if (wParam == WM_LBUTTONDOWN) {
            app.captureButtonDown = true;
            return 1;
        }
        if (wParam == WM_LBUTTONUP && app.captureButtonDown) {
            app.captureButtonDown = false;
            PostMessageW(app.window, FinishPositionCaptureMessage, 0, 0);
            return 1;
        }
        if (wParam == WM_RBUTTONDOWN) {
            PostMessageW(app.window, CancelPositionCaptureMessage, 0, 0);
            return 1;
        }
        if (wParam == WM_RBUTTONUP) {
            return 1;
        }
    }
    return CallNextHookEx(app.captureMouseHook, code, wParam, lParam);
}

void StopPositionCapture() {
    if (app.captureMouseHook) {
        UnhookWindowsHookEx(app.captureMouseHook);
        app.captureMouseHook = nullptr;
    }
    app.capturingPosition = false;
    app.captureButtonDown = false;
    EnableWindow(app.captureButton, TRUE);
}

void CapturePosition() {
    if (app.capturingPosition) {
        return;
    }
    app.captureMouseHook = SetWindowsHookExW(WH_MOUSE_LL, CaptureMouseProcedure, app.instance, 0);
    if (!app.captureMouseHook) {
        SetStatus(L"Could not start position picker");
        return;
    }
    app.capturingPosition = true;
    EnableWindow(app.captureButton, FALSE);
    SetStatus(L"Left-click target; right-click cancels");
    ShowWindow(app.window, SW_HIDE);
}

void FinishPositionCapture() {
    POINT point{};
    GetCursorPos(&point);
    StopPositionCapture();
    SetEditInteger(app.xEdit, point.x);
    SetEditInteger(app.yEdit, point.y);
    app.fixedPosition = true;
    SetRadioPair(app.currentPositionRadio, app.fixedPositionRadio, false);
    SetStatus(L"Captured (" + std::to_wstring(point.x) + L", " + std::to_wstring(point.y) + L")");
    ShowWindow(app.window, SW_SHOW);
    SetForegroundWindow(app.window);
    UpdateControlStates();
    ScheduleSettingsSave();
}

void CancelPositionCapture() {
    StopPositionCapture();
    SetStatus(L"Position pick cancelled");
    ShowWindow(app.window, SW_SHOW);
    SetForegroundWindow(app.window);
}

void ApplyAlwaysOnTop(bool enabled) {
    SendMessageW(app.alwaysOnTopCheck, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowPos(app.window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void HandleCommand(int id, int notification) {
    if (notification == EN_CHANGE && !app.updatingTiming) {
        if (id >= HoursEditId && id <= MillisecondsEditId) {
            UpdateCpsFromInterval();
        } else if (id == CpsEditId) {
            UpdateIntervalFromCps();
        }
        ScheduleSettingsSave();
        return;
    }
    if (notification != BN_CLICKED) {
        return;
    }

    switch (id) {
    case IntervalModeId:
        SetTimingMode(false);
        UpdateCpsFromInterval();
        ScheduleSettingsSave();
        break;
    case CpsModeId:
        SetTimingMode(true);
        UpdateCpsFromInterval();
        ScheduleSettingsSave();
        break;
    case InfiniteRepeatId:
        app.infiniteRepeat = true;
        SetRadioPair(app.infiniteRepeatRadio, app.finiteRepeatRadio, true);
        UpdateControlStates();
        ScheduleSettingsSave();
        break;
    case FiniteRepeatId:
        app.infiniteRepeat = false;
        SetRadioPair(app.infiniteRepeatRadio, app.finiteRepeatRadio, false);
        UpdateControlStates();
        ScheduleSettingsSave();
        break;
    case CurrentPositionId:
        app.fixedPosition = false;
        SetRadioPair(app.currentPositionRadio, app.fixedPositionRadio, true);
        UpdateControlStates();
        ScheduleSettingsSave();
        break;
    case FixedPositionId:
        app.fixedPosition = true;
        SetRadioPair(app.currentPositionRadio, app.fixedPositionRadio, false);
        UpdateControlStates();
        ScheduleSettingsSave();
        break;
    case CapturePositionId:
        CapturePosition();
        break;
    case MouseLeftId:
    case MouseRightId:
        app.mouseButton = id - MouseLeftId;
        ScheduleSettingsSave();
        break;
    case StartButtonId:
        StartClicking();
        break;
    case StopButtonId:
        StopClicking();
        break;
    case ToggleButtonId:
        ToggleClicking();
        break;
    case AlwaysOnTopId: {
        const bool enabled = SendMessageW(app.alwaysOnTopCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        ApplyAlwaysOnTop(enabled);
        ScheduleSettingsSave();
        break;
    }
    default:
        break;
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        app.window = window;
        app.normalFont = CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft Sans Serif");
        app.boldFont = CreateFontW(-Scale(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft Sans Serif");
        app.smallFont = CreateFontW(-Scale(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft Sans Serif");
        app.backgroundBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

        CreateControls();
        LoadSettings();

        const bool startOk = RegisterHotKey(window, StartHotkeyId, MOD_NOREPEAT, VK_F6) != FALSE;
        const bool stopOk = RegisterHotKey(window, StopHotkeyId, MOD_NOREPEAT, VK_F7) != FALSE;
        const bool toggleOk = RegisterHotKey(window, ToggleHotkeyId, MOD_NOREPEAT, VK_F8) != FALSE;
        if (!startOk || !stopOk || !toggleOk) {
            SetStatus(L"A hotkey is already in use");
        }
        return 0;
    }
    case WM_COMMAND:
        HandleCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == SettingsSaveTimerId) {
            SaveSettings();
        }
        return 0;
    case PerformClickMessage:
        PerformClick();
        return 0;
    case FinishPositionCaptureMessage:
        FinishPositionCapture();
        return 0;
    case CancelPositionCaptureMessage:
        CancelPositionCapture();
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == app.cpsSlider && !app.updatingTiming) {
            UpdateCpsFromSlider();
            ScheduleSettingsSave();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_HOTKEY:
        if (wParam == StartHotkeyId) StartClicking();
        else if (wParam == StopHotkeyId && app.capturingPosition) CancelPositionCapture();
        else if (wParam == StopHotkeyId) StopClicking();
        else if (wParam == ToggleHotkeyId) ToggleClicking();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F6) StartClicking();
        else if (wParam == VK_F7) StopClicking();
        else if (wParam == VK_F8) ToggleClicking();
        else return DefWindowProcW(window, message, wParam, lParam);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(app.backgroundBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_SIZE:
        if (app.statusBar) {
            SendMessageW(app.statusBar, WM_SIZE, 0, 0);
            const int statusParts[] = {Scale(230), -1};
            SendMessageW(app.statusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(statusParts));
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC memoryDc = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        const HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        DrawInterface(memoryDc, client);
        BitBlt(dc, 0, 0, client.right, client.bottom, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        StopPositionCapture();
        SaveSettings();
        StopClicking();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        StopClickTimer();
        UnregisterHotKey(window, StartHotkeyId);
        UnregisterHotKey(window, StopHotkeyId);
        UnregisterHotKey(window, ToggleHotkeyId);
        if (app.normalFont) DeleteObject(app.normalFont);
        if (app.boldFont) DeleteObject(app.boldFont);
        if (app.smallFont) DeleteObject(app.smallFont);
        if (app.backgroundBrush) DeleteObject(app.backgroundBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    SetProcessDPIAware();
    app.instance = instance;

    HDC screenDc = GetDC(nullptr);
    app.dpi = GetDeviceCaps(screenDc, LOGPIXELSX);
    ReleaseDC(nullptr, screenDc);

    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&commonControls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!windowClass.hIcon) {
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WindowClassName;
    windowClass.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!windowClass.hIconSm) {
        windowClass.hIconSm = windowClass.hIcon;
    }
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }

    RECT windowRect{0, 0, Scale(440), Scale(335)};
    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&windowRect, windowStyle, FALSE, 0);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const bool qaOffscreen = GetEnvironmentVariableW(L"AUTOCLICKER_QA_OFFSCREEN", nullptr, 0) > 0;
    const int x = qaOffscreen
        ? workArea.right + Scale(20)
        : workArea.left + (workArea.right - workArea.left - windowWidth) / 2;
    const int y = workArea.top + (workArea.bottom - workArea.top - windowHeight) / 2;

    HWND window = CreateWindowExW(
        0, WindowClassName, AutoClickerWindowTitle, windowStyle,
        x, y, windowWidth, windowHeight, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 2;
    }

    ShowWindow(window, qaOffscreen ? SW_SHOWNOACTIVATE : showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
