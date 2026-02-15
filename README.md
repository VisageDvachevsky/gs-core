# 🎮 GameStream — Game Streaming Over WebRTC

**Status**: Pre-Development (Architecture Planning)
**Target Launch**: PHASE 1 (Feb 2026)
**Technology Stack**: C++20 (core), Python (server), React (client)

---

## 📖 Что это?

GameStream — это **низко-задержечный стриминг игр** для твоей подруги через интернет.

**Принцип работы**:
1. Ты играешь в Death Stranding на своем PC (Borderless Window)
2. Твой PC захватывает экран через DXGI, кодирует через AMF H.264
3. Видео отправляется через WebRTC (P2P + TURN для NAT)
4. Подруга открывает браузер, видит видео
5. Ввод (мышь, клавиатура, геймпад) отправляется обратно через DataChannel

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
| **1** | Локальный PoC: DXGI + AMF захват | 1–2 неделя | 🟡 Ready to start |
| **2** | WebRTC локально: видео через localhost | 1–2 неделя | 📋 Planning |
| **3** | Сигнальный сервер: интернет-подключение | 3–5 дней | 📋 Planning |
| **4** | Веб-клиент: красивый интерфейс | 3–5 дней | 📋 Planning |
| **5** | Оптимизация и мониторинг: production-ready | Текущая | 📋 Planning |

**Статус разработки**: Фаза 0 (закрепление архитектуры) ✅ Завершена
**Следующий шаг**: Фаза 1 (начать разработку Core в C++20)

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
│   │   ├── capture/dxgi_*.h/cpp
│   │   ├── encode/amf_*.h/cpp
│   │   ├── webrtc/webrtc_peer.h/cpp
│   │   └── input/input_handler.h/cpp
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
- **DXGI API** → только C++ (или C# с P/Invoke overhead)
- **AMF кодирование** → нативный C API
- **libwebrtc** → C++ API
- **Результат**: Каждый слой копирования = +3–10 мс задержки

### Почему AMF, а не NVIDIA/Intel?
Твой **RX 6700 XT** имеет VCN 3.0 (Video Coding Engine 3.0), что дает **1-кадровую задержку** вместо 2–3 кадров у NVENC.

### Почему WebRTC, а не RTMP?
WebRTC: <2 сек задержка, встроенное NAT traversal
RTMP: 30–45 сек задержка, требует TCP relay

### Почему Borderless Windowed, а не Fullscreen Exclusive?
DXGI DuplicateOutput не работает в Fullscreen Exclusive — это ограничение Windows. Borderless Windowed = решение.

---

## ✅ Чеклист перед началом разработки

**ОС и драйверы**:
- [ ] Windows 11 Pro
- [ ] AMD Radeon Driver 23.12+
- [ ] Windows SDK 22621

**Инструменты разработки**:
- [ ] Visual Studio 2022 (Desktop C++)
- [ ] CMake 3.20+
- [ ] Git

**Зависимости** (позже):
- [ ] libwebrtc M125+
- [ ] AMF SDK 1.4.29+
- [ ] Python 3.11 + Poetry
- [ ] Node.js 18+

---

## 📞 Контакты и поддержка

**Проблемы при установке?**
1. Проверь REQUIREMENTS.md
2. Поищи в DESIGN_DECISIONS.md ответы на вопросы
3. Создай Issue в GitHub с полным логом

---

**Status**: 🟢 Ready for PHASE 1
**Last Updated**: 2026-02-15
