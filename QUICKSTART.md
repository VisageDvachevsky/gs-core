# ⚡ GameStream — Quick Start (3 мин)

**Что это**: Низко-задержечный стриминг игр через WebRTC (C++20 + Python + React)

**Целевая задержка**: <80 мс (интернет Москва → Европа)

---

## 🎯 Архитектура

```
Подруга (браузер)
    ↕️ WebRTC P2P (+25–45 мс интернет)
Твой PC:
    • Ядро: C++20 (DXGI + AMF H.264 + libwebrtc)
    • Сервер: Python FastAPI + WebSocket + Redis
    • Ввод: SendInput() + Raw Input API
```

---

## 🔧 Tech Stack (LOCKED)

| Компонент | Технология | Почему |
|-----------|-----------|--------|
| **Core** | C++20: DXGI + AMF + libwebrtc | Минимум задержек, нативные API |
| **Server** | Python 3.11: FastAPI + WebSocket | Асинхронный, простой deploy |
| **Client** | React: TypeScript + Gamepad API | WebRTC API слабо типизирован |
| **Codec** | H.264 baseline | Максимум совместимости, низкая задержка |
| **GPU Encoder** | AMD AMF (RX 6700 XT) | 1-frame latency vs NVENC (2–3 frames) |
| **Delivery** | WebRTC P2P + TURN fallback | <2 сек задержка, встроенное NAT traversal |

---

## 📊 Задержки

```
Видео (захват → экран подруги):
  DXGI (0.5 ms) → AMF encode (5–8 ms) → RTP (0.1 ms) →
  Internet (25–45 ms) → Decode (5–10 ms) → Display (16.6 ms)
  ────────────────────────────────────────────────────
  ИТОГО: ~55–85 ms ✅

Input echo (клик → реакция на экране):
  Click (1 ms) → DataChannel (0.1 ms) →
  Internet (25–45 ms) → SendInput (0.1 ms) →
  Game frame (16.6 ms) + video path (55–85 ms)
  ────────────────────────────────────────────────────
  ИТОГО: ~100–150 ms (OK для Death Stranding)
```

---

## 📋 План (5 фаз)

1. **PHASE 1**: Local DXGI + AMF PoC (1–2 нед) 🟡 Ready
2. **PHASE 2**: WebRTC locally (1–2 нед) 📋 Next
3. **PHASE 3**: Signaling server (3–5 дн) 📋 Later
4. **PHASE 4**: Web client (3–5 дн) 📋 Later
5. **PHASE 5**: Optimization + monitoring (2+ нед) 📋 Later

---

## ⚠️ Критические решения

| Решение | Проблема | Решение |
|---------|----------|---------|
| DXGI + Borderless Window | DXGI не работает в Fullscreen | Обязательно Borderless |
| AMF H.264 | только AMD GPU | На PHASE 6+ добавим NVIDIA/Intel |
| SendInput() | Может быть заблокирован anti-cheat | Death Stranding не имеет AC |
| libwebrtc компиляция | Большой (1+ GB) | Кэшируем в CI/CD |

---

## ✅ Чеклист перед началом

- [ ] Windows 11 Pro + AMD Radeon Driver 23.12+
- [ ] Visual Studio 2022 (C++ tools) + CMake 3.20+
- [ ] libwebrtc M125+ + AMF SDK 1.4.29+
- [ ] Python 3.11 + Node.js 18+

---

## 📚 Документы

| Файл | Время | Содержание |
|------|-------|-----------|
| **QUICKSTART.md** | 3 мин | ← ТЫ ЗДЕСЬ |
| **README.md** | 5 мин | Обзор и структура |
| **ARCHITECTURE.md** | 15 мин | Полная система |
| **DESIGN_DECISIONS.md** | 10 мин | Почему каждое решение |
| **REQUIREMENTS.md** | 5 мин | Зависимости и версии |
| **PROJECT_CHECKLIST.md** | 5 мин | Согласование |

---

**Status**: ✅ Архитектура готова
**Следующий шаг**: Читай ARCHITECTURE.md → начни PHASE 1
