# ✅ Project Checklist — Architecture Approval

**Purpose**: Confirm all architectural decisions before development starts
**Date**: 2026-02-24
**Status**: In Development (Phase 4 complete, Phase 5 planning)

---

## 🎯 Core Decisions (Sign-Off Required)

### Tech Stack

- [x] **C++20** for streaming core (DXGI + AMF + libwebrtc native)
  - Minimizes inter-language data copying latency
  - Trade-off: More complex development

- [x] **Python 3.11** for signal server (FastAPI + WebSocket + Redis)
  - Simple, async, easy deployment
  - Trade-off: Not as fast as compiled languages (but irrelevant for signal server)

- [x] **React + TypeScript** for web client
  - Type-safe WebRTC API usage
  - Trade-off: Bundle size

### Video Streaming

- [x] **WebRTC** (not RTMP/HLS)
  - <2 sec latency + built-in NAT traversal
  - Limitation: Doesn't scale >1000 concurrent users (P2P architecture)

- [x] **H.264 Baseline Profile** (not H.265/AV1)
  - Maximum browser compatibility
  - Limitation: Uses more bandwidth than H.265 (but acceptable for 1080p60 @ 20 Mbps)

- [x] **AMD AMF Encoder** (RX 6700 XT VCN 3.0)
  - 1-frame latency (optimal for AMD)
  - Limitation: Only works on AMD GPUs (PHASE 6 = other GPUs)

### Capture & Display

- [x] **DXGI Output Duplication** for screen capture
  - <2 ms latency
  - Limitation: Only works in Borderless Windowed (Windows API limitation)

- [x] **WGC (Windows Graphics Capture)** for per-window capture
  - Modern API, supports independent window capture

- [x] **Borderless Windowed Mode** (NOT Fullscreen Exclusive)
  - Enables DXGI capture
  - Impact: ~15% performance reduction (Death Stranding still >144 FPS)

- [x] **Dual DataChannel input pipeline + SendInput()** for input
  - `input.fast` (unordered/unreliable) for relative mouse move
  - `input.reliable` (ordered/reliable) for keyboard/buttons/wheel/RELEASE_ALL
  - Limitation: Some anti-cheat games block SendInput (Death Stranding doesn't)

### WebRTC & Signaling

- [x] **libwebrtc native C++** (not Pion/Go wrapper)
  - Zero-copy RTP payload
  - Trade-off: Complex build (1+ GB, 20–40 min)

- [x] **WebSocket for ICE signaling** (not REST polling)
  - Faster connection setup
  - Standard for WebRTC

- [x] **TURN server (coturn)** for NAT fallback
  - Adds ~15 ms latency when needed
  - Trade-off: Extra infrastructure to maintain

---

## 📊 Confirmation Questions

**Q1: Do you agree with C++20 for the core?**
- [x] Yes
- [ ] No → Specify alternative and rationale

**Q2: Is Borderless Windowed acceptable for the game?**
- [x] Yes (Death Stranding supports it)
- [ ] No → Problem?

**Q3: Do you want multi-GPU support (NVIDIA/Intel) in MVP?**
- [x] No (AMF only, add on PHASE 6+)
- [ ] Yes → Will delay PHASE 1 startup

**Q4: Is <80ms internet latency acceptable?**
- [x] Yes (realistic for Moscow↔Europe)
- [ ] No → Requires server relocation (expensive)

**Q5: Do you accept that latency cannot be optimized below ~40ms without ISP changes?**
- [x] Yes (speed of light in fiber = 25–45 ms Moscow↔Europe)
- [ ] No → Problem?

---

## 🚨 Known Constraints & Acceptances

| Constraint | Accepted |
|-----------|----------|
| DXGI doesn't work in Fullscreen | [x] Use Borderless Windowed |
| libwebrtc large build | [x] Cache artifacts, accept build time |
| AMF only on AMD | [x] Add other GPUs on PHASE 6 |
| SendInput() may be blocked by AC | [x] OK (Death Stranding no AC) |
| Internet latency 25–45 ms baseline | [x] Accept as limit |
| No scaling >100 concurrent users | [x] OK for MVP (1 friend) |

---

## ✅ Pre-Development Conditions

**PHASE 1 can START if ALL are true:**

- [x] AMD Radeon Driver 23.12+ installed
- [x] Windows SDK 22621 installed
- [x] Visual Studio 2022 C++ tools installed
- [x] CMake 3.20+ installed
- [x] libwebrtc M125+ sources/prebuilt obtained
- [x] AMF SDK 1.4.29+ downloaded
- [x] You understand ARCHITECTURE.md
- [x] You understand DESIGN_DECISIONS.md
- [x] You understand REQUIREMENTS.md

**If ANY condition is false**: Go back to REQUIREMENTS.md and install/download.

---

## 📋 Risk Assessment

| Risk | Probability | Severity | Mitigation |
|------|------------|----------|-----------|
| Drivers too old | 🔴 High | 🔴 High | Check & update 23.12+ |
| libwebrtc build fails | 🟡 Medium | 🔴 High | Use prebuilt or CI artifacts |
| DXGI integration complex | 🟡 Medium | 🟡 Medium | Study examples, iterate |
| Latency expectations unmet | 🟡 Medium | 🟡 Medium | Educate user about ISP limits |
| Fullscreen mode issues | 🟢 Low | 🔴 High | Document Borderless requirement |

---

## 🎯 Success Criteria for Each Phase

**PHASE 1**: DXGI captures @ 60 FPS, H.264 file output, <5ms latency ✅
**PHASE 2**: WebRTC P2P works locally, <10ms e2e latency ✅
**PHASE 3**: Signal server handles multiple clients, internet-ready ✅ Complete
**PHASE 4**: Friend can play via browser, dual-channel input responds ✅ Complete
**PHASE 5**: Consistent <80ms echo latency over real internet 📋

---

## 🚀 Architecture Status

```
Architecture Planning:  ✅ COMPLETE
Tech Stack Locked:      ✅ COMPLETE
Risk Assessment:        ✅ COMPLETE
Dependencies Listed:    ✅ COMPLETE
Pre-conditions Set:     ✅ COMPLETE

Ready for PHASE 1?      → ✅ STARTED
```

---

## 📝 Sign-Off

Developer: (You)
Date: 2026-02-24
Approval: Architecture approved and implemented through Phase 4 completion

---

See ARCHITECTURE.md for full technical details.
