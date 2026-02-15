# 📦 Dependencies & Requirements

**Platform**: Windows 11 Pro 10.0.26200+
**GPU**: AMD RX 6700 XT (or compatible Radeon with VCN)
**Target**: PHASE 1 startup

---

## 🖥 System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Windows 10 22H2 | Windows 11 Pro |
| **GPU** | AMD RDNA with VCN | RX 6700 XT or higher |
| **CPU** | i5-10400 / Ryzen 5 3600 | i7-12700K / Ryzen 7 5800X |
| **RAM** | 16 GB | 32 GB |
| **GPU VRAM** | 6 GB | 8+ GB |

---

## 📚 Windows & Drivers (CRITICAL for PHASE 1)

```
Windows SDK:          22621 (matches Windows 11)
                      https://developer.microsoft.com/windows/downloads/windows-sdk

AMD Radeon Drivers:   23.12 or newer (VCN 3.0 support)
                      https://www.amd.com/support

Visual Studio 2022:   Community Edition (Desktop C++ development)
                      Install MSVC v143 + Windows SDK 22621

CMake:                3.20+
Git:                  2.30+
```

**Action**: Install these BEFORE starting PHASE 1.

---

## 🔨 Third-Party Dependencies

### libwebrtc (Google WebRTC)

```bash
# Source URL: https://chromium.googlesource.com/chromium/tools/depot_tools
# Clone depot_tools, then:
fetch webrtc
cd src
gn gen out/Release --args='rtc_use_h264=true is_debug=false'
ninja -C out/Release
# Result: webrtc.lib + headers in include/
```

**Artifact**: ~1.5 GB, 20–40 min build time
**Workaround**: Cache in CI or use prebuilt binaries if available

### AMF SDK (AMD Media Framework)

```bash
# https://github.com/GPUOpen-LibrariesAndSDKs/AMF
# Download and extract → use /include/core and /include/components
```

---

## 🐍 Python (for Server, PHASE 3+)

```bash
python:           3.11+
poetry:           1.7+ (dependency manager)
fastapi:          0.100+
uvicorn:          0.24+
redis:            5.0+ (client)
prometheus-client: 0.18+
structlog:        23.2+ (logging)
```

**Setup**:
```bash
pip install poetry
cd server/
poetry install
```

---

## 🌐 Node.js (for Client, PHASE 4+)

```bash
node:       18+ (or 20 LTS)
npm / pnpm: 9+
react:      18+
typescript: 5.2+
```

**Setup**:
```bash
cd client/
npm install
npm run dev
```

---

## 🔌 Infrastructure (PHASE 3+)

### Redis
```bash
Windows:  https://github.com/microsoftarchive/redis/releases
WSL:      wsl && sudo apt install redis-server
Verify:   redis-cli ping → should return PONG
```

### TURN Server (coturn)
```bash
Linux:    sudo apt install coturn
Windows:  Use WSL or standalone binary
Config:   /etc/coturn/turnserver.conf
```

---

## ✅ Pre-PHASE 1 Checklist

- [ ] Windows 11 Pro (or Windows 10 22H2)
- [ ] AMD Radeon Drivers 23.12+ installed
- [ ] Windows SDK 22621 installed
- [ ] Visual Studio 2022 with C++ tools installed
- [ ] CMake 3.20+ installed
- [ ] Git 2.30+ installed
- [ ] libwebrtc M125+ ready (sources cloned or prebuilt)
- [ ] AMF SDK downloaded and extracted

---

## 🚨 Common Issues & Fixes

**Q: AMF SDK not found**
A: Update AMD drivers to 23.12+. Check that VCN 3.0 is available in RadeonSoftware.

**Q: libwebrtc won't compile**
A: Ensure VS2022 + Windows SDK 22621 installed. Verify depot_tools in PATH.

**Q: DXGI capture not working**
A: Game must be in Borderless Windowed mode, NOT Fullscreen Exclusive.

**Q: CMake can't find Windows SDK**
A: Reinstall Windows SDK 22621. Verify path in Visual Studio Installer.

---

## 📋 Versions Locked

- **Python**: 3.11+
- **C++**: std=c++2a (C++20)
- **Windows SDK**: 22621
- **CMake**: 3.20+
- **libwebrtc**: M125+
- **AMF SDK**: 1.4.29+
- **FastAPI**: 0.100+
- **React**: 18+

Changes to these require architecture review (PHASE 5+).

---

See ARCHITECTURE.md for full system design.
