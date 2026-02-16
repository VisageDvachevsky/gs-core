#pragma once

#include <string>
#include <variant>
#include <optional>
#include <cassert>

namespace gamestream {

/// Lightweight Result type for error handling without exceptions (C++20 compatible)
/// Usage:
///   Result<Texture> texture = capture.acquire_frame();
///   if (!texture) {
///       log_error("Capture failed: {}", texture.error());
///       return;
///   }
///   process(texture.value());
template<typename T>
class Result {
public:
    // Success constructor
    Result(T&& value) : data_(std::move(value)) {}
    Result(const T& value) : data_(value) {}

    // Error constructor
    static Result error(std::string msg) {
        Result r;
        r.data_ = std::move(msg);
        return r;
    }

    // Check if result is success
    bool has_value() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return has_value(); }

    // Get value (undefined if error)
    T& value() & {
        assert(has_value() && "Result::value() called on error state");
        return std::get<T>(data_);
    }
    const T& value() const & {
        assert(has_value() && "Result::value() called on error state");
        return std::get<T>(data_);
    }
    T&& value() && {
        assert(has_value() && "Result::value() called on error state");
        return std::move(std::get<T>(data_));
    }

    // Get error message (undefined if success)
    const std::string& error() const { return std::get<std::string>(data_); }

private:
    Result() = default;  // Private default constructor for error()
    std::variant<T, std::string> data_;
};

/// Result<void> specialization for operations that don't return a value
class VoidResult {
public:
    // Success constructor
    VoidResult() : error_{} {}

    // Error constructor
    static VoidResult error(std::string msg) {
        VoidResult r;
        r.error_ = std::move(msg);
        return r;
    }

    // Check if result is success
    bool has_value() const { return !error_.has_value(); }
    explicit operator bool() const { return has_value(); }

    // Get error message (undefined if success)
    const std::string& error() const { return *error_; }

private:
    std::optional<std::string> error_;
};

} // namespace gamestream
