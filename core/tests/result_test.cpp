/// @file result_test.cpp
/// Unit tests for result.h — Result<T> and VoidResult.
/// Target: 100% line coverage of every executable line in result.h.
///
/// Coverage map (result.h line → test that covers it):
///   detail::Ok<T> { T value }  — SuccessFromRvalue, SuccessFromLvalue
///   detail::Err { string msg } — ErrorFactory, EmptyErrorString
///   Result(T&&)                — SuccessFromRvalue
///   Result(const T&)           — SuccessFromLvalue
///   error() factory            — ErrorFactory, EmptyErrorString, Copy*, Move*
///   has_value() true           — HasValueReturnsTrueOnSuccess
///   has_value() false          — HasValueReturnsFalseOnError
///   operator bool true         — BoolOperatorTrueOnSuccess
///   operator bool false        — BoolOperatorFalseOnError
///   value() &                  — ValueLvalueRefReturnsReference
///   assert in value() &        — LvalueValueOnErrorState (death), covered by success path
///   value() const &            — ValueConstLvalueRef
///   assert in value() const &  — ConstLvalueValueOnErrorState (death)
///   value() &&                 — ValueRvalueMovesOut
///   assert in value() &&       — RvalueValueOnErrorState (death)
///   value_if()/error_if()      — ValueIf*, ErrorIf*
///   error() const accessor     — ErrorMessageAccessor, EmptyErrorString, ErrorOnSuccessState (death)
///   ErrorConstructTag, ctor    — every error() factory call
///   VoidResult()               — DefaultConstructorIsSuccess
///   VoidResult::error()        — VoidResult::ErrorFactory, EmptyVoidErrorString
///   VoidResult::has_value()    — HasValueOn{Success,Error}
///   VoidResult::operator bool  — BoolOn{True,False}
///   VoidResult::error() const  — ErrorAccessorReturnsMessage

#include "result.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using namespace gamestream;

// ===========================================================================
// Result<int> — basic behaviour with a trivial value type
// ===========================================================================

// detail::Ok<T> rvalue branch; Result(T&&)
TEST(ResultTest, SuccessFromRvalue) {
    Result<int> r(42);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

// detail::Ok<T> lvalue branch; Result(const T&)
TEST(ResultTest, SuccessFromLvalue) {
    const int x = 100;
    Result<int> r(x);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 100);
}

// error() factory + ErrorConstructTag private ctor + detail::Err
TEST(ResultTest, ErrorFactory) {
    auto r = Result<int>::error("something failed");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "something failed");
}

// Edge case: empty error string is a valid error state
TEST(ResultTest, EmptyErrorString) {
    auto r = Result<int>::error("");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "");
}

// has_value() — true branch
TEST(ResultTest, HasValueReturnsTrueOnSuccess) {
    Result<int> r(1);
    EXPECT_TRUE(r.has_value());
}

// has_value() — false branch
TEST(ResultTest, HasValueReturnsFalseOnError) {
    auto r = Result<int>::error("err");
    EXPECT_FALSE(r.has_value());
}

// operator bool — true branch
TEST(ResultTest, BoolOperatorTrueOnSuccess) {
    Result<int> r(7);
    EXPECT_TRUE(static_cast<bool>(r));
}

// operator bool — false branch
TEST(ResultTest, BoolOperatorFalseOnError) {
    auto r = Result<int>::error("x");
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultTest, ValueIfReturnsPointerOnSuccess) {
    Result<int> r(7);
    ASSERT_NE(r.value_if(), nullptr);
    EXPECT_EQ(*r.value_if(), 7);
}

TEST(ResultTest, ValueIfReturnsNullOnError) {
    auto r = Result<int>::error("x");
    EXPECT_EQ(r.value_if(), nullptr);
}

TEST(ResultTest, ErrorIfReturnsPointerOnError) {
    auto r = Result<int>::error("x");
    ASSERT_NE(r.error_if(), nullptr);
    EXPECT_EQ(*r.error_if(), "x");
}

TEST(ResultTest, ErrorIfReturnsNullOnSuccess) {
    Result<int> r(7);
    EXPECT_EQ(r.error_if(), nullptr);
}

// value() & — lvalue reference; mutation propagates back
TEST(ResultTest, ValueLvalueRefReturnsReference) {
    Result<int> r(55);
    int& ref = r.value();
    EXPECT_EQ(ref, 55);
    ref = 99;
    EXPECT_EQ(r.value(), 99);
}

// value() const & — const lvalue reference
TEST(ResultTest, ValueConstLvalueRef) {
    const Result<int> r(77);
    const int& ref = r.value();
    EXPECT_EQ(ref, 77);
}

// value() && — rvalue overload moves value out of Result
TEST(ResultTest, ValueRvalueMovesOut) {
    Result<std::vector<int>> r(std::vector<int>{1, 2, 3});
    std::vector<int> moved = std::move(r).value();
    EXPECT_EQ(moved, (std::vector<int>{1, 2, 3}));
}

// error() accessor
TEST(ResultTest, ErrorMessageAccessor) {
    auto r = Result<int>::error("specific error msg");
    const std::string& msg = r.error();
    EXPECT_EQ(msg, "specific error msg");
}

// ===========================================================================
// Result<int> — copy and move semantics
// ===========================================================================

// Copy constructor preserves value
TEST(ResultTest, CopyConstructorPreservesSuccessValue) {
    Result<int> r1(123);
    Result<int> r2 = r1;               // copy
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 123);
    EXPECT_EQ(r1.value(), 123);        // original unchanged
}

// Copy constructor preserves error
TEST(ResultTest, CopyConstructorPreservesError) {
    auto r1 = Result<int>::error("copy me");
    Result<int> r2 = r1;               // copy
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error(), "copy me");
    EXPECT_EQ(r1.error(), "copy me");  // original unchanged
}

// Copy assignment
TEST(ResultTest, CopyAssignmentFromSuccess) {
    Result<int> r1(42);
    auto r2 = Result<int>::error("old");
    r2 = r1;                           // copy-assign success over error
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 42);
}

// Move constructor
TEST(ResultTest, MoveConstructorSuccess) {
    Result<int> r1(88);
    Result<int> r2 = std::move(r1);
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 88);
}

// Move constructor for error
TEST(ResultTest, MoveConstructorError) {
    auto r1 = Result<int>::error("move me");
    Result<int> r2 = std::move(r1);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error(), "move me");
}

// ===========================================================================
// Result<std::string> — T = std::string must work (was broken before Ok<T> fix)
// ===========================================================================

TEST(ResultTest, StringTypeSuccessRoundTrip) {
    Result<std::string> r(std::string("world"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "world");
}

TEST(ResultTest, StringTypeErrorFactory) {
    auto r = Result<std::string>::error("string error");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "string error");
}

// value() && with std::string — the value is moved out
TEST(ResultTest, StringTypeValueRvalueMovesOut) {
    Result<std::string> r(std::string("hello"));
    std::string moved = std::move(r).value();
    EXPECT_EQ(moved, "hello");
}

// ===========================================================================
// Death tests — fail-fast contract for invalid accessors.
// Result<T>::value() / error() and VoidResult::error() abort the process when
// called on the wrong state.
// ===========================================================================

TEST(ResultDeathTest, LvalueValueOnErrorState) {
    EXPECT_DEATH(
        {
            auto r = Result<int>::error("bad");
            [[maybe_unused]] int& v = r.value();
        },
        ".*");
}

TEST(ResultDeathTest, ConstLvalueValueOnErrorState) {
    EXPECT_DEATH(
        {
            const auto r = Result<int>::error("bad");
            [[maybe_unused]] const int& v = r.value();
        },
        ".*");
}

TEST(ResultDeathTest, RvalueValueOnErrorState) {
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto v = Result<int>::error("bad").value();
        },
        ".*");
}

TEST(ResultDeathTest, ErrorOnSuccessState) {
    EXPECT_DEATH(
        {
            Result<int> r(42);
            [[maybe_unused]] const auto& e = r.error();
        },
        ".*");
}

// ===========================================================================
// VoidResult
// ===========================================================================

// VoidResult() default ctor
TEST(VoidResultTest, DefaultConstructorIsSuccess) {
    VoidResult r;
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

// VoidResult::error() factory
TEST(VoidResultTest, ErrorFactory) {
    auto r = VoidResult::error("void failed");
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "void failed");
}

// Edge case: empty error string
TEST(VoidResultTest, EmptyErrorString) {
    auto r = VoidResult::error("");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "");
}

// has_value() — true branch
TEST(VoidResultTest, HasValueOnSuccess) {
    VoidResult r;
    EXPECT_TRUE(r.has_value());
}

// has_value() — false branch
TEST(VoidResultTest, HasValueOnError) {
    auto r = VoidResult::error("err");
    EXPECT_FALSE(r.has_value());
}

// operator bool — true branch
TEST(VoidResultTest, BoolTrueOnSuccess) {
    VoidResult r;
    if (!r) {
        FAIL() << "Expected success VoidResult to be truthy";
    }
}

// operator bool — false branch
TEST(VoidResultTest, BoolFalseOnError) {
    auto r = VoidResult::error("fail");
    if (r) {
        FAIL() << "Expected error VoidResult to be falsy";
    }
}

// error() accessor
TEST(VoidResultTest, ErrorAccessorReturnsMessage) {
    auto r = VoidResult::error("specific error");
    EXPECT_EQ(r.error(), "specific error");
}

TEST(VoidResultDeathTest, ErrorOnSuccessState) {
    EXPECT_DEATH(
        {
            VoidResult r;
            [[maybe_unused]] const auto& e = r.error();
        },
        ".*");
}

// Copy semantics of VoidResult
TEST(VoidResultTest, CopySuccessPreservesState) {
    VoidResult r1;
    VoidResult r2 = r1;
    EXPECT_TRUE(r2.has_value());
}

TEST(VoidResultTest, CopyErrorPreservesMessage) {
    auto r1 = VoidResult::error("copy");
    VoidResult r2 = r1;
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error(), "copy");
}
