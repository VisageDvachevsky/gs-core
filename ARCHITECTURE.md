# 🧠 GameStream — Архитектура системы

**Status**: Development (PHASE 4: Input Pipeline)
**Target**: Death Stranding @ 1080p60 WebRTC with <80ms latency

---

## 📐 System Architecture

### Component Stack

```
CLIENT (Browser)
├─ React + TypeScript UI
├─ WebRTC Peer Connection (P2P + TURN)
├─ Video Decoding (H.264)
└─ Gamepad/Mouse Input (DataChannel)
        ↕️ WebRTC (UDP, +25–45ms internet latency)
SERVER (Your PC)
├─ Streaming Core (C++20)
│  ├─ WGC / DXGI Capture (GPU capture <2ms)
│  ├─ AMD AMF H.264 Encoder (5–8ms encode)
│  ├─ libwebrtc (RTP transport)
│  └─ SendInput() / Raw Input (control injection)
├─ Signal Server (Python FastAPI)
│  ├─ REST API (/session/start, /session/stop)
│  ├─ WebSocket (ICE candidates, SDP)
│  ├─ Redis (session management)
│  └─ TURN Server (coturn, fallback relay)
└─ Monitoring (Prometheus)
```

### Critical Path Analysis

**Video latency (capture → display)**:
- WGC/DXGI Poll: 0.5–1 ms (GPU operation)
- AMF Encode: 5–8 ms (H.264 VCN 3.0)
- RTP Packetization: 0.1 ms
- Network: 25–45 ms (Moscow↔Europe distance)
- Decode: 5–10 ms (browser H.264)
- Display: 16.6 ms @ 60 FPS
- **Total: ~55–85 ms** ✅

**Input echo latency (click → reaction)**:
- Browser event: 1–2 ms
- DataChannel send: 0.1 ms
- Network: 25–45 ms
- Core parse + SendInput: 0.2 ms
- Game process 1 frame: 16.6 ms
- Return video path: 55–85 ms
- **Total: ~100–150 ms** (acceptable for DS)

---

## 🛠 Tech Stack (LOCKED until PHASE 5)

### Core Streaming (C++20)

| Component | Technology | Rationale |
|-----------|-----------|-----------|
| Video Capture | WGC / DXGI | WGC for windows, DXGI as fallback. <2ms latency |
| GPU Encoder | AMD AMF H.264 | VCN 3.0: 1-frame latency vs NVENC (2–3) |
| WebRTC | libwebrtc native C++ | Zero-copy RTP payload |
| Input | SendInput() + Raw Input | <0.1ms Windows API |
| Codec | H.264 baseline | Max browser compatibility, fast decode |

### Signal Server (Python 3.11+)

- **FastAPI**: Async REST + WebSocket
- **Redis**: Session management, black-list
- **coturn**: STUN/TURN for NAT traversal
- **Prometheus**: Metrics collection

### Client (React + TypeScript)

- **Video**: <video> tag with WebRTC stream
- **Input**: Gamepad API + Mouse listener
- **Signaling**: WebSocket to FastAPI

---

## 📋 Development Phases

| Phase | Goal | Time | Status |
|-------|------|------|--------|
| 1 | Local DXGI + AMF capture PoC | 1–2 weeks | ✅ Done |
| 2 | WebRTC integration (localhost) | 1–2 weeks | ✅ Done |
| 3 | Signaling server (internet ready) | 3–5 days | ✅ Done |
| 4 | Input pipeline (DataChannel + SendInput) | 3–5 days | 🟡 In Progress |
| 5 | Web client UI + optimization + monitoring | 2+ weeks | 📋 Later |

---

## ⚠️ Critical Constraints

| Constraint | Impact | Mitigation |
|-----------|--------|-----------|
| DXGI only works in Windowed mode | Must use Borderless (perf -15%) | WGC helps with per-window capture |
| libwebrtc large compilation (1+ GB) | Build time 20–40 min | Cache artifacts in CI |
| AMF only on AMD GPU | Future: add NVIDIA/Intel layer | PHASE 6+ abstraction |
| SendInput() blocked by some AC | Death Stranding OK | Accept for MVP |
| Network latency 25–45ms baseline | Can't improve (physics) | Accept as limit |

---

## 🎯 Success Metrics

- Frame latency: <80ms (internet) ✅
- FPS stability: 50–60 steady, no drops
- Input echo: <150ms
- Bitrate: 15–20 Mbps @ 1080p60 CBR
- CPU core usage: <30% (streaming_core.exe)
- GPU usage: <70% (RX 6700 XT)

---

**Next**: Read DESIGN_DECISIONS.md for rationale
