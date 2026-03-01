#pragma once

#include "input_types.h"
#include "iinput_sender.h"
#include "result.h"

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <set>

namespace gamestream {

/// Receives raw DataChannel packets, parses them, and dispatches
/// OS-level input events via IInputSender.
///
/// Thread-safety:
///   on_packet() and release_all() may be called concurrently from
///   WebRTC's worker thread. Internal state is guarded by a mutex.
///
/// Anti-sticky-key guarantee:
///   Call release_all() on channel close, window blur, or Pointer Lock loss.
///   The handler tracks all held keys/buttons and sends KEY_UP / BUTTON_UP
///   for each one.
class InputHandler {
public:
    /// Construct with the injection point for OS input.
    /// @param sender  Must not be null; must outlive this instance.
    explicit InputHandler(IInputSender* sender);

    ~InputHandler() = default;

    InputHandler(const InputHandler&)            = delete;
    InputHandler& operator=(const InputHandler&) = delete;
    InputHandler(InputHandler&&)                 = delete;
    InputHandler& operator=(InputHandler&&)      = delete;

    // -----------------------------------------------------------------------
    // Hot path — called from DataChannel callbacks
    // -----------------------------------------------------------------------

    /// Parse and dispatch a raw binary packet from the DataChannel.
    /// Unknown packet types are silently ignored (logged at WARN level).
    /// Size/version validation failures are logged at WARN level.
    void on_packet(const uint8_t* data, size_t size);

    /// Release all currently held keys and mouse buttons via SendInput.
    /// Safe to call even if nothing is held.
    void release_all();

    // -----------------------------------------------------------------------
    // Pure parsing — no side effects, useful for tests and the overlay tool
    // -----------------------------------------------------------------------

    /// Parse a raw packet and return an InputEvent without dispatching.
    /// Returns error on version mismatch, unknown type, or truncated data.
    [[nodiscard]] static Result<InputEvent> parse_packet(const uint8_t* data, size_t size);

private:
    void handle_mouse_move(const MouseMoveEvent& ev);
    void handle_mouse_button(const MouseButtonEvent& ev);
    void handle_mouse_wheel(const MouseWheelEvent& ev);
    void handle_key(const KeyEvent& ev);

    IInputSender* sender_;       // Non-owning; injected at construction.

    std::mutex         state_mutex_;
    std::set<uint16_t> held_scan_codes_;   // Scan codes of currently pressed keys.
    std::set<uint8_t>  held_buttons_;      // MouseButton values of held mouse buttons.
};

} // namespace gamestream
