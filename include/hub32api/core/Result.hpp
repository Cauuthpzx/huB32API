#pragma once

#include <cassert>
#include <variant>
#include <functional>
#include "hub32api/core/Error.hpp"

namespace hub32api {

// -----------------------------------------------------------------------
// Result<T> — value-or-error return type used throughout the API
// -----------------------------------------------------------------------
template<typename T>
class Result
{
public:
    static Result ok(T value)          { return Result(std::move(value)); }
    static Result fail(ApiError error) { return Result(std::move(error)); }

    bool is_ok()  const noexcept { return std::holds_alternative<T>(m_data); }
    bool is_err() const noexcept { return !is_ok(); }

    /// @brief Returns the contained value; asserts is_ok() in debug builds.
    const T&        value() const { assert(is_ok() && "Result::value() called on error result"); return std::get<T>(m_data); }
    /// @brief Moves the contained value out; asserts is_ok() in debug builds.
    T&&             take()        { assert(is_ok() && "Result::take() called on error result"); return std::move(std::get<T>(m_data)); }
    /// @brief Returns the contained error; asserts is_err() in debug builds.
    const ApiError& error() const { assert(is_err() && "Result::error() called on ok result"); return std::get<ApiError>(m_data); }

    // Map value if ok, propagate error otherwise
    template<typename U, typename F>
    Result<U> map(F&& fn) const
    {
        if (is_ok()) return Result<U>::ok(fn(value()));
        return Result<U>::fail(error());
    }

private:
    explicit Result(T v)         : m_data(std::move(v)) {}
    explicit Result(ApiError e)  : m_data(std::move(e)) {}

    std::variant<T, ApiError> m_data;
};

// Specialisation for void (success/failure only)
template<>
class Result<void>
{
public:
    static Result ok()             { return Result(true);  }
    static Result fail(ApiError e) { return Result(std::move(e)); }

    bool is_ok()  const noexcept { return m_ok; }
    bool is_err() const noexcept { return !m_ok; }
    const ApiError& error() const { return m_error; }

private:
    explicit Result(bool ok)       : m_ok(ok)  {}
    explicit Result(ApiError e)    : m_ok(false), m_error(std::move(e)) {}

    bool     m_ok = true;
    ApiError m_error{ ErrorCode::None };
};

} // namespace hub32api
