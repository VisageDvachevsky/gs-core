#pragma once

#include <windows.h>
#include <cstdint>

namespace gamestream {

/// Interface for sending OS-level input events.
///
/// The real implementation calls ::SendInput().
/// Tests inject a MockInputSender to verify correct INPUT structs
/// without actually moving the cursor or pressing keys.
class IInputSender {
public:
    virtual ~IInputSender() = default;

    IInputSender(const IInputSender&)            = delete;
    IInputSender& operator=(const IInputSender&) = delete;
    IInputSender(IInputSender&&)                 = delete;
    IInputSender& operator=(IInputSender&&)      = delete;

    /// Send @p count input events.
    /// @return  Number of events successfully inserted (mirrors ::SendInput return value).
    virtual uint32_t Send(const INPUT* inputs, uint32_t count) = 0;

protected:
    IInputSender() = default;
};

/// Production implementation — thin wrapper around ::SendInput().
class Win32InputSender final : public IInputSender {
public:
    Win32InputSender()  = default;
    ~Win32InputSender() override = default;

    uint32_t Send(const INPUT* inputs, uint32_t count) override {
        return ::SendInput(count, const_cast<INPUT*>(inputs), sizeof(INPUT));
    }
};

} // namespace gamestream
