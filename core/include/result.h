#pragma once

#include <string>
#include <variant>
#include <optional>
#include <cassert>
#include <cstdlib>

namespace gamestream {

/// Internal wrapper types for Result<T>.
/// Wrapping T in Ok<T> ensures the variant always holds distinct alternatives,
/// which makes Result<std::string> and Result<any T> well-formed.
namespace detail {

template<typename T>
struct Ok { T value; };

struct Err { std::string message; };

} // namespace detail

/// Lightweight Result type for error handling without exceptions (C++20 compatible).
/// Works correctly for any T, including T = std::string and non-default-constructible T.
///
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
    // Success constructors
    Result(T&& value) : data_(detail::Ok<T>{std::move(value)}) {}
    Result(const T& value) : data_(detail::Ok<T>{value}) {}

    // Error factory — never default-constructs T
    static Result error(std::string msg) {
        return Result(ErrorConstructTag{}, std::move(msg));
    }

    // Check if result is success
    bool has_value() const { return std::holds_alternative<detail::Ok<T>>(data_); }
    explicit operator bool() const { return has_value(); }

    // Non-throwing accessors for hot paths where exceptions are unacceptable.
    T* value_if() noexcept {
        auto* ok = std::get_if<detail::Ok<T>>(&data_);
        return ok ? &ok->value : nullptr;
    }
    const T* value_if() const noexcept {
        const auto* ok = std::get_if<detail::Ok<T>>(&data_);
        return ok ? &ok->value : nullptr;
    }
    std::string* error_if() noexcept {
        auto* err = std::get_if<detail::Err>(&data_);
        return err ? &err->message : nullptr;
    }
    const std::string* error_if() const noexcept {
        const auto* err = std::get_if<detail::Err>(&data_);
        return err ? &err->message : nullptr;
    }

    // Get value (assert fires on error state)
    T& value() & {
        auto* ok = std::get_if<detail::Ok<T>>(&data_);
        if (!ok) {
            assert(false && "Result::value() called on error state");
            std::abort();
        }
        return ok->value;
    }
    const T& value() const & {
        const auto* ok = std::get_if<detail::Ok<T>>(&data_);
        if (!ok) {
            assert(false && "Result::value() called on error state");
            std::abort();
        }
        return ok->value;
    }
    T&& value() && {
        auto* ok = std::get_if<detail::Ok<T>>(&data_);
        if (!ok) {
            assert(false && "Result::value() called on error state");
            std::abort();
        }
        return std::move(ok->value);
    }

    // Get error message (fail-fast on success state)
    const std::string& error() const {
        const auto* err = std::get_if<detail::Err>(&data_);
        if (!err) {
            assert(false && "Result::error() called on success state");
            std::abort();
        }
        return err->message;
    }

private:
    struct ErrorConstructTag {};
    Result(ErrorConstructTag, std::string msg) : data_(detail::Err{std::move(msg)}) {}

    std::variant<detail::Ok<T>, detail::Err> data_;
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

    // Get error message (fail-fast on success state)
    const std::string& error() const {
        if (!error_) {
            assert(false && "VoidResult::error() called on success state");
            std::abort();
        }
        return *error_;
    }

private:
    std::optional<std::string> error_;
};

} // namespace gamestream
