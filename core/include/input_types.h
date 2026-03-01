#pragma once

#include <cstdint>
#include <string_view>

namespace gamestream {

// ---------------------------------------------------------------------------
// Binary input protocol — Version 1
//
// All multi-byte integers are little-endian.
//
// Packet layout:
//
//   Common header (16 bytes):
//     [0]      version    : uint8_t  — protocol version (must be kInputProtocolVersion)
//     [1]      type       : uint8_t  — InputPacketType
//     [2]      flags      : uint8_t  — reserved for future use (must be 0)
//     [3]      reserved   : uint8_t  — padding (must be 0)
//     [4..7]   seq        : uint32_t — monotonically increasing sequence number
//     [8..15]  timestamp  : uint64_t — client-side microseconds (performance.now() * 1000)
//
//   Type-specific payload (follows immediately after the header):
//
//     MOUSE_MOVE   (type=0x01, total 20 bytes): [dx:int16_t] [dy:int16_t]
//     MOUSE_BUTTON (type=0x02, total 18 bytes): [button:uint8_t] [is_down:uint8_t]
//     MOUSE_WHEEL  (type=0x03, total 20 bytes): [wheel_x:int16_t] [wheel_y:int16_t]
//     KEY          (type=0x04, total 20 bytes): [code_id:uint16_t] [is_down:uint8_t] [repeat:uint8_t]
//     RELEASE_ALL  (type=0x05, total 16 bytes): (no payload)
// ---------------------------------------------------------------------------

constexpr uint8_t kInputProtocolVersion = 1;

constexpr size_t kInputHeaderSize       = 16;
constexpr size_t kMouseMovePacketSize   = 20;
constexpr size_t kMouseButtonPacketSize = 18;
constexpr size_t kMouseWheelPacketSize  = 20;
constexpr size_t kKeyPacketSize         = 20;
constexpr size_t kReleaseAllPacketSize  = 16;

// ---------------------------------------------------------------------------
// Packet type tag
// ---------------------------------------------------------------------------

enum class InputPacketType : uint8_t {
    kMouseMove   = 0x01,
    kMouseButton = 0x02,
    kMouseWheel  = 0x03,
    kKey         = 0x04,
    kReleaseAll  = 0x05,
};

// ---------------------------------------------------------------------------
// Mouse button identifiers
// ---------------------------------------------------------------------------

enum class MouseButton : uint8_t {
    kLeft   = 0x01,
    kRight  = 0x02,
    kMiddle = 0x03,
    kX1     = 0x04,
    kX2     = 0x05,
};

// ---------------------------------------------------------------------------
// Key code identifiers
//
// Maps KeyboardEvent.code (browser string) to a stable uint16_t.
// The JavaScript client uses the same enum (as a numeric constant table).
// The host maps code_id → Windows scan code via get_scan_code().
//
// Ranges:
//   0x0001..0x001A  — Letters A–Z
//   0x0021..0x002A  — Digits 1–0 (top row)
//   0x0031..0x003C  — Function keys F1–F12
//   0x0041..0x0060  — Control/navigation/misc keys
//   0x0061..0x007A  — Numpad
// ---------------------------------------------------------------------------

enum class InputKeyCode : uint16_t {
    kUnknown = 0x0000,

    // Letters
    kKeyA = 0x0001, kKeyB = 0x0002, kKeyC = 0x0003, kKeyD = 0x0004,
    kKeyE = 0x0005, kKeyF = 0x0006, kKeyG = 0x0007, kKeyH = 0x0008,
    kKeyI = 0x0009, kKeyJ = 0x000A, kKeyK = 0x000B, kKeyL = 0x000C,
    kKeyM = 0x000D, kKeyN = 0x000E, kKeyO = 0x000F, kKeyP = 0x0010,
    kKeyQ = 0x0011, kKeyR = 0x0012, kKeyS = 0x0013, kKeyT = 0x0014,
    kKeyU = 0x0015, kKeyV = 0x0016, kKeyW = 0x0017, kKeyX = 0x0018,
    kKeyY = 0x0019, kKeyZ = 0x001A,

    // Digits (top row)
    kDigit1 = 0x0021, kDigit2 = 0x0022, kDigit3 = 0x0023, kDigit4 = 0x0024,
    kDigit5 = 0x0025, kDigit6 = 0x0026, kDigit7 = 0x0027, kDigit8 = 0x0028,
    kDigit9 = 0x0029, kDigit0 = 0x002A,

    // Function keys
    kF1  = 0x0031, kF2  = 0x0032, kF3  = 0x0033, kF4  = 0x0034,
    kF5  = 0x0035, kF6  = 0x0036, kF7  = 0x0037, kF8  = 0x0038,
    kF9  = 0x0039, kF10 = 0x003A, kF11 = 0x003B, kF12 = 0x003C,

    // Control keys
    kEscape     = 0x0041,
    kTab        = 0x0042,
    kCapsLock   = 0x0043,
    kLShift     = 0x0044,
    kRShift     = 0x0045,
    kLControl   = 0x0046,
    kRControl   = 0x0047,
    kLAlt       = 0x0048,
    kRAlt       = 0x0049,  // AltGr
    kSpace      = 0x004A,
    kEnter      = 0x004B,
    kBackspace  = 0x004C,

    // Navigation
    kInsert     = 0x004D,
    kDelete     = 0x004E,
    kHome       = 0x004F,
    kEnd        = 0x0050,
    kPageUp     = 0x0051,
    kPageDown   = 0x0052,
    kArrowUp    = 0x0053,
    kArrowDown  = 0x0054,
    kArrowLeft  = 0x0055,
    kArrowRight = 0x0056,

    // Punctuation / symbols
    kMinus          = 0x0057,  // -
    kEqual          = 0x0058,  // =
    kBracketLeft    = 0x0059,  // [
    kBracketRight   = 0x005A,  // ]
    kBackslash      = 0x005B,  // backslash
    kSemicolon      = 0x005C,  // ;
    kQuote          = 0x005D,  // '
    kBackquote      = 0x005E,  // `
    kComma          = 0x005F,  // ,
    kPeriod         = 0x0060,  // .
    kSlash          = 0x0061,  // /

    // Numpad
    kNumpad0    = 0x0071,
    kNumpad1    = 0x0072,
    kNumpad2    = 0x0073,
    kNumpad3    = 0x0074,
    kNumpad4    = 0x0075,
    kNumpad5    = 0x0076,
    kNumpad6    = 0x0077,
    kNumpad7    = 0x0078,
    kNumpad8    = 0x0079,
    kNumpad9    = 0x007A,
    kNumpadDecimal  = 0x007B,
    kNumpadEnter    = 0x007C,
    kNumpadAdd      = 0x007D,
    kNumpadSubtract = 0x007E,
    kNumpadMultiply = 0x007F,
    kNumpadDivide   = 0x0080,
    kNumLock        = 0x0081,

    // Extra
    kPrintScreen = 0x0091,
    kScrollLock  = 0x0092,
    kPause       = 0x0093,
    kLMeta       = 0x0094,  // Left Windows key
    kRMeta       = 0x0095,  // Right Windows key
    kContextMenu = 0x0096,
};

// ---------------------------------------------------------------------------
// Scan code entry — pairs a PS/2 scan code with the extended-key flag.
// ---------------------------------------------------------------------------

struct ScanCodeEntry {
    uint16_t scan_code;    ///< PS/2 set-1 scan code (Make code, 1 byte)
    bool     extended;     ///< true → KEYEVENTF_EXTENDEDKEY required
};

/// Map InputKeyCode → ScanCodeEntry.
/// Returns {0, false} for unknown / unmapped codes.
[[nodiscard]] inline ScanCodeEntry get_scan_code(InputKeyCode code) noexcept {
    // PS/2 Set 1 scan codes (US ANSI, single-byte make codes).
    // Extended keys (0xE0 prefix) use extended=true.
    switch (code) {
        // Letters (standard scan codes)
        case InputKeyCode::kKeyA: return {0x1E, false};
        case InputKeyCode::kKeyB: return {0x30, false};
        case InputKeyCode::kKeyC: return {0x2E, false};
        case InputKeyCode::kKeyD: return {0x20, false};
        case InputKeyCode::kKeyE: return {0x12, false};
        case InputKeyCode::kKeyF: return {0x21, false};
        case InputKeyCode::kKeyG: return {0x22, false};
        case InputKeyCode::kKeyH: return {0x23, false};
        case InputKeyCode::kKeyI: return {0x17, false};
        case InputKeyCode::kKeyJ: return {0x24, false};
        case InputKeyCode::kKeyK: return {0x25, false};
        case InputKeyCode::kKeyL: return {0x26, false};
        case InputKeyCode::kKeyM: return {0x32, false};
        case InputKeyCode::kKeyN: return {0x31, false};
        case InputKeyCode::kKeyO: return {0x18, false};
        case InputKeyCode::kKeyP: return {0x19, false};
        case InputKeyCode::kKeyQ: return {0x10, false};
        case InputKeyCode::kKeyR: return {0x13, false};
        case InputKeyCode::kKeyS: return {0x1F, false};
        case InputKeyCode::kKeyT: return {0x14, false};
        case InputKeyCode::kKeyU: return {0x16, false};
        case InputKeyCode::kKeyV: return {0x2F, false};
        case InputKeyCode::kKeyW: return {0x11, false};
        case InputKeyCode::kKeyX: return {0x2D, false};
        case InputKeyCode::kKeyY: return {0x15, false};
        case InputKeyCode::kKeyZ: return {0x2C, false};

        // Digits (top row)
        case InputKeyCode::kDigit1: return {0x02, false};
        case InputKeyCode::kDigit2: return {0x03, false};
        case InputKeyCode::kDigit3: return {0x04, false};
        case InputKeyCode::kDigit4: return {0x05, false};
        case InputKeyCode::kDigit5: return {0x06, false};
        case InputKeyCode::kDigit6: return {0x07, false};
        case InputKeyCode::kDigit7: return {0x08, false};
        case InputKeyCode::kDigit8: return {0x09, false};
        case InputKeyCode::kDigit9: return {0x0A, false};
        case InputKeyCode::kDigit0: return {0x0B, false};

        // Function keys
        case InputKeyCode::kF1:  return {0x3B, false};
        case InputKeyCode::kF2:  return {0x3C, false};
        case InputKeyCode::kF3:  return {0x3D, false};
        case InputKeyCode::kF4:  return {0x3E, false};
        case InputKeyCode::kF5:  return {0x3F, false};
        case InputKeyCode::kF6:  return {0x40, false};
        case InputKeyCode::kF7:  return {0x41, false};
        case InputKeyCode::kF8:  return {0x42, false};
        case InputKeyCode::kF9:  return {0x43, false};
        case InputKeyCode::kF10: return {0x44, false};
        case InputKeyCode::kF11: return {0x57, false};
        case InputKeyCode::kF12: return {0x58, false};

        // Control keys
        case InputKeyCode::kEscape:    return {0x01, false};
        case InputKeyCode::kTab:       return {0x0F, false};
        case InputKeyCode::kCapsLock:  return {0x3A, false};
        case InputKeyCode::kLShift:    return {0x2A, false};
        case InputKeyCode::kRShift:    return {0x36, false};
        case InputKeyCode::kLControl:  return {0x1D, false};
        case InputKeyCode::kRControl:  return {0x1D, true};   // E0 1D
        case InputKeyCode::kLAlt:      return {0x38, false};
        case InputKeyCode::kRAlt:      return {0x38, true};   // E0 38
        case InputKeyCode::kSpace:     return {0x39, false};
        case InputKeyCode::kEnter:     return {0x1C, false};
        case InputKeyCode::kBackspace: return {0x0E, false};

        // Navigation (all extended)
        case InputKeyCode::kInsert:     return {0x52, true};
        case InputKeyCode::kDelete:     return {0x53, true};
        case InputKeyCode::kHome:       return {0x47, true};
        case InputKeyCode::kEnd:        return {0x4F, true};
        case InputKeyCode::kPageUp:     return {0x49, true};
        case InputKeyCode::kPageDown:   return {0x51, true};
        case InputKeyCode::kArrowUp:    return {0x48, true};
        case InputKeyCode::kArrowDown:  return {0x50, true};
        case InputKeyCode::kArrowLeft:  return {0x4B, true};
        case InputKeyCode::kArrowRight: return {0x4D, true};

        // Punctuation
        case InputKeyCode::kMinus:       return {0x0C, false};
        case InputKeyCode::kEqual:       return {0x0D, false};
        case InputKeyCode::kBracketLeft: return {0x1A, false};
        case InputKeyCode::kBracketRight:return {0x1B, false};
        case InputKeyCode::kBackslash:   return {0x2B, false};
        case InputKeyCode::kSemicolon:   return {0x27, false};
        case InputKeyCode::kQuote:       return {0x28, false};
        case InputKeyCode::kBackquote:   return {0x29, false};
        case InputKeyCode::kComma:       return {0x33, false};
        case InputKeyCode::kPeriod:      return {0x34, false};
        case InputKeyCode::kSlash:       return {0x35, false};

        // Numpad
        case InputKeyCode::kNumpad0:    return {0x52, false};
        case InputKeyCode::kNumpad1:    return {0x4F, false};
        case InputKeyCode::kNumpad2:    return {0x50, false};
        case InputKeyCode::kNumpad3:    return {0x51, false};
        case InputKeyCode::kNumpad4:    return {0x4B, false};
        case InputKeyCode::kNumpad5:    return {0x4C, false};
        case InputKeyCode::kNumpad6:    return {0x4D, false};
        case InputKeyCode::kNumpad7:    return {0x47, false};
        case InputKeyCode::kNumpad8:    return {0x48, false};
        case InputKeyCode::kNumpad9:    return {0x49, false};
        case InputKeyCode::kNumpadDecimal:  return {0x53, false};
        case InputKeyCode::kNumpadEnter:    return {0x1C, true};
        case InputKeyCode::kNumpadAdd:      return {0x4E, false};
        case InputKeyCode::kNumpadSubtract: return {0x4A, false};
        case InputKeyCode::kNumpadMultiply: return {0x37, false};
        case InputKeyCode::kNumpadDivide:   return {0x35, true};
        case InputKeyCode::kNumLock:        return {0x45, false};

        // Extra
        case InputKeyCode::kPrintScreen: return {0x37, true};  // E0 37
        case InputKeyCode::kScrollLock:  return {0x46, false};
        case InputKeyCode::kPause:       return {0x45, false};  // Special handling needed
        case InputKeyCode::kLMeta:       return {0x5B, true};
        case InputKeyCode::kRMeta:       return {0x5C, true};
        case InputKeyCode::kContextMenu: return {0x5D, true};

        default:
            return {0, false};
    }
}

// ---------------------------------------------------------------------------
// Parsed input event — result of InputHandler::parse_packet()
// ---------------------------------------------------------------------------

struct MouseMoveEvent   { int16_t dx; int16_t dy; };
struct MouseButtonEvent { MouseButton button; bool is_down; };
struct MouseWheelEvent  { int16_t wheel_x; int16_t wheel_y; };
struct KeyEvent         { InputKeyCode code; bool is_down; bool repeat; };

/// Unified parsed event returned by InputHandler::parse_packet().
struct InputEvent {
    InputPacketType type;
    uint32_t        seq;
    uint64_t        timestamp_us;

    union {
        MouseMoveEvent   mouse_move;
        MouseButtonEvent mouse_button;
        MouseWheelEvent  mouse_wheel;
        KeyEvent         key;
    };
};

} // namespace gamestream
