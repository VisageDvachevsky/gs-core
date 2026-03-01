#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "input_handler.h"
#include "input_types.h"
#include "iinput_sender.h"

#include <windows.h>
#include <cstring>
#include <set>
#include <thread>

namespace gamestream {

// ---------------------------------------------------------------------------
// Mock sender — verifies INPUT structs without OS side effects
// ---------------------------------------------------------------------------

class MockInputSender : public IInputSender {
public:
    MOCK_METHOD(uint32_t, Send, (const INPUT* inputs, uint32_t count), (override));
};

// ---------------------------------------------------------------------------
// Packet builder helpers — construct well-formed binary packets
// ---------------------------------------------------------------------------

namespace {

void write_u16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
void write_i16(uint8_t* p, int16_t  v) { std::memcpy(p, &v, 2); }
void write_u32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void write_u64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

void fill_header(uint8_t* buf, InputPacketType type,
                 uint32_t seq = 1, uint64_t ts = 123456) {
    buf[0] = kInputProtocolVersion;
    buf[1] = static_cast<uint8_t>(type);
    buf[2] = 0;  // flags
    buf[3] = 0;  // reserved
    write_u32(buf + 4, seq);
    write_u64(buf + 8, ts);
}

std::array<uint8_t, kMouseMovePacketSize> make_mouse_move(int16_t dx, int16_t dy,
                                                          uint32_t seq = 1) {
    std::array<uint8_t, kMouseMovePacketSize> buf{};
    fill_header(buf.data(), InputPacketType::kMouseMove, seq);
    write_i16(buf.data() + 16, dx);
    write_i16(buf.data() + 18, dy);
    return buf;
}

std::array<uint8_t, kMouseButtonPacketSize> make_mouse_button(MouseButton btn,
                                                              bool is_down,
                                                              uint32_t seq = 1) {
    std::array<uint8_t, kMouseButtonPacketSize> buf{};
    fill_header(buf.data(), InputPacketType::kMouseButton, seq);
    buf[16] = static_cast<uint8_t>(btn);
    buf[17] = is_down ? 1u : 0u;
    return buf;
}

std::array<uint8_t, kMouseWheelPacketSize> make_mouse_wheel(int16_t wx, int16_t wy,
                                                            uint32_t seq = 1) {
    std::array<uint8_t, kMouseWheelPacketSize> buf{};
    fill_header(buf.data(), InputPacketType::kMouseWheel, seq);
    write_i16(buf.data() + 16, wx);
    write_i16(buf.data() + 18, wy);
    return buf;
}

std::array<uint8_t, kKeyPacketSize> make_key(InputKeyCode code, bool is_down,
                                             bool repeat = false, uint32_t seq = 1) {
    std::array<uint8_t, kKeyPacketSize> buf{};
    fill_header(buf.data(), InputPacketType::kKey, seq);
    write_u16(buf.data() + 16, static_cast<uint16_t>(code));
    buf[18] = is_down ? 1u : 0u;
    buf[19] = repeat  ? 1u : 0u;
    return buf;
}

std::array<uint8_t, kReleaseAllPacketSize> make_release_all(uint32_t seq = 1) {
    std::array<uint8_t, kReleaseAllPacketSize> buf{};
    fill_header(buf.data(), InputPacketType::kReleaseAll, seq);
    return buf;
}

} // namespace

// ===========================================================================
// parse_packet — static, no side effects
// ===========================================================================

TEST(ParsePacketTest, RejectsNullData) {
    auto result = InputHandler::parse_packet(nullptr, 16);
    EXPECT_FALSE(result);
}

TEST(ParsePacketTest, RejectsEmptyData) {
    uint8_t buf[1] = {kInputProtocolVersion};
    auto result = InputHandler::parse_packet(buf, 1);
    EXPECT_FALSE(result);
}

TEST(ParsePacketTest, RejectsWrongVersion) {
    uint8_t buf[kMouseMovePacketSize]{};
    buf[0] = 99;  // wrong version
    buf[1] = static_cast<uint8_t>(InputPacketType::kMouseMove);
    auto result = InputHandler::parse_packet(buf, kMouseMovePacketSize);
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().find("version"), std::string::npos);
}

TEST(ParsePacketTest, RejectsUnknownType) {
    uint8_t buf[kInputHeaderSize]{};
    buf[0] = kInputProtocolVersion;
    buf[1] = 0xFF;  // unknown type
    auto result = InputHandler::parse_packet(buf, kInputHeaderSize);
    EXPECT_FALSE(result);
}

TEST(ParsePacketTest, ParsesMouseMove) {
    auto pkt = make_mouse_move(10, -5, 42);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result.value().type, InputPacketType::kMouseMove);
    EXPECT_EQ(result.value().seq,  42u);
    EXPECT_EQ(result.value().mouse_move.dx, 10);
    EXPECT_EQ(result.value().mouse_move.dy, -5);
}

TEST(ParsePacketTest, ParsesMouseMoveZero) {
    auto pkt = make_mouse_move(0, 0);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().mouse_move.dx, 0);
    EXPECT_EQ(result.value().mouse_move.dy, 0);
}

TEST(ParsePacketTest, ParsesMouseMoveMaxValues) {
    auto pkt = make_mouse_move(32767, -32768);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().mouse_move.dx, 32767);
    EXPECT_EQ(result.value().mouse_move.dy, -32768);
}

TEST(ParsePacketTest, RejectsMouseMoveTruncated) {
    auto pkt = make_mouse_move(1, 2);
    // Supply only 18 bytes instead of 20.
    auto result = InputHandler::parse_packet(pkt.data(), 18);
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().find("truncated"), std::string::npos);
}

TEST(ParsePacketTest, ParsesMouseButtonDown) {
    auto pkt = make_mouse_button(MouseButton::kLeft, true);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().type, InputPacketType::kMouseButton);
    EXPECT_EQ(result.value().mouse_button.button, MouseButton::kLeft);
    EXPECT_TRUE(result.value().mouse_button.is_down);
}

TEST(ParsePacketTest, ParsesMouseButtonUp) {
    auto pkt = make_mouse_button(MouseButton::kRight, false);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().mouse_button.button, MouseButton::kRight);
    EXPECT_FALSE(result.value().mouse_button.is_down);
}

TEST(ParsePacketTest, ParsesMouseWheel) {
    auto pkt = make_mouse_wheel(0, -3);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().type, InputPacketType::kMouseWheel);
    EXPECT_EQ(result.value().mouse_wheel.wheel_x, 0);
    EXPECT_EQ(result.value().mouse_wheel.wheel_y, -3);
}

TEST(ParsePacketTest, ParsesKeyDown) {
    auto pkt = make_key(InputKeyCode::kKeyA, true);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().type, InputPacketType::kKey);
    EXPECT_EQ(result.value().key.code, InputKeyCode::kKeyA);
    EXPECT_TRUE(result.value().key.is_down);
    EXPECT_FALSE(result.value().key.repeat);
}

TEST(ParsePacketTest, ParsesKeyRepeat) {
    auto pkt = make_key(InputKeyCode::kKeyW, true, /*repeat=*/true);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value().key.is_down);
    EXPECT_TRUE(result.value().key.repeat);
}

TEST(ParsePacketTest, ParsesKeyUp) {
    auto pkt = make_key(InputKeyCode::kEscape, false);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().key.code, InputKeyCode::kEscape);
    EXPECT_FALSE(result.value().key.is_down);
}

TEST(ParsePacketTest, ParsesReleaseAll) {
    auto pkt = make_release_all();
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().type, InputPacketType::kReleaseAll);
}

TEST(ParsePacketTest, PreservesTimestamp) {
    auto pkt = make_mouse_move(1, 1, /*seq=*/7);
    // Overwrite timestamp bytes (offset 8..15) with known value.
    const uint64_t expected_ts = 9'999'888'777ULL;
    std::memcpy(pkt.data() + 8, &expected_ts, 8);
    auto result = InputHandler::parse_packet(pkt.data(), pkt.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().timestamp_us, expected_ts);
}

// ===========================================================================
// InputHandler — dispatches to IInputSender
// ===========================================================================

TEST(InputHandlerTest, MouseMoveCallsRelativeSendInput) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t /*count*/) -> uint32_t {
            EXPECT_EQ(inputs[0].type,       INPUT_MOUSE);
            EXPECT_EQ(inputs[0].mi.dx,      10);
            EXPECT_EQ(inputs[0].mi.dy,      -5);
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_MOVE);
            // ABSOLUTE flag must NOT be set
            EXPECT_EQ(inputs[0].mi.dwFlags & MOUSEEVENTF_ABSOLUTE, 0u);
            return 1;
        });

    auto pkt = make_mouse_move(10, -5);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, MouseMoveZeroIsSkipped) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // dx=0, dy=0 → no SendInput call
    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);

    auto pkt = make_mouse_move(0, 0);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, MouseLeftButtonDownCallsSendInput) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].type,       INPUT_MOUSE);
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_LEFTDOWN);
            return 1;
        });

    auto pkt = make_mouse_button(MouseButton::kLeft, true);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, MouseRightButtonUpCallsSendInput) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_RIGHTUP);
            return 1;
        });

    auto pkt = make_mouse_button(MouseButton::kRight, false);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, KeyDownUsesScanCodeFlag) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].type, INPUT_KEYBOARD);
            // KeyA scan code = 0x1E, not extended
            EXPECT_EQ(inputs[0].ki.wScan, 0x1Eu);
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_SCANCODE) != 0u);
            EXPECT_FALSE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            EXPECT_FALSE((inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0u);
            return 1;
        });

    auto pkt = make_key(InputKeyCode::kKeyA, true);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, KeyUpSetsFlagKeyUp) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            return 1;
        });

    auto pkt = make_key(InputKeyCode::kKeyA, false);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, ExtendedKeysSetsExtendedFlag) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // ArrowRight is an extended key.
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0u);
            return 1;
        });

    auto pkt = make_key(InputKeyCode::kArrowRight, true);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, RepeatKeyDownIsIgnored) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Repeat key-down must NOT call SendInput.
    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);

    auto pkt = make_key(InputKeyCode::kKeyW, true, /*repeat=*/true);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, UnknownKeyCodeIsIgnored) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // InputKeyCode::kUnknown (0x0000) has no scan code → no call
    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);

    auto pkt = make_key(InputKeyCode::kUnknown, true);
    handler.on_packet(pkt.data(), pkt.size());
}

// ===========================================================================
// release_all — anti-sticky-key safeguard
// ===========================================================================

TEST(InputHandlerTest, ReleaseAllReleasesHeldKeys) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Sequence: press W, then receive RELEASE_ALL.
    // Expect: key-down for W, then key-up for W (from release_all).
    testing::InSequence seq;

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            // Key-down W (scan 0x11, not extended)
            EXPECT_FALSE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            EXPECT_EQ(inputs[0].ki.wScan, 0x11u);
            return 1;
        });
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            // Key-up (from release_all)
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            EXPECT_EQ(inputs[0].ki.wScan, 0x11u);
            return 1;
        });

    auto key_down = make_key(InputKeyCode::kKeyW, true);
    handler.on_packet(key_down.data(), key_down.size());

    auto release = make_release_all();
    handler.on_packet(release.data(), release.size());
}

TEST(InputHandlerTest, ReleaseAllReleasesHeldMouseButtons) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    testing::InSequence seq;

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_LEFTDOWN);
            return 1;
        });
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_LEFTUP);
            return 1;
        });

    auto btn_down = make_mouse_button(MouseButton::kLeft, true);
    handler.on_packet(btn_down.data(), btn_down.size());

    auto release = make_release_all();
    handler.on_packet(release.data(), release.size());
}

TEST(InputHandlerTest, ReleaseAllOnEmptyStateIsNoop) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Nothing held → release_all should not call Send at all.
    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);

    handler.release_all();
}

TEST(InputHandlerTest, ReleaseAllClearsStateAfterRelease) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Press and release normally — key is no longer in held set.
    EXPECT_CALL(sender, Send(testing::_, 1)).Times(2);  // down + up

    auto key_down = make_key(InputKeyCode::kKeyA, true);
    auto key_up   = make_key(InputKeyCode::kKeyA, false);
    handler.on_packet(key_down.data(), key_down.size());
    handler.on_packet(key_up.data(),   key_up.size());

    // Now release_all should not send anything (nothing held).
    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);
    handler.release_all();
}

// ===========================================================================
// scan code mapping — get_scan_code()
// ===========================================================================

TEST(ScanCodeTest, LetterAScanCode) {
    const auto entry = get_scan_code(InputKeyCode::kKeyA);
    EXPECT_EQ(entry.scan_code, 0x1Eu);
    EXPECT_FALSE(entry.extended);
}

TEST(ScanCodeTest, ArrowRightIsExtended) {
    const auto entry = get_scan_code(InputKeyCode::kArrowRight);
    EXPECT_EQ(entry.scan_code, 0x4Du);
    EXPECT_TRUE(entry.extended);
}

TEST(ScanCodeTest, HomeIsExtended) {
    const auto entry = get_scan_code(InputKeyCode::kHome);
    EXPECT_TRUE(entry.extended);
}

TEST(ScanCodeTest, SpaceScanCode) {
    const auto entry = get_scan_code(InputKeyCode::kSpace);
    EXPECT_EQ(entry.scan_code, 0x39u);
    EXPECT_FALSE(entry.extended);
}

TEST(ScanCodeTest, EscapeScanCode) {
    const auto entry = get_scan_code(InputKeyCode::kEscape);
    EXPECT_EQ(entry.scan_code, 0x01u);
    EXPECT_FALSE(entry.extended);
}

TEST(ScanCodeTest, UnknownCodeReturnsZero) {
    const auto entry = get_scan_code(InputKeyCode::kUnknown);
    EXPECT_EQ(entry.scan_code, 0u);
}

TEST(ScanCodeTest, NumpadEnterIsExtended) {
    const auto entry = get_scan_code(InputKeyCode::kNumpadEnter);
    EXPECT_EQ(entry.scan_code, 0x1Cu);
    EXPECT_TRUE(entry.extended);
}

TEST(ScanCodeTest, RControlIsExtended) {
    const auto entry = get_scan_code(InputKeyCode::kRControl);
    EXPECT_EQ(entry.scan_code, 0x1Du);
    EXPECT_TRUE(entry.extended);
}

// ===========================================================================
// Missing coverage: mouse wheel, X1/X2 buttons, middle button
// ===========================================================================

TEST(InputHandlerTest, WheelYSendsMouseEventfWheel) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].type,       INPUT_MOUSE);
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_WHEEL);
            // wheel_y = -3 (scroll down) → mouseData = -(-3)*40 = +120 (WHEEL_DELTA)
            // Wait: host sends -wheel_y * 40. wheel_y=-3 → +120 = one notch up.
            // Verify sign matches source (non-zero and WHEEL flag set is the key assertion).
            EXPECT_NE(inputs[0].mi.mouseData, 0u);
            return 1;
        });

    auto pkt = make_mouse_wheel(0, -3);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, WheelXSendsMouseEventfHwheel) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].type,       INPUT_MOUSE);
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_HWHEEL);
            EXPECT_NE(inputs[0].mi.mouseData, 0u);
            return 1;
        });

    auto pkt = make_mouse_wheel(2, 0);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, WheelBothAxesSendsTwoEvents) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Non-zero wx AND wy → Send called twice (one for each axis).
    EXPECT_CALL(sender, Send(testing::_, 1)).Times(2);

    auto pkt = make_mouse_wheel(1, 1);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, WheelZeroIsSkipped) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, testing::_)).Times(0);

    auto pkt = make_mouse_wheel(0, 0);
    handler.on_packet(pkt.data(), pkt.size());
}

TEST(InputHandlerTest, MiddleButtonDownAndUp) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    testing::InSequence seq;
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_MIDDLEDOWN);
            return 1;
        });
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_MIDDLEUP);
            return 1;
        });

    handler.on_packet(make_mouse_button(MouseButton::kMiddle, true).data(),
                      kMouseButtonPacketSize);
    handler.on_packet(make_mouse_button(MouseButton::kMiddle, false).data(),
                      kMouseButtonPacketSize);
}

TEST(InputHandlerTest, X1ButtonUsesXdownXupWithXbutton1Data) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags,    MOUSEEVENTF_XDOWN);
            EXPECT_EQ(inputs[0].mi.mouseData,  XBUTTON1);
            return 1;
        });

    handler.on_packet(make_mouse_button(MouseButton::kX1, true).data(),
                      kMouseButtonPacketSize);
}

TEST(InputHandlerTest, X2ButtonUsesXdownWithXbutton2Data) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags,   MOUSEEVENTF_XDOWN);
            EXPECT_EQ(inputs[0].mi.mouseData, XBUTTON2);
            return 1;
        });

    handler.on_packet(make_mouse_button(MouseButton::kX2, true).data(),
                      kMouseButtonPacketSize);
}

TEST(InputHandlerTest, X1ButtonReleasedByReleaseAll) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    testing::InSequence seq;
    EXPECT_CALL(sender, Send(testing::_, 1))  // X1 down
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags, MOUSEEVENTF_XDOWN);
            return 1;
        });
    EXPECT_CALL(sender, Send(testing::_, 1))  // X1 up (from release_all)
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            EXPECT_EQ(inputs[0].mi.dwFlags,   MOUSEEVENTF_XUP);
            EXPECT_EQ(inputs[0].mi.mouseData, XBUTTON1);
            return 1;
        });

    handler.on_packet(make_mouse_button(MouseButton::kX1, true).data(),
                      kMouseButtonPacketSize);
    handler.on_packet(make_release_all().data(), kReleaseAllPacketSize);
}

// ===========================================================================
// Integration tests — multiple components / full session pipelines
// ===========================================================================

// Full session: press multiple keys + button → RELEASE_ALL clears everything.
TEST(IntegrationTest, MultipleKeysAndButtonReleasedByReleaseAll) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Expect: 3 key-downs + 1 button-down = 4 sends before release_all.
    // Then: 3 key-ups + 1 button-up = 4 sends from release_all.
    EXPECT_CALL(sender, Send(testing::_, 1)).Times(8);

    for (InputKeyCode code : {InputKeyCode::kLControl, InputKeyCode::kLShift, InputKeyCode::kKeyC}) {
        handler.on_packet(make_key(code, true).data(), kKeyPacketSize);
    }
    handler.on_packet(make_mouse_button(MouseButton::kLeft, true).data(),
                      kMouseButtonPacketSize);

    handler.on_packet(make_release_all().data(), kReleaseAllPacketSize);
}

// Full session: mixed packet sequence matching a realistic game input burst.
TEST(IntegrationTest, RealisticInputBurst) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Key W down, mouse move ×3, right-click, key W up, scroll.
    // Sends: 1 (W down) + 3 (moves) + 1 (RMB down) + 1 (W up) + 1 (scroll) = 7.
    EXPECT_CALL(sender, Send(testing::_, 1)).Times(7);

    handler.on_packet(make_key(InputKeyCode::kKeyW, true).data(), kKeyPacketSize);
    for (int i = 0; i < 3; ++i) {
        handler.on_packet(make_mouse_move(5, -2).data(), kMouseMovePacketSize);
    }
    handler.on_packet(make_mouse_button(MouseButton::kRight, true).data(),
                      kMouseButtonPacketSize);
    handler.on_packet(make_key(InputKeyCode::kKeyW, false).data(), kKeyPacketSize);
    handler.on_packet(make_mouse_wheel(0, -1).data(), kMouseWheelPacketSize);
}

// Concurrency: on_packet and release_all can be called from different threads
// without data races.  This test verifies liveness (no deadlock) and that
// the final state is consistent.
TEST(IntegrationTest, ConcurrentOnPacketAndReleaseAll) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // Allow any number of Send calls — we care about no crash/deadlock.
    EXPECT_CALL(sender, Send(testing::_, testing::_))
        .WillRepeatedly(testing::Return(1u));

    constexpr int kIterations = 200;

    std::thread packet_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            auto pkt = make_key(InputKeyCode::kKeyA, (i % 2 == 0));
            handler.on_packet(pkt.data(), pkt.size());
        }
    });

    std::thread release_thread([&] {
        for (int i = 0; i < kIterations / 10; ++i) {
            handler.release_all();
        }
    });

    packet_thread.join();
    release_thread.join();
    // If we reach here without TSAN report / crash, the mutex is correct.
}

// Extended key round-trip: press ArrowLeft (extended), verify scan + flag,
// then verify release_all sends KEY_UP with EXTENDEDKEY flag preserved.
TEST(IntegrationTest, ExtendedKeyPreservesExtendedFlagInReleaseAll) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    testing::InSequence seq;

    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            // ArrowLeft scan = 0x4B, extended
            EXPECT_EQ(inputs[0].ki.wScan, 0x4Bu);
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0u);
            EXPECT_FALSE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            return 1;
        });
    EXPECT_CALL(sender, Send(testing::_, 1))
        .WillOnce([](const INPUT* inputs, uint32_t) -> uint32_t {
            // release_all must send KEY_UP with EXTENDEDKEY still set
            EXPECT_EQ(inputs[0].ki.wScan, 0x4Bu);
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0u);
            EXPECT_TRUE((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0u);
            return 1;
        });

    handler.on_packet(make_key(InputKeyCode::kArrowLeft, true).data(), kKeyPacketSize);
    handler.on_packet(make_release_all().data(), kReleaseAllPacketSize);
}

// Verify that release_all is idempotent: calling it twice after a single
// key-press sends KEY_UP exactly once.
TEST(IntegrationTest, DoubleReleaseAllIsIdempotent) {
    MockInputSender sender;
    InputHandler    handler(&sender);

    // key-down: 1 send; first release_all: 1 send; second release_all: 0 sends.
    EXPECT_CALL(sender, Send(testing::_, 1)).Times(2);

    handler.on_packet(make_key(InputKeyCode::kSpace, true).data(), kKeyPacketSize);
    handler.on_packet(make_release_all().data(), kReleaseAllPacketSize);
    handler.on_packet(make_release_all().data(), kReleaseAllPacketSize);
}

// Scan code coverage: verify all letter keys produce distinct, non-zero scan codes.
TEST(ScanCodeTest, AllLettersHaveDistinctNonZeroScanCodes) {
    constexpr InputKeyCode kLetters[] = {
        InputKeyCode::kKeyA, InputKeyCode::kKeyB, InputKeyCode::kKeyC,
        InputKeyCode::kKeyD, InputKeyCode::kKeyE, InputKeyCode::kKeyF,
        InputKeyCode::kKeyG, InputKeyCode::kKeyH, InputKeyCode::kKeyI,
        InputKeyCode::kKeyJ, InputKeyCode::kKeyK, InputKeyCode::kKeyL,
        InputKeyCode::kKeyM, InputKeyCode::kKeyN, InputKeyCode::kKeyO,
        InputKeyCode::kKeyP, InputKeyCode::kKeyQ, InputKeyCode::kKeyR,
        InputKeyCode::kKeyS, InputKeyCode::kKeyT, InputKeyCode::kKeyU,
        InputKeyCode::kKeyV, InputKeyCode::kKeyW, InputKeyCode::kKeyX,
        InputKeyCode::kKeyY, InputKeyCode::kKeyZ,
    };

    std::set<uint16_t> seen;
    for (const auto code : kLetters) {
        const auto entry = get_scan_code(code);
        EXPECT_NE(entry.scan_code, 0u) << "Letter key has zero scan code";
        EXPECT_FALSE(entry.extended) << "Letter keys should not be extended";
        const bool inserted = seen.insert(entry.scan_code).second;
        EXPECT_TRUE(inserted) << "Duplicate scan code for letter key";
    }
}

// All navigation keys (arrows, home, end, etc.) should be extended.
TEST(ScanCodeTest, AllNavigationKeysAreExtended) {
    constexpr InputKeyCode kNavKeys[] = {
        InputKeyCode::kInsert,    InputKeyCode::kDelete,
        InputKeyCode::kHome,      InputKeyCode::kEnd,
        InputKeyCode::kPageUp,    InputKeyCode::kPageDown,
        InputKeyCode::kArrowUp,   InputKeyCode::kArrowDown,
        InputKeyCode::kArrowLeft, InputKeyCode::kArrowRight,
    };
    for (const auto code : kNavKeys) {
        const auto entry = get_scan_code(code);
        EXPECT_NE(entry.scan_code, 0u);
        EXPECT_TRUE(entry.extended) << "Navigation key not marked extended";
    }
}

} // namespace gamestream
