#pragma once

#include <variant>
#include <functional>
#include "veyon32api/core/Error.hpp"

namespace veyon32api {

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

    const T&        value() const { return std::get<T>(m_data); }
    T&&             take()        { return std::move(std::get<T>(m_data)); }
    const ApiError& error() const { return std::get<ApiError>(m_data); }

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

} // namespace veyon32api
