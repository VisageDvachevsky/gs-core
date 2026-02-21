/// @file result_test.cpp
/// Unit tests for result.h — Result<T> and VoidResult.
/// Target: 100% line coverage of every executable line in result.h.
///
/// Coverage map (result.h line → test that covers it):
///   22  — SuccessFromRvalue (rvalue constructor)
///   23  — SuccessFromLvalue (lvalue constructor)
///   26–30 — ErrorFactory (error() static method, all 4 lines)
///   33  — HasValueReturns{True,False}OnSuccess/Error
///   34  — BoolOperator{True,False}On{Success,Error}
///   37–40 — ValueLvalueRefReturnsReference (success path) +
///            LvalueValueOnErrorState (death test for assert)
///   41–44 — ValueConstLvalueRef + ConstLvalueValueOnErrorState
///   45–48 — ValueRvalueMovesOut + RvalueValueOnErrorState
///   51  — ErrorMessageAccessor
///   62  — DefaultConstructorIsSuccess
///   65–68 — VoidResult::ErrorFactory
///   72  — VoidResult::HasValueOn{Success,Error}
///   73  — VoidResult::BoolOn{Success,Error}
///   76  — VoidResult::ErrorAccessorReturnsMessage

#include "result.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

using namespace gamestream;

// ===========================================================================
// Result<int> — tests with a trivial value type
// ===========================================================================

// line 22: Result(T&& value) : data_(std::move(value)) {}
TEST(ResultTest, SuccessFromRvalue) {
    Result<int> r(42);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

// line 23: Result(const T& value) : data_(value) {}
TEST(ResultTest, SuccessFromLvalue) {
    const int x = 100;
    Result<int> r(x);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 100);
}

// lines 26–30: static Result error(std::string msg) { ... }
TEST(ResultTest, ErrorFactory) {
    auto r = Result<int>::error("something failed");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "something failed");
}

// line 33: bool has_value() const — true branch
TEST(ResultTest, HasValueReturnsTrueOnSuccess) {
    Result<int> r(1);
    EXPECT_TRUE(r.has_value());
}

// line 33: bool has_value() const — false branch
TEST(ResultTest, HasValueReturnsFalseOnError) {
    auto r = Result<int>::error("err");
    EXPECT_FALSE(r.has_value());
}

// line 34: explicit operator bool() const — true branch
TEST(ResultTest, BoolOperatorTrueOnSuccess) {
    Result<int> r(7);
    EXPECT_TRUE(static_cast<bool>(r));
}

// line 34: explicit operator bool() const — false branch
TEST(ResultTest, BoolOperatorFalseOnError) {
    auto r = Result<int>::error("x");
    EXPECT_FALSE(static_cast<bool>(r));
}

// lines 37–40: T& value() & — lvalue reference overload, success path
TEST(ResultTest, ValueLvalueRefReturnsReference) {
    Result<int> r(55);
    int& ref = r.value();
    EXPECT_EQ(ref, 55);
    ref = 99;                   // Mutate through the reference
    EXPECT_EQ(r.value(), 99);  // Confirm the mutation
}

// lines 41–44: const T& value() const & — const lvalue reference overload
TEST(ResultTest, ValueConstLvalueRef) {
    const Result<int> r(77);
    const int& ref = r.value();
    EXPECT_EQ(ref, 77);
}

// lines 45–48: T&& value() && — rvalue reference overload (moves value out)
TEST(ResultTest, ValueRvalueMovesOut) {
    Result<std::string> r(std::string("hello"));
    std::string moved = std::move(r).value();
    EXPECT_EQ(moved, "hello");
}

// line 51: const std::string& error() const
TEST(ResultTest, ErrorMessageAccessor) {
    auto r = Result<int>::error("specific error msg");
    const std::string& msg = r.error();
    EXPECT_EQ(msg, "specific error msg");
}

// ---------------------------------------------------------------------------
// Result<std::string> — ensures template works with non-trivial types
// ---------------------------------------------------------------------------

TEST(ResultTest, StringValueRoundTrip) {
    Result<std::string> r(std::string("world"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "world");
}

TEST(ResultTest, StringErrorFactory) {
    auto r = Result<std::string>::error("string error");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "string error");
}

// ---------------------------------------------------------------------------
// Death tests — assert fires when value() is called on an error Result.
// In Release builds (NDEBUG defined), EXPECT_DEBUG_DEATH is a no-op —
// lines 38/42/46 are still covered by the success-path tests above.
// ---------------------------------------------------------------------------

// line 38: assert(has_value()) in T& value() &
TEST(ResultDeathTest, LvalueValueOnErrorState) {
    EXPECT_DEBUG_DEATH(
        {
            auto r = Result<int>::error("bad");
            [[maybe_unused]] int& v = r.value();
        },
        ".*");
}

// line 42: assert(has_value()) in const T& value() const &
TEST(ResultDeathTest, ConstLvalueValueOnErrorState) {
    EXPECT_DEBUG_DEATH(
        {
            const auto r = Result<int>::error("bad");
            [[maybe_unused]] const int& v = r.value();
        },
        ".*");
}

// line 46: assert(has_value()) in T&& value() &&
TEST(ResultDeathTest, RvalueValueOnErrorState) {
    EXPECT_DEBUG_DEATH(
        {
            [[maybe_unused]] auto v = Result<int>::error("bad").value();
        },
        ".*");
}

// ===========================================================================
// VoidResult — tests for the void specialization
// ===========================================================================

// line 62: VoidResult() : error_{} {}
TEST(VoidResultTest, DefaultConstructorIsSuccess) {
    VoidResult r;
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

// lines 65–68: static VoidResult error(std::string msg) { ... }
TEST(VoidResultTest, ErrorFactory) {
    auto r = VoidResult::error("void failed");
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "void failed");
}

// line 72: bool has_value() const — true branch
TEST(VoidResultTest, HasValueOnSuccess) {
    VoidResult r;
    EXPECT_TRUE(r.has_value());
}

// line 72: bool has_value() const — false branch
TEST(VoidResultTest, HasValueOnError) {
    auto r = VoidResult::error("err");
    EXPECT_FALSE(r.has_value());
}

// line 73: explicit operator bool() const — true branch
TEST(VoidResultTest, BoolTrueOnSuccess) {
    VoidResult r;
    if (!r) {
        FAIL() << "Expected success VoidResult to be truthy";
    }
}

// line 73: explicit operator bool() const — false branch
TEST(VoidResultTest, BoolFalseOnError) {
    auto r = VoidResult::error("fail");
    if (r) {
        FAIL() << "Expected error VoidResult to be falsy";
    }
}

// line 76: const std::string& error() const { return *error_; }
TEST(VoidResultTest, ErrorAccessorReturnsMessage) {
    auto r = VoidResult::error("specific error");
    EXPECT_EQ(r.error(), "specific error");
}
