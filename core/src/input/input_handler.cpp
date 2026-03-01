#include "input_handler.h"
#include "input_types.h"

#include <windows.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <format>

namespace gamestream {

namespace {

// ---------------------------------------------------------------------------
// Wire-format helpers — read little-endian integers from a byte buffer.
// All reads are bounds-checked by the caller via packet size validation.
// ---------------------------------------------------------------------------

inline int16_t read_i16(const uint8_t* p) noexcept {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint16_t read_u16(const uint8_t* p) noexcept {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t read_u32(const uint8_t* p) noexcept {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint64_t read_u64(const uint8_t* p) noexcept {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// InputHandler — construction
// ---------------------------------------------------------------------------

InputHandler::InputHandler(IInputSender* sender)
    : sender_(sender) {
    // sender_ validity is a precondition; fail fast in debug builds.
    if (!sender_) {
        spdlog::critical("[InputHandler] sender must not be null");
        std::abort();
    }
}

// ---------------------------------------------------------------------------
// Static parsing — no side effects
// ---------------------------------------------------------------------------

Result<InputEvent> InputHandler::parse_packet(const uint8_t* data, size_t size) {
    if (!data || size < kInputHeaderSize) {
        return Result<InputEvent>::error("packet too short for header");
    }

    const uint8_t version = data[0];
    if (version != kInputProtocolVersion) {
        return Result<InputEvent>::error(
            std::format("unsupported protocol version: {}", version));
    }

    const auto type = static_cast<InputPacketType>(data[1]);
    InputEvent ev{};
    ev.type         = type;
    ev.seq          = read_u32(data + 4);
    ev.timestamp_us = read_u64(data + 8);

    switch (type) {
        case InputPacketType::kMouseMove:
            if (size < kMouseMovePacketSize) {
                return Result<InputEvent>::error("MOUSE_MOVE packet truncated");
            }
            ev.mouse_move.dx = read_i16(data + kInputHeaderSize);
            ev.mouse_move.dy = read_i16(data + kInputHeaderSize + 2);
            return ev;

        case InputPacketType::kMouseButton:
            if (size < kMouseButtonPacketSize) {
                return Result<InputEvent>::error("MOUSE_BUTTON packet truncated");
            }
            ev.mouse_button.button  = static_cast<MouseButton>(data[kInputHeaderSize]);
            ev.mouse_button.is_down = (data[kInputHeaderSize + 1] != 0u);
            return ev;

        case InputPacketType::kMouseWheel:
            if (size < kMouseWheelPacketSize) {
                return Result<InputEvent>::error("MOUSE_WHEEL packet truncated");
            }
            ev.mouse_wheel.wheel_x = read_i16(data + kInputHeaderSize);
            ev.mouse_wheel.wheel_y = read_i16(data + kInputHeaderSize + 2);
            return ev;

        case InputPacketType::kKey:
            if (size < kKeyPacketSize) {
                return Result<InputEvent>::error("KEY packet truncated");
            }
            ev.key.code    = static_cast<InputKeyCode>(read_u16(data + kInputHeaderSize));
            ev.key.is_down = (data[kInputHeaderSize + 2] != 0u);
            ev.key.repeat  = (data[kInputHeaderSize + 3] != 0u);
            return ev;

        case InputPacketType::kReleaseAll:
            if (size < kReleaseAllPacketSize) {
                return Result<InputEvent>::error("RELEASE_ALL packet truncated");
            }
            return ev;

        default:
            return Result<InputEvent>::error(
                std::format("unknown input packet type: 0x{:02X}", static_cast<uint8_t>(type)));
    }
}

// ---------------------------------------------------------------------------
// on_packet — parse + dispatch
// ---------------------------------------------------------------------------

void InputHandler::on_packet(const uint8_t* data, size_t size) {
    auto result = parse_packet(data, size);
    if (!result) {
        spdlog::warn("[InputHandler] parse_packet: {}", result.error());
        return;
    }

    const InputEvent& ev = result.value();
    switch (ev.type) {
        case InputPacketType::kMouseMove:
            handle_mouse_move(ev.mouse_move);
            break;
        case InputPacketType::kMouseButton:
            handle_mouse_button(ev.mouse_button);
            break;
        case InputPacketType::kMouseWheel:
            handle_mouse_wheel(ev.mouse_wheel);
            break;
        case InputPacketType::kKey:
            handle_key(ev.key);
            break;
        case InputPacketType::kReleaseAll:
            release_all();
            break;
    }
}

// ---------------------------------------------------------------------------
// Mouse movement — RELATIVE only (no MOUSEEVENTF_ABSOLUTE)
// ---------------------------------------------------------------------------

void InputHandler::handle_mouse_move(const MouseMoveEvent& ev) {
    if (ev.dx == 0 && ev.dy == 0) return;

    INPUT input{};
    input.type      = INPUT_MOUSE;
    input.mi.dx     = ev.dx;
    input.mi.dy     = ev.dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;  // Relative, no ABSOLUTE flag

    sender_->Send(&input, 1);
}

// ---------------------------------------------------------------------------
// Mouse buttons — track held state for release_all()
// ---------------------------------------------------------------------------

void InputHandler::handle_mouse_button(const MouseButtonEvent& ev) {
    INPUT input{};
    input.type = INPUT_MOUSE;

    switch (ev.button) {
        case MouseButton::kLeft:
            input.mi.dwFlags = ev.is_down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case MouseButton::kRight:
            input.mi.dwFlags = ev.is_down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButton::kMiddle:
            input.mi.dwFlags = ev.is_down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case MouseButton::kX1:
        case MouseButton::kX2:
            input.mi.dwFlags    = ev.is_down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData  = (ev.button == MouseButton::kX1) ? XBUTTON1 : XBUTTON2;
            break;
        default:
            spdlog::warn("[InputHandler] unknown mouse button: {:#04x}",
                         static_cast<uint8_t>(ev.button));
            return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    const uint32_t sent = sender_->Send(&input, 1);
    if (sent != 1u) {
        spdlog::warn("[InputHandler] SendInput(mouse button) failed: sent={}", sent);
        return;
    }
    if (ev.is_down) {
        held_buttons_.insert(static_cast<uint8_t>(ev.button));
    } else {
        held_buttons_.erase(static_cast<uint8_t>(ev.button));
    }
}

// ---------------------------------------------------------------------------
// Mouse wheel
// ---------------------------------------------------------------------------

void InputHandler::handle_mouse_wheel(const MouseWheelEvent& ev) {
    // Windows SendInput takes WHEEL_DELTA units (120 = one notch).
    // Browser sends raw pixel delta; treat 1 unit = WHEEL_DELTA/3 for natural scroll.
    constexpr int kWheelScaleFactor = 40;

    if (ev.wheel_y != 0) {
        INPUT input{};
        input.type         = INPUT_MOUSE;
        input.mi.dwFlags   = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(-ev.wheel_y * kWheelScaleFactor);
        sender_->Send(&input, 1);
    }

    if (ev.wheel_x != 0) {
        INPUT input{};
        input.type         = INPUT_MOUSE;
        input.mi.dwFlags   = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(ev.wheel_x * kWheelScaleFactor);
        sender_->Send(&input, 1);
    }
}

// ---------------------------------------------------------------------------
// Keyboard — scan code only (layout-independent, reliable in games)
// ---------------------------------------------------------------------------

void InputHandler::handle_key(const KeyEvent& ev) {
    // Ignore auto-repeat for key-down events — games handle their own repeat.
    if (ev.is_down && ev.repeat) return;

    const ScanCodeEntry entry = get_scan_code(ev.code);
    if (entry.scan_code == 0) {
        spdlog::warn("[InputHandler] unknown key code_id: {:#06x}",
                     static_cast<uint16_t>(ev.code));
        return;
    }

    INPUT input{};
    input.type          = INPUT_KEYBOARD;
    input.ki.wScan      = entry.scan_code;
    input.ki.dwFlags    = KEYEVENTF_SCANCODE;
    if (entry.extended) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!ev.is_down) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    const uint32_t sent = sender_->Send(&input, 1);
    if (sent != 1u) {
        spdlog::warn("[InputHandler] SendInput(key) failed: sent={}", sent);
        return;
    }
    const uint16_t sc = entry.scan_code |
        (entry.extended ? static_cast<uint16_t>(0x100u) : 0u);
    if (ev.is_down) {
        held_scan_codes_.insert(sc);
    } else {
        held_scan_codes_.erase(sc);
    }
}

// ---------------------------------------------------------------------------
// release_all — anti-sticky-key safeguard
// ---------------------------------------------------------------------------

void InputHandler::release_all() {
    std::set<uint16_t> keys_to_release;
    std::set<uint8_t>  buttons_to_release;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        keys_to_release    = held_scan_codes_;
        buttons_to_release = held_buttons_;
        held_scan_codes_.clear();
        held_buttons_.clear();
    }

    for (const uint16_t sc_entry : keys_to_release) {
        const bool extended   = (sc_entry & 0x100u) != 0u;
        const uint16_t sc     = sc_entry & 0x00FFu;

        INPUT input{};
        input.type       = INPUT_KEYBOARD;
        input.ki.wScan   = sc;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        if (extended) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        sender_->Send(&input, 1);
    }

    for (const uint8_t btn : buttons_to_release) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        switch (static_cast<MouseButton>(btn)) {
            case MouseButton::kLeft:
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                break;
            case MouseButton::kRight:
                input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                break;
            case MouseButton::kMiddle:
                input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
                break;
            case MouseButton::kX1:
                input.mi.dwFlags   = MOUSEEVENTF_XUP;
                input.mi.mouseData = XBUTTON1;
                break;
            case MouseButton::kX2:
                input.mi.dwFlags   = MOUSEEVENTF_XUP;
                input.mi.mouseData = XBUTTON2;
                break;
            default:
                continue;
        }
        sender_->Send(&input, 1);
    }

    if (!keys_to_release.empty() || !buttons_to_release.empty()) {
        spdlog::info("[InputHandler] release_all: released {} keys, {} buttons",
                     keys_to_release.size(), buttons_to_release.size());
    }
}

} // namespace gamestream
