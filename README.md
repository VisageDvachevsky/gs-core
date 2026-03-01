# 🎮 GameStream — Game Streaming Over WebRTC

**Status**: Development (Phase 4 complete, Phase 5 planning)
**Target Launch**: PHASE 5 (Mar 2026)
**Technology Stack**: C++20 (core), Python (server), React (client)

---

## 📖 Что это?

GameStream — это **низко-задержечный стриминг игр** для пользователя через интернет.

**Принцип работы**:
1. Ты играешь в Death Stranding на своем PC (Borderless Window)
2. Твой PC захватывает экран через DXGI или WGC, кодирует через AMF H.264
3. Видео отправляется через WebRTC (P2P + TURN для NAT)
4. Пользователь открывает браузер, видит видео
5. Ввод (мышь, клавиатура, скролл; геймпад — TODO) отправляется обратно через DataChannel

**Целевая задержка**: <80 мс (интернет Москва → Европа)

---

## 🚀 Быстрый старт

### Прежде всего: прочитай архитектуру

```bash
# Три обязательных документа:
1. ARCHITECTURE.md       — Полная архитектурная схема (15 мин чтения)
2. DESIGN_DECISIONS.md   — Почему каждое решение (10 мин чтения)
3. REQUIREMENTS.md       — Что установить перед началом (5 мин чтения)

# Опционально:
4. PROJECT_CHECKLIST.md  — Чеклист согласования (5 мин чтения)
```

---

## 🎯 План разработки (5 фаз)

| Фаза | Цель | Время | Статус |
|------|------|-------|--------|
| **1** | Локальный PoC: DXGI + AMF захват | 1–2 неделя | ✅ Completed |
| **2** | WebRTC локально: видео через localhost | 1–2 неделя | ✅ Completed |
| **3** | Сигнальный сервер: интернет-подключение | 3–5 дней | ✅ Completed |
| **4** | Ввод: Dual DataChannel + SendInput | 3–5 дней | ✅ Completed |
| **5** | Веб-клиент и оптимизация: production-ready | Текущая | 📋 Planning |

**Статус разработки**: Фаза 4 закрыта (мышь/клавиатура/скролл, dual-channel input) ✅
**Следующий шаг**: Фаза 5 — интеграционный браузерный UX и e2e проверка игры через интернет.

---

## 📦 Структура репозитория

```
gamestream/
├── README.md                    ← ТЫ ЗДЕСЬ
├── ARCHITECTURE.md              ← Прочитай это первым
├── DESIGN_DECISIONS.md          ← Потом это
├── REQUIREMENTS.md              ← И это
├── PROJECT_CHECKLIST.md         ← И это
│
├── core/                        (C++20 ядро)
│   ├── src/
│   │   ├── main.cpp
│   │   ├── capture/             (DXGI & WGC capture)
│   │   ├── codec/               (AMF Encoder)
│   │   └── webrtc/              (PeerConnection, Host)
│   ├── include/                 (Public headers)
│   └── tools/                   (Test utilities)
│   └── CMakeLists.txt
│
├── server/                      (Python сигнальный сервер)
│   ├── main.py
│   ├── signaling/
│   ├── auth/
│   ├── metrics/
│   └── pyproject.toml
│
├── client/                      (React веб-клиент)
│   ├── src/
│   │   ├── components/
│   │   ├── pages/
│   │   └── services/
│   └── package.json
│
├── deploy/                      (Docker, systemd, конфиги)
│   ├── docker/
│   ├── systemd/
│   └── coturn/
│
└── docs/
    ├── SETUP.md                 (пошаговая инструкция)
    ├── DEPLOYMENT.md            (как deploy на VPS)
    ├── TROUBLESHOOTING.md       (что делать если сломалось)
    └── PERFORMANCE.md           (профилирование и оптимизация)
```

---

## 💡 Ключевые технические решения

### Почему C++20 для ядра?
- **DXGI / WGC API** → только C++
- **AMF кодирование** → нативный C API
- **libwebrtc** → C++ API
- **Результат**: Каждый слой копирования = +3–10 мс задержки

### Почему AMF, а не NVIDIA/Intel?
Моя **RX 6700 XT** имеет VCN 3.0 (Video Coding Engine 3.0), что дает **1-кадровую задержку** вместо 2–3 кадров у NVENC.

### Почему WebRTC, а не RTMP?
WebRTC: <2 сек задержка, встроенное NAT traversal
RTMP: 30–45 сек задержка, требует TCP relay

### Почему Borderless Windowed, а не Fullscreen Exclusive?
DXGI DuplicateOutput не работает в Fullscreen Exclusive — это ограничение Windows. Borderless Windowed = решение.

---

## ✅ Чеклист перед началом разработки

**ОС и драйверы**:
- [x] Windows 11 Pro
- [x] AMD Radeon Driver 23.12+
- [x] Windows SDK 22621

**Инструменты разработки**:
- [x] Visual Studio 2022 (Desktop C++)
- [x] CMake 3.20+
- [x] Git

**Зависимости** (позже):
- [x] libwebrtc M125+
- [x] AMF SDK 1.4.29+
- [ ] Python 3.11 + Poetry
- [ ] Node.js 18+

---

## 📞 Контакты и поддержка

**Проблемы при установке?**
1. Проверь REQUIREMENTS.md
2. Поищи в DESIGN_DECISIONS.md ответы на вопросы
3. Создай Issue в GitHub с полным логом

---

**Status**: 📋 PHASE 5 (planned)
**Last Updated**: 2026-03-01
