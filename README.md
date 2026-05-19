# KeyFreeze

**Professional Per-Keyboard Input Locking Utility for Windows**

![License](https://img.shields.io/badge/license-Zlib-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%207%2B-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange.svg)

> **Selectively lock individual keyboards while keeping others fully functional**

KeyFreeze is a lightweight Windows utility that allows you to disable specific keyboards without affecting others connected to your system. Perfect for laptop users who want to use an external keyboard while preventing accidental input from their built-in keyboard.

---

## ✨ Features

- 🎯 **Per-Keyboard Control** - Lock individual keyboards independently
- 🔄 **Hot-Plug Support** - Automatically detects newly connected keyboards
- 💾 **Persistent Configuration** - Lock states survive application restarts
- 📊 **Input Statistics** - Track blocked keystrokes and lock events
- 🔒 **Security First** - Ctrl+Alt+Del always works, even on locked keyboards
- ⚡ **Lightweight** - Minimal CPU and memory usage
- 🪟 **System Tray Integration** - Quick access from the notification area

---

## 🎬 Quick Demo

```
┌─────────────────────────────────────┐
│  KeyFreeze - System Tray            │
├─────────────────────────────────────┤
│  Locked: 1/3 | Blocked: 247 keys    │
│                                     │
│  ✓ Lock All Keyboards               │
│  ○ Unlock All Keyboards             │
│  ⚙ Select Keyboards...              │
│  ℹ About KeyFreeze                  │
│  ✕ Exit                             │
└─────────────────────────────────────┘
```

**Left-click** tray icon → Toggle all keyboards  
**Right-click** tray icon → Access menu

---

## 🚀 Getting Started

### Prerequisites

- **Windows 7 or later** (Windows 10/11 recommended)
- **Administrator privileges** (required for keyboard interception)

### Installation

#### Option 1: Download Pre-built Binary

1. Download the latest release from [Releases](https://github.com/MrRogueKnight/KeyFreeze/releases)
2. Extract `KeyFreeze.exe` to any folder
3. **Right-click** `KeyFreeze.exe` → **Run as administrator**

#### Option 2: Build from Source

**Requirements:**
- Visual Studio 2019/2022 (with C++ Desktop Development workload)
- Windows SDK

**Build Steps:**

```batch
# Clone the repository
git clone https://github.com/MrRogueKnight/KeyFreeze.git
cd KeyFreeze

# Compile resource file
rc resource.rc

# Build the application
cl /EHsc /O2 /W4 /Fe:KeyFreeze.exe KeyFreeze.cpp resource.res ^
   user32.lib shell32.lib comctl32.lib setupapi.lib hid.lib advapi32.lib
```

**Or use Visual Studio:**
1. Open `KeyFreeze.sln`
2. Select **Release** configuration
3. Build → Build Solution (Ctrl+Shift+B)

---

## 📖 Usage Guide

### Basic Operations

1. **Start KeyFreeze** (as administrator)
2. **Right-click** the tray icon to open the menu
3. **Select "Select Keyboards..."** to choose which keyboards to lock
4. **Check** the keyboards you want to lock
5. Click **OK**

### Quick Actions

| Action | How To |
|--------|--------|
| Toggle all keyboards | **Left-click** tray icon |
| Lock all keyboards | Right-click → "Lock All Keyboards" |
| Unlock all keyboards | Right-click → "Unlock All Keyboards" |
| Select specific keyboards | Right-click → "Select Keyboards..." |
| View statistics | Open "Select Keyboards" dialog |
| Exit application | Right-click → "Exit" |

### Common Scenarios

#### 🖥️ Laptop + External Keyboard Setup

**Problem:** You want to use an external keyboard but keep accidentally hitting your laptop's built-in keyboard.

**Solution:**
1. Connect your external keyboard
2. Open KeyFreeze keyboard selection
3. Lock **only** the built-in laptop keyboard
4. Your external keyboard remains fully functional

#### 🎮 Gaming Setup

**Problem:** You have multiple keyboards and want to prevent input from specific ones during gameplay.

**Solution:**
1. Identify which keyboard(s) to disable in the selection dialog
2. Lock them before starting your game
3. Toggle off after gaming session

#### 🧹 Keyboard Cleaning

**Problem:** Need to clean your keyboard without triggering random inputs.

**Solution:**
1. Lock the keyboard you're about to clean
2. Clean safely without random keypresses
3. Unlock when done

---

## 🛠️ Configuration

### Configuration File Location

```
%APPDATA%\KeyFreeze\KeyFreeze.config
```

**Example:** `C:\Users\YourName\AppData\Roaming\KeyFreeze\KeyFreeze.config`

### Configuration Format

The configuration file stores locked keyboard device paths:

```
# KeyFreeze Configuration File
# This file stores the list of locked keyboard device paths

\\?\HID#VID_046D&PID_C52B#7&1234abcd&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}
\\?\HID#VID_04D9&PID_1203#8&5678efgh&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}
```

> **Note:** This file is automatically managed by KeyFreeze. Manual editing is not recommended.

---

## 🔒 Security Considerations

### What KeyFreeze Does

- Intercepts keyboard input using Windows API hooks
- Blocks input from specific keyboards based on device path
- Always allows Ctrl+Alt+Del (Windows security requirement)
- Runs only when you start it (no automatic startup)

### What KeyFreeze Does NOT Do

- Record or log your keystrokes
- Send data over the network
-  Access the internet
- Store sensitive information
- Install kernel drivers

### Privacy

KeyFreeze is **100% local** and **100% private**:
- No telemetry or analytics
- No cloud connectivity
- Configuration stored only on your PC
- Open source - verify the code yourself

---

## 🤔 Troubleshooting

### "Failed to install keyboard hook"

**Cause:** Application doesn't have administrator privileges.

**Solution:** Right-click `KeyFreeze.exe` → "Run as administrator"

### "KeyFreeze is already running"

**Cause:** Another instance is already active.

**Solution:** Check your system tray for the KeyFreeze icon. Right-click → Exit to close it.

### Keyboard not detected

**Possible causes:**
- Keyboard connected after KeyFreeze started
- USB receiver not properly initialized

**Solutions:**
1. Disconnect and reconnect the keyboard
2. Wait a few seconds for Windows to detect it
3. KeyFreeze automatically refreshes every 2 seconds

### Locked keyboard still works

**Possible causes:**
- Application not running as administrator
- Keyboard uses special drivers bypassing Windows hooks
- Wireless keyboard with hardware-level input

**Solutions:**
1. Verify KeyFreeze is running with admin rights
2. Check the tray icon status
3. Try locking/unlocking the specific keyboard again

### Configuration not persisting

**Cause:** Permission issue writing to %APPDATA% folder.

**Solution:**
1. Check that `%APPDATA%\KeyFreeze\` folder exists and is writable
2. Run KeyFreeze as administrator
3. Verify no antivirus is blocking file writes

---

## 📊 Technical Details

### Architecture

KeyFreeze uses a two-phase input detection system:

1. **WM_INPUT (Raw Input)** - Identifies which keyboard sent input
2. **WH_KEYBOARD_LL (Low-Level Hook)** - Blocks input from locked keyboards

```
┌──────────────┐         ┌─────────────────┐
│  Keyboard 1  │────────▶│   Raw Input     │
│ (Unlocked)   │         │  (WM_INPUT)     │
└──────────────┘         └─────────────────┘
                                │
┌──────────────┐                │ Identifies
│  Keyboard 2  │────────▶───────┤ source device
│  (Locked)    │                │
└──────────────┘                ▼
                         ┌─────────────────┐
                         │ Keyboard Hook   │
                         │ (WH_KEYBOARD_LL)│
                         └─────────────────┘
                                │
                         ┌──────┴──────┐
                         │             │
                    Allow        Block & Count
```

### System Requirements

| Component | Requirement |
|-----------|-------------|
| **OS** | Windows 7 SP1 or later |
| **RAM** | < 10 MB |
| **CPU** | < 0.1% average usage |
| **Disk** | < 1 MB |
| **Privileges** | Administrator |

### Compatibility

| Feature | Windows 7 | Windows 8/8.1 | Windows 10 | Windows 11 |
|---------|-----------|---------------|------------|------------|
| Per-keyboard locking | ✅ | ✅ | ✅ | ✅ |
| Hot-plug detection | ✅ | ✅ | ✅ | ✅ |
| Configuration persistence | ✅ | ✅ | ✅ | ✅ |
| System tray integration | ✅ | ✅ | ✅ | ✅ |

---

## 🧪 Known Limitations

1. **Wireless Keyboards with Multiple Receivers**
   - Some wireless keyboards appear as a single device even if you have multiple physical keyboards
   - Locking one may affect others using the same receiver model

2. **Special Gaming Keyboards**
   - Keyboards with custom drivers (e.g., Razer, Corsair iCUE) may bypass standard Windows input
   - Most gaming keyboards work fine, but some may require special handling

3. **Bluetooth Keyboards**
   - Bluetooth keyboards are fully supported
   - May take 2-3 seconds to detect on connection due to Bluetooth pairing time

4. **Virtual Machines**
   - KeyFreeze works on the host OS
   - Does not affect keyboard input inside virtual machines

---

## 📄 License

This project is licensed under the **Zlib License** - see the [LICENSE](LICENSE) file for details.

```
Copyright (c) 2024 KeyFreeze Project

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
```

---

## 🙏 Acknowledgments

- **Windows Raw Input API** - For reliable per-device input identification
- **Windows Hooks API** - For low-level keyboard interception
- **Community testers** - For helping identify and fix edge cases

---
