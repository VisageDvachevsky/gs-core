# 🎯 Design Decisions & Rationale

---

## 1. Language Choices

### ✅ C++20 for Streaming Core

**Why**: DXGI (GPU capture) and AMF (hardware encoder) are C/C++ native APIs. Every language layer adds 3–10ms latency from data copying.

**Alternatives considered**:
- Rust: Would need FFI for DXGI/AMF anyway, adds complexity
- C#: GC pauses can cause frame drops in critical section
- Go: No native Windows GPU API access

---

### ✅ Python 3.11+ for Signal Server

**Why**: Signal server processes ~100 packets/sec (not a bottleneck). FastAPI async is minimal overhead. Redis integration seamless. Easy deployment.

**Alternatives**: Go/Node.js have no advantage, only familiarity.

---

### ✅ React + TypeScript for Web Client

**Why**: WebRTC browser API is weakly typed. TypeScript provides safety. React ecosystem is large.

---

## 2. Video Streaming

### ✅ WebRTC > RTMP/HLS

| Protocol | Latency | Complexity | NAT Traversal |
|----------|---------|-----------|---|
| **WebRTC** | <2 sec | Moderate | ✅ Built-in ICE |
| **RTMP** | 30–45 sec | Low | ❌ Requires relay |
| **HLS** | 20–60 sec | Low | ❌ HTTP only |

**Conclusion**: WebRTC is the only viable choice for gameplay with feedback.

---

### ✅ H.264 Baseline > H.265/AV1

| Codec | Compression | Decode Latency | Browser Support |
|-------|------------|-----------------|---|
| **H.264** | Good | ⭐ Low | ✅ Everywhere |
| **H.265** | 30% better | Higher | ⚠️ Safari/Edge only |
| **AV1** | Best | High | ⚠️ Chrome/Firefox only |

**Conclusion**: H.264 baseline = maximum compatibility + minimum decode latency.

---

### ✅ AMD AMF > NVIDIA NVENC / Intel QSV

Your GPU is **RX 6700 XT** with **VCN 3.0** hardware encoder.

| Encoder | Frame Latency | Your GPU |
|---------|---|---|
| **AMF** | 1 frame | ✅ RX 6700 XT |
| **NVENC** | 2–3 frames | ❌ N/A |
| **QSV** | 2–3 frames | ❌ N/A |

**Decision**: AMF for PHASE 1–4. Add NVIDIA/Intel abstraction on PHASE 6+.

---

## 3. Video Capture

### ✅ DXGI Output Duplication > BitBlt/GDI

| Method | Latency | CPU | Works in Fullscreen |
|--------|---------|-----|---|
| **DXGI Dup** | <2 ms | ⭐ Low | ❌ Borderless only |
| **BitBlt** | 50+ ms | ⭐⭐⭐ High | ✅ Any mode |
| **GDI** | 100+ ms | 💀 Very High | ✅ Any mode |

**Constraint**: DXGI only works in windowed mode (Windows limitation, not fixable).

---

### ✅ Borderless Windowed (NOT Fullscreen Exclusive)

**Why**: DXGI DuplicateOutput has no access to Fullscreen Exclusive surfaces. Borderless = normal window without frame = DXGI can capture.

**Impact**: ~15% performance hit, but Death Stranding still >144 FPS at 1080p.

---

## 4. Input Handling

### ✅ Dual DataChannel + SendInput()

| Layer | Purpose | Delivery Guarantees |
|-------|---------|---|
| **`input.fast`** | Relative mouse move (`movementX/Y`) | Unordered + Unreliable |
| **`input.reliable`** | Keyboard, mouse buttons, wheel, `RELEASE_ALL` | Ordered + Reliable |
| **SendInput()** | Windows input injection | <0.1 ms API call |

**Why this split**: Mouse move is high-frequency and self-correcting, but key/button events must not be dropped.
**For MVP**: Death Stranding has no anti-cheat. Acceptable.
**Future**: PHASE 6+ consider alternatives for anti-cheat-restricted games.

---

## 5. WebRTC Transport

### ✅ libwebrtc Native > Pion/Go Wrapper

- **libwebrtc native C++**: Zero-copy RTP payload
- **Pion/Go wrapper**: Requires marshaling between Go↔C, adds 15–20ms

**Trade-off**: libwebrtc is complex to build (M125+ sources = 1 GB).

---

### ✅ WebSocket for ICE Signaling (not REST polling)

- **WebSocket**: Bidirectional, <50ms setup
- **REST polling**: Every 100ms = excessive setup latency

---

### ✅ TURN Server (coturn) as fallback

- **STUN**: Works for most NAT (determine external IP)
- **TURN**: Only needed ~5% (symmetric NAT). Adds ~15ms. Acceptable.

---

## 6. Authentication

### ✅ JWT + Redis

- **Tokens**: Stateless, can share via URL
- **Redis**: Fast session lookup, easy black-listing
- **Scope**: Single friend MVP, no OAuth/Twitch yet

---

## 7. Known Risks & Mitigation

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|-----------|
| libwebrtc fails to compile | 🟡 Medium | High | Use prebuilt binaries |
| AMD drivers old (< 23.12) | 🔴 High | High | **Update before PHASE 1** |
| DXGI doesn't work in Fullscreen | 🟢 Low | High | Use Borderless (documented) |
| Firewall blocks UDP/STUN | 🟡 Medium | High | QoS routing + docs |
| WebRTC fails to connect (NAT) | 🟡 Medium | Medium | Verify TURN server works |
| SendInput() blocked by AC | 🟢 Low | High | Death Stranding OK |
| Latency >150ms over internet | 🟡 Medium | Medium | Expected (ISP/distance), optimize later |

---

## ✅ Architecture is Locked

All decisions above are FIXED until PHASE 5. No major pivots will occur mid-development.

Changes allowed:
- Optimization within each component
- Bug fixes
- Additional features after PHASE 5

Changes NOT allowed until PHASE 6+:
- Switching from C++ to another language (core)
- Switching from AMF to other encoders
- Switching from WebRTC to RTMP
