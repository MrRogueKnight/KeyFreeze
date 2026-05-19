// ============================================================================
// KeyFreeze - Professional Per-Keyboard Input Locking Utility
// ============================================================================
// Copyright (c) KeyFreeze Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <commctrl.h>
#include <dbt.h>
#include <devguid.h>
#include <hidsdi.h>
#include <hidusage.h>
#include <setupapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windows.h>


#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>


#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ----------------------------------------------------------------------------
// Resource Identifiers
// ----------------------------------------------------------------------------
#define IDD_KEYBOARD_DLG 200
#define IDC_KEYBOARD_LIST 201
#define IDC_SELECT_ALL 202
#define IDC_DESELECT_ALL 203
#define IDC_STATS_TEXT 204
#define IDI_KEYFREEZE 300

// ----------------------------------------------------------------------------
// Application Constants
// ----------------------------------------------------------------------------
#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 101

#define IDM_EXIT 1001
#define IDM_LOCK_ALL 1002
#define IDM_UNLOCK_ALL 1003
#define IDM_SELECT 1004
#define IDM_ABOUT 1005

#define APP_NAME L"KeyFreeze"
#define APP_MUTEX L"Global\\KeyFreeze_Instance"
#define CONFIG_FILENAME L"KeyFreeze.config"

#define INPUT_TIMESTAMP_VALIDITY_MS 100
#define DEVICE_REFRESH_INTERVAL_MS 2000

// ----------------------------------------------------------------------------
// Keyboard Device Descriptor
// ----------------------------------------------------------------------------
struct KeyboardDevice {
  std::wstring path;
  std::wstring name;
  std::wstring description;
  DWORD vendorId = 0;
  DWORD productId = 0;
  HANDLE rawHandle = nullptr;
  bool isConnected = false;
  bool isLocked = false;

  bool operator==(const KeyboardDevice &other) const {
    return path == other.path;
  }
};

// ----------------------------------------------------------------------------
// RAII Hook Wrapper
// ----------------------------------------------------------------------------
class HookGuard {
public:
  HookGuard() = default;
  ~HookGuard() { Release(); }

  HookGuard(const HookGuard &) = delete;
  HookGuard &operator=(const HookGuard &) = delete;

  HookGuard(HookGuard &&other) noexcept : m_hook(other.m_hook) {
    other.m_hook = nullptr;
  }

  HookGuard &operator=(HookGuard &&other) noexcept {
    if (this != &other) {
      Release();
      m_hook = other.m_hook;
      other.m_hook = nullptr;
    }
    return *this;
  }

  bool Install(HINSTANCE instance, HOOKPROC procedure) {
    Release();
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, procedure, instance, 0);
    return m_hook != nullptr;
  }

  void Release() {
    if (m_hook) {
      UnhookWindowsHookEx(m_hook);
      m_hook = nullptr;
    }
  }

  explicit operator bool() const { return m_hook != nullptr; }
  operator HHOOK() const { return m_hook; }

private:
  HHOOK m_hook = nullptr;
};

// ----------------------------------------------------------------------------
// RAII Instance Mutex
// ----------------------------------------------------------------------------
class InstanceMutex {
public:
  InstanceMutex() = default;
  ~InstanceMutex() { Release(); }

  InstanceMutex(const InstanceMutex &) = delete;
  InstanceMutex &operator=(const InstanceMutex &) = delete;

  bool Create(const wchar_t *name) {
    Release();
    m_handle = CreateMutexW(nullptr, FALSE, name);
    if (m_handle && GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(m_handle);
      m_handle = nullptr;
      return false;
    }
    return m_handle != nullptr;
  }

  void Release() {
    if (m_handle) {
      ReleaseMutex(m_handle);
      CloseHandle(m_handle);
      m_handle = nullptr;
    }
  }

private:
  HANDLE m_handle = nullptr;
};

// ----------------------------------------------------------------------------
// Main Application
// ----------------------------------------------------------------------------
class KeyFreezeApp {
public:
  explicit KeyFreezeApp(HINSTANCE instance);
  ~KeyFreezeApp();

  KeyFreezeApp(const KeyFreezeApp &) = delete;
  KeyFreezeApp &operator=(const KeyFreezeApp &) = delete;

  bool Initialize();
  int Run();
  void Shutdown();

  static KeyFreezeApp *Instance();

  bool IsDeviceLocked(const std::wstring &devicePath) const;
  size_t GetLockedCount() const;
  size_t GetTotalCount() const;
  std::vector<KeyboardDevice> GetDevices() const;

  void LockAll();
  void UnlockAll();
  void ToggleDevice(const std::wstring &devicePath);

  bool ShouldBlockCurrentInput() const;
  void RecordBlockedKeystroke();

  HINSTANCE GetInstanceHandle() const { return m_instance; }
  HWND GetMainWindow() const { return m_hwnd; }

private:
  bool CheckElevation() const;
  void RegisterRawInput();
  void RegisterDeviceNotifications();
  void EnumerateDevices();
  void CreateTrayIcon();
  void LoadConfiguration();
  void SaveConfiguration();

  void UpdateTrayIcon();
  void RemoveTrayIcon();
  void ShowBalloon(const wchar_t *title, const wchar_t *message) const;
  void ShowKeyboardDialog();
  void ProcessRawInput(HRAWINPUT rawInput);
  void HandleDeviceArrival();
  void HandleDeviceRemoval();
  void RefreshLoop();

  std::wstring GetConfigFilePath() const;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam);
  static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam,
                                           LPARAM lParam);
  static INT_PTR CALLBACK KeyboardDialogProc(HWND dialog, UINT message,
                                             WPARAM wParam, LPARAM lParam);

  HINSTANCE m_instance;
  HWND m_hwnd = nullptr;

  mutable std::mutex m_mutex;
  std::vector<KeyboardDevice> m_keyboards;
  std::set<std::wstring> m_lockedPaths;

  std::wstring m_lastActiveDevicePath;
  std::chrono::steady_clock::time_point m_lastInputTime;

  std::atomic<bool> m_running{false};
  HANDLE m_stopEvent = nullptr;
  std::thread m_refreshThread;

  HookGuard m_keyboardHook;
  InstanceMutex m_instanceMutex;

  bool m_showNotifications = true;
  bool m_autoSaveConfig = true;

  struct Statistics {
    std::atomic<uint64_t> keystrokesBlocked{0};
    std::atomic<uint64_t> lockEvents{0};
    std::atomic<uint64_t> unlockEvents{0};
  } m_statistics;

  static KeyFreezeApp *s_instance;
};

KeyFreezeApp *KeyFreezeApp::s_instance = nullptr;

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------
KeyFreezeApp::KeyFreezeApp(HINSTANCE instance) : m_instance(instance) {
  s_instance = this;
  m_lastInputTime = std::chrono::steady_clock::now();
}

KeyFreezeApp::~KeyFreezeApp() {
  Shutdown();
  s_instance = nullptr;
}

KeyFreezeApp *KeyFreezeApp::Instance() { return s_instance; }

// ----------------------------------------------------------------------------
// Initialize
// ----------------------------------------------------------------------------
bool KeyFreezeApp::Initialize() {
  if (!CheckElevation()) {
    MessageBoxW(nullptr,
                L"KeyFreeze requires administrator privileges to intercept "
                L"keyboard input.\n\n"
                L"Please right-click the application and select 'Run as "
                L"administrator'.",
                APP_NAME, MB_OK | MB_ICONWARNING);
    return false;
  }

  if (!m_instanceMutex.Create(APP_MUTEX)) {
    MessageBoxW(nullptr,
                L"KeyFreeze is already running.\n\n"
                L"Please check the system tray for the existing instance.",
                APP_NAME, MB_OK | MB_ICONINFORMATION);
    return false;
  }

  m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!m_stopEvent)
    return false;

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = m_instance;
  wc.hIcon = LoadIconW(m_instance, MAKEINTRESOURCE(IDI_KEYFREEZE));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"KeyFreeze_MainWindow";

  if (!RegisterClassExW(&wc))
    return false;

  m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           wc.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 0, 0,
                           nullptr, nullptr, m_instance, nullptr);

  if (!m_hwnd)
    return false;

  EnumerateDevices();
  LoadConfiguration();
  RegisterRawInput();
  RegisterDeviceNotifications();

  if (!m_keyboardHook.Install(m_instance, KeyboardHookProc)) {
    DWORD error = GetLastError();
    wchar_t msg[256];
    swprintf_s(msg,
               L"Failed to install keyboard hook (Error: %lu).\n\n"
               L"Ensure the application has administrator privileges.",
               error);
    MessageBoxW(nullptr, msg, APP_NAME, MB_OK | MB_ICONERROR);
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    return false;
  }

  CreateTrayIcon();

  m_running = true;
  m_refreshThread = std::thread(&KeyFreezeApp::RefreshLoop, this);

  if (m_showNotifications) {
    wchar_t msg[128];
    swprintf_s(msg, L"KeyFreeze is ready. %zu keyboard(s) detected.",
               m_keyboards.size());
    ShowBalloon(L"KeyFreeze Started", msg);
  }

  return true;
}

// ----------------------------------------------------------------------------
// Main Message Loop
// ----------------------------------------------------------------------------
int KeyFreezeApp::Run() {
  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

// ----------------------------------------------------------------------------
// Clean Shutdown
// ----------------------------------------------------------------------------
void KeyFreezeApp::Shutdown() {
  if (!m_running)
    return;

  m_running = false;
  SetEvent(m_stopEvent);

  if (m_refreshThread.joinable())
    m_refreshThread.join();

  UnlockAll();
  m_keyboardHook.Release();
  RemoveTrayIcon();
  SaveConfiguration();

  if (m_hwnd) {
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
  }
  if (m_stopEvent) {
    CloseHandle(m_stopEvent);
    m_stopEvent = nullptr;
  }
}

// ----------------------------------------------------------------------------
// Elevation Check
// ----------------------------------------------------------------------------
bool KeyFreezeApp::CheckElevation() const {
  BOOL elevated = FALSE;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION elevation = {0};
    DWORD size = sizeof(elevation);
    if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
      elevated = elevation.TokenIsElevated;
    CloseHandle(token);
  }
  return elevated != FALSE;
}

// ----------------------------------------------------------------------------
// Raw Input Registration
// ----------------------------------------------------------------------------
void KeyFreezeApp::RegisterRawInput() {
  RAWINPUTDEVICE rid[1];
  rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
  rid[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
  rid[0].dwFlags = RIDEV_INPUTSINK;
  rid[0].hwndTarget = m_hwnd;

  RegisterRawInputDevices(rid, 1, sizeof(RAWINPUTDEVICE));
}

// ----------------------------------------------------------------------------
// Device Notification Registration
// ----------------------------------------------------------------------------
void KeyFreezeApp::RegisterDeviceNotifications() {
  DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
  filter.dbcc_size = sizeof(filter);
  filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  filter.dbcc_classguid = GUID_DEVINTERFACE_KEYBOARD;

  RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
}

// ----------------------------------------------------------------------------
// Device Enumeration
// ----------------------------------------------------------------------------
void KeyFreezeApp::EnumerateDevices() {
  std::lock_guard<std::mutex> lock(m_mutex);

  UINT count = 0;
  GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
  if (count == 0)
    return;

  std::vector<RAWINPUTDEVICELIST> rawDevices(count);
  GetRawInputDeviceList(rawDevices.data(), &count, sizeof(RAWINPUTDEVICELIST));

  std::set<std::wstring> currentPaths;

  for (const auto &dev : rawDevices) {
    if (dev.dwType != RIM_TYPEKEYBOARD)
      continue;

    UINT len = 0;
    GetRawInputDeviceInfoW(dev.hDevice, RIDI_DEVICENAME, nullptr, &len);
    if (len == 0)
      continue;

    std::vector<wchar_t> buf(len);
    if (GetRawInputDeviceInfoW(dev.hDevice, RIDI_DEVICENAME, buf.data(),
                               &len) <= 0)
      continue;

    std::wstring path(buf.data());
    currentPaths.insert(path);

    auto it =
        std::find_if(m_keyboards.begin(), m_keyboards.end(),
                     [&](const KeyboardDevice &k) { return k.path == path; });

    if (it != m_keyboards.end()) {
      it->rawHandle = dev.hDevice;
      it->isConnected = true;
    } else {
      KeyboardDevice kb;
      kb.path = path;
      kb.rawHandle = dev.hDevice;
      kb.isConnected = true;

      auto vp = path.find(L"VID_");
      auto pp = path.find(L"PID_");
      if (vp != std::wstring::npos && pp != std::wstring::npos) {
        kb.vendorId = wcstoul(path.substr(vp + 4, 4).c_str(), nullptr, 16);
        kb.productId = wcstoul(path.substr(pp + 4, 4).c_str(), nullptr, 16);
      }

      wchar_t name[256];
      if (kb.vendorId && kb.productId)
        swprintf_s(name, L"Keyboard %zu [VID:%04X PID:%04X]",
                   m_keyboards.size() + 1, kb.vendorId, kb.productId);
      else
        swprintf_s(name, L"Keyboard %zu", m_keyboards.size() + 1);
      kb.name = name;
      kb.description = name;
      kb.isLocked = (m_lockedPaths.count(path) > 0);

      m_keyboards.push_back(std::move(kb));
    }
  }

  for (auto &kb : m_keyboards) {
    if (currentPaths.count(kb.path) == 0) {
      kb.isConnected = false;
      kb.rawHandle = nullptr;
    }
  }
}

// ----------------------------------------------------------------------------
// Lock Management
// ----------------------------------------------------------------------------
void KeyFreezeApp::LockAll() {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto &kb : m_keyboards)
    m_lockedPaths.insert(kb.path);
  for (auto &kb : m_keyboards)
    kb.isLocked = true;
  m_statistics.lockEvents++;
  UpdateTrayIcon();
  SaveConfiguration();
  if (m_showNotifications)
    ShowBalloon(APP_NAME, L"All keyboards locked");
}

void KeyFreezeApp::UnlockAll() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_lockedPaths.clear();
  for (auto &kb : m_keyboards)
    kb.isLocked = false;
  m_statistics.unlockEvents++;
  UpdateTrayIcon();
  SaveConfiguration();
  if (m_showNotifications)
    ShowBalloon(APP_NAME, L"All keyboards unlocked");
}

void KeyFreezeApp::ToggleDevice(const std::wstring &devicePath) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_lockedPaths.count(devicePath)) {
    m_lockedPaths.erase(devicePath);
    m_statistics.unlockEvents++;
  } else {
    m_lockedPaths.insert(devicePath);
    m_statistics.lockEvents++;
  }
  for (auto &kb : m_keyboards) {
    if (kb.path == devicePath) {
      kb.isLocked = (m_lockedPaths.count(devicePath) > 0);
      break;
    }
  }
  UpdateTrayIcon();
  SaveConfiguration();
}

// ----------------------------------------------------------------------------
// Query Methods
// ----------------------------------------------------------------------------
bool KeyFreezeApp::IsDeviceLocked(const std::wstring &devicePath) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_lockedPaths.count(devicePath) > 0;
}

size_t KeyFreezeApp::GetLockedCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_lockedPaths.size();
}

size_t KeyFreezeApp::GetTotalCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_keyboards.size();
}

std::vector<KeyboardDevice> KeyFreezeApp::GetDevices() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_keyboards;
}

// ----------------------------------------------------------------------------
// Raw Input Processing
// ----------------------------------------------------------------------------
void KeyFreezeApp::ProcessRawInput(HRAWINPUT rawInput) {
  UINT size = 0;
  GetRawInputData(rawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  if (size == 0)
    return;

  std::vector<BYTE> buf(size);
  if (GetRawInputData(rawInput, RID_INPUT, buf.data(), &size,
                      sizeof(RAWINPUTHEADER)) <= 0)
    return;

  auto *raw = reinterpret_cast<RAWINPUT *>(buf.data());
  if (raw->header.dwType != RIM_TYPEKEYBOARD)
    return;

  UINT len = 0;
  GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME, nullptr, &len);
  if (len > 0) {
    std::vector<wchar_t> path(len);
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME,
                               path.data(), &len) > 0) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_lastActiveDevicePath = path.data();
      m_lastInputTime = std::chrono::steady_clock::now();
    }
  }
}

// ----------------------------------------------------------------------------
// Hook Support
// ----------------------------------------------------------------------------
bool KeyFreezeApp::ShouldBlockCurrentInput() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_lastActiveDevicePath.empty())
    return false;

  auto elapsed = std::chrono::steady_clock::now() - m_lastInputTime;
  if (elapsed > std::chrono::milliseconds(INPUT_TIMESTAMP_VALIDITY_MS))
    return false;

  return m_lockedPaths.count(m_lastActiveDevicePath) > 0;
}

void KeyFreezeApp::RecordBlockedKeystroke() {
  m_statistics.keystrokesBlocked++;
}

// ----------------------------------------------------------------------------
// Configuration Persistence
// ----------------------------------------------------------------------------
std::wstring KeyFreezeApp::GetConfigFilePath() const {
  wchar_t path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
    std::wstring dir = std::wstring(path) + L"\\KeyFreeze";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + CONFIG_FILENAME;
  }
  return CONFIG_FILENAME;
}

void KeyFreezeApp::SaveConfiguration() {
  if (!m_autoSaveConfig)
    return;
  std::lock_guard<std::mutex> lock(m_mutex);

  std::wofstream file(GetConfigFilePath(), std::ios::trunc);
  if (file.is_open()) {
    file << L"# KeyFreeze Configuration\n";
    for (const auto &p : m_lockedPaths)
      file << p << L"\n";
  }
}

void KeyFreezeApp::LoadConfiguration() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::wifstream file(GetConfigFilePath());
  if (file.is_open()) {
    std::wstring line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == L'#')
        continue;
      line.erase(0, line.find_first_not_of(L" \t\r\n"));
      line.erase(line.find_last_not_of(L" \t\r\n") + 1);
      if (!line.empty())
        m_lockedPaths.insert(line);
    }
  }
}

// ----------------------------------------------------------------------------
// Tray Icon Management
// ----------------------------------------------------------------------------
void KeyFreezeApp::CreateTrayIcon() {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = m_hwnd;
  nid.uID = TRAY_ICON_ID;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID;
  nid.uCallbackMessage = WM_TRAYICON;
  nid.hIcon = LoadIconW(m_instance, MAKEINTRESOURCE(IDI_KEYFREEZE));
  StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), APP_NAME);
  Shell_NotifyIconW(NIM_ADD, &nid);
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void KeyFreezeApp::UpdateTrayIcon() {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = m_hwnd;
  nid.uID = TRAY_ICON_ID;
  nid.uFlags = NIF_TIP;

  size_t locked = GetLockedCount();
  size_t total = GetTotalCount();

  if (locked == 0)
    swprintf_s(nid.szTip, L"%s - All Unlocked | Blocked: %llu", APP_NAME,
               m_statistics.keystrokesBlocked.load());
  else if (locked == total)
    swprintf_s(nid.szTip, L"%s - All Locked (%zu) | Blocked: %llu", APP_NAME,
               locked, m_statistics.keystrokesBlocked.load());
  else
    swprintf_s(nid.szTip, L"%s - %zu/%zu Locked | Blocked: %llu", APP_NAME,
               locked, total, m_statistics.keystrokesBlocked.load());

  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void KeyFreezeApp::RemoveTrayIcon() {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = m_hwnd;
  nid.uID = TRAY_ICON_ID;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

void KeyFreezeApp::ShowBalloon(const wchar_t *title,
                               const wchar_t *message) const {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = m_hwnd;
  nid.uID = TRAY_ICON_ID;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = NIIF_INFO;
  nid.uTimeout = 5000;
  StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title);
  StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), message);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ----------------------------------------------------------------------------
// Device Hotplug Handlers
// ----------------------------------------------------------------------------
void KeyFreezeApp::HandleDeviceArrival() {
  EnumerateDevices();
  UpdateTrayIcon();
  if (m_showNotifications) {
    wchar_t msg[128];
    swprintf_s(msg, L"New keyboard detected. Total: %zu", m_keyboards.size());
    ShowBalloon(APP_NAME, msg);
  }
}

void KeyFreezeApp::HandleDeviceRemoval() {
  EnumerateDevices();
  UpdateTrayIcon();
  if (m_showNotifications) {
    wchar_t msg[128];
    swprintf_s(msg, L"Keyboard removed. Total: %zu", m_keyboards.size());
    ShowBalloon(APP_NAME, msg);
  }
}

// ----------------------------------------------------------------------------
// Keyboard Selection Dialog
// ----------------------------------------------------------------------------
void KeyFreezeApp::ShowKeyboardDialog() {
  DialogBoxParamW(m_instance, MAKEINTRESOURCE(IDD_KEYBOARD_DLG), m_hwnd,
                  KeyboardDialogProc, reinterpret_cast<LPARAM>(this));
}

// ----------------------------------------------------------------------------
// Background Refresh Thread
// ----------------------------------------------------------------------------
void KeyFreezeApp::RefreshLoop() {
  while (m_running) {
    if (WaitForSingleObject(m_stopEvent, DEVICE_REFRESH_INTERVAL_MS) ==
        WAIT_OBJECT_0)
      break;
    EnumerateDevices();
    UpdateTrayIcon();
  }
}

// ----------------------------------------------------------------------------
// Window Procedure
// ----------------------------------------------------------------------------
LRESULT CALLBACK KeyFreezeApp::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
  auto *app = Instance();

  switch (msg) {
  case WM_INPUT:
    if (app)
      app->ProcessRawInput(reinterpret_cast<HRAWINPUT>(lp));
    return DefWindowProcW(hwnd, msg, wp, lp);

  case WM_TRAYICON:
    if (!app)
      return 0;
    if (LOWORD(lp) == WM_LBUTTONUP) {
      if (app->GetLockedCount() > 0)
        app->UnlockAll();
      else
        app->LockAll();
    } else if (LOWORD(lp) == WM_RBUTTONUP) {
      POINT pt;
      GetCursorPos(&pt);
      HMENU menu = CreatePopupMenu();

      wchar_t status[128];
      swprintf_s(status, L"Locked: %zu / %zu | Blocked: %llu",
                 app->GetLockedCount(), app->GetTotalCount(),
                 app->m_statistics.keystrokesBlocked.load());
      AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status);
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IDM_LOCK_ALL, L"Lock All Keyboards");
      AppendMenuW(menu, MF_STRING, IDM_UNLOCK_ALL, L"Unlock All Keyboards");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IDM_SELECT, L"Select Keyboards...");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About KeyFreeze");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

      SetForegroundWindow(hwnd);
      TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0,
                     hwnd, nullptr);
      DestroyMenu(menu);
    }
    return 0;

  case WM_COMMAND:
    if (!app)
      return 0;
    switch (LOWORD(wp)) {
    case IDM_LOCK_ALL:
      app->LockAll();
      break;
    case IDM_UNLOCK_ALL:
      app->UnlockAll();
      break;
    case IDM_SELECT:
      app->ShowKeyboardDialog();
      break;
    case IDM_ABOUT:
      MessageBoxW(hwnd,
                  L"KeyFreeze - Per-Keyboard Input Locking Utility\n\n"
                  L"Selectively lock individual keyboards while allowing\n"
                  L"others to function normally.\n\n"
                  L"Right-click the system tray icon for options.",
                  L"About KeyFreeze", MB_OK | MB_ICONINFORMATION);
      break;
    case IDM_EXIT:
      DestroyWindow(hwnd);
      break;
    }
    return 0;

  case WM_DEVICECHANGE:
    if (app) {
      if (wp == DBT_DEVICEARRIVAL)
        app->HandleDeviceArrival();
      else if (wp == DBT_DEVICEREMOVECOMPLETE)
        app->HandleDeviceRemoval();
    }
    return TRUE;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ----------------------------------------------------------------------------
// Keyboard Hook Procedure
// ----------------------------------------------------------------------------
LRESULT CALLBACK KeyFreezeApp::KeyboardHookProc(int code, WPARAM wp,
                                                LPARAM lp) {
  if (code != HC_ACTION)
    return CallNextHookEx(nullptr, code, wp, lp);

  auto *app = Instance();
  if (!app)
    return CallNextHookEx(nullptr, code, wp, lp);

  auto *ks = reinterpret_cast<KBDLLHOOKSTRUCT *>(lp);

  // Always allow Ctrl+Alt+Del
  if (ks->vkCode == VK_DELETE && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
      (GetAsyncKeyState(VK_MENU) & 0x8000))
    return CallNextHookEx(nullptr, code, wp, lp);

  if (app->ShouldBlockCurrentInput()) {
    app->RecordBlockedKeystroke();
    return 1;
  }

  return CallNextHookEx(nullptr, code, wp, lp);
}

// ----------------------------------------------------------------------------
// Keyboard Selection Dialog Procedure
// ----------------------------------------------------------------------------
INT_PTR CALLBACK KeyFreezeApp::KeyboardDialogProc(HWND dlg, UINT msg, WPARAM wp,
                                                  LPARAM lp) {
  static KeyFreezeApp *app = nullptr;

  switch (msg) {
  case WM_INITDIALOG:
    app = reinterpret_cast<KeyFreezeApp *>(lp);
    {
      HWND list = GetDlgItem(dlg, IDC_KEYBOARD_LIST);
      auto devs = app->GetDevices();

      for (size_t i = 0; i < devs.size(); ++i) {
        wchar_t item[256];
        swprintf_s(item, L"%s%s",
                   devs[i].isConnected ? L"" : L"[DISCONNECTED] ",
                   devs[i].name.c_str());
        int idx = ListBox_AddString(list, item);
        ListBox_SetItemData(list, idx, static_cast<LPARAM>(i));
        if (devs[i].isLocked)
          ListBox_SetSel(list, TRUE, idx);
      }

      if (devs.empty())
        ListBox_AddString(list, L"No keyboards detected");

      HWND stats = GetDlgItem(dlg, IDC_STATS_TEXT);
      wchar_t txt[256];
      swprintf_s(txt,
                 L"Statistics:\n"
                 L"  Lock events: %llu\n"
                 L"  Unlock events: %llu\n"
                 L"  Keystrokes blocked: %llu",
                 app->m_statistics.lockEvents.load(),
                 app->m_statistics.unlockEvents.load(),
                 app->m_statistics.keystrokesBlocked.load());
      SetWindowTextW(stats, txt);
    }
    return TRUE;

  case WM_COMMAND:
    switch (LOWORD(wp)) {
    case IDOK: {
      HWND list = GetDlgItem(dlg, IDC_KEYBOARD_LIST);
      int cnt = ListBox_GetCount(list);
      auto devs = app->GetDevices();

      for (int i = 0; i < cnt; ++i) {
        LRESULT data = ListBox_GetItemData(list, i);
        bool sel = (ListBox_GetSel(list, i) > 0);
        if (data >= 0 && static_cast<size_t>(data) < devs.size()) {
          bool cur = app->IsDeviceLocked(devs[data].path);
          if (sel != cur)
            app->ToggleDevice(devs[data].path);
        }
      }
      app->UpdateTrayIcon();
    }
      EndDialog(dlg, IDOK);
      return TRUE;

    case IDCANCEL:
      EndDialog(dlg, IDCANCEL);
      return TRUE;

    case IDC_SELECT_ALL: {
      HWND list = GetDlgItem(dlg, IDC_KEYBOARD_LIST);
      int cnt = ListBox_GetCount(list);
      for (int i = 0; i < cnt; ++i)
        ListBox_SetSel(list, TRUE, i);
    }
      return TRUE;

    case IDC_DESELECT_ALL: {
      HWND list = GetDlgItem(dlg, IDC_KEYBOARD_LIST);
      int cnt = ListBox_GetCount(list);
      for (int i = 0; i < cnt; ++i)
        ListBox_SetSel(list, FALSE, i);
    }
      return TRUE;
    }
    break;
  }

  return FALSE;
}

// ----------------------------------------------------------------------------
// Entry Point
// ----------------------------------------------------------------------------
int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE previous,
                      _In_ LPWSTR cmdLine, _In_ int showCmd) {
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(cmdLine);
  UNREFERENCED_PARAMETER(showCmd);

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  auto app = std::make_unique<KeyFreezeApp>(instance);
  if (!app->Initialize())
    return 1;
  return app->Run();
}