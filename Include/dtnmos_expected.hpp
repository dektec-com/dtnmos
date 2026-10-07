// #*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_expected.hpp *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API's own std::expected, for a program on C++20
//
// SPDX-License-Identifier: BSD-3-Clause
//
// C++20 has no std::expected. This header gives the C++ API one of its own,
// Detail::OwnExpected, with the whole interface of std::expected of C++23: the same
// constructors, assignments, observers, monadic functions and comparisons, with the
// same names. dtnmos.hpp makes DtNmos::Expected and DtNmos::Status of it where the
// standard library has no std::expected, so that a program that uses them compiles and
// works the same under C++20 and C++23. A program does not include this header itself.
//
// OwnUnexpected, OwnUnexpect and OwnBadExpectedAccess are std::unexpected,
// std::unexpect and std::bad_expected_access. The names are not those of DtNmos, so
// that code in Detail that writes Expected or Unexpected finds those of DtNmos.
// Without exceptions, value() of an Expected that holds an error calls
// std::terminate().

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

// The tag that GCC and Clang put into the name, for the linker, of each function that
// returns a Detail::OwnExpected, so that it differs from that of the same function where
// Expected is a std::expected. A program whose sources include the headers under C++20
// and under C++23 then links a copy of each function per standard, rather than one of
// them for both. MSVC puts the type that a function returns into its name anyway.
#if defined(__GNUC__)
    #define DTNMOS_DETAIL_OWN_EXPECTED [[gnu::abi_tag("dtnmos_own_expected")]]
#else
    #define DTNMOS_DETAIL_OWN_EXPECTED
#endif

namespace DtNmos::Detail
{

template <typename E> class OwnUnexpected;
template <typename T, typename E> class DTNMOS_DETAIL_OWN_EXPECTED OwnExpected;

// True when T is a specialization of Unexpected.
template <typename T> struct IsOwnUnexpectedType : std::false_type
{
};
template <typename E> struct IsOwnUnexpectedType<OwnUnexpected<E>> : std::true_type
{
};
template <typename T>
inline constexpr bool IsOwnUnexpected = IsOwnUnexpectedType<T>::value;

// True when T is a specialization of Expected.
template <typename T> struct IsOwnExpectedType : std::false_type
{
};
template <typename T, typename E>
struct IsOwnExpectedType<OwnExpected<T, E>> : std::true_type
{
};
template <typename T> inline constexpr bool IsOwnExpected = IsOwnExpectedType<T>::value;

// True when E can be the error of an Expected: an object type that is not an array, not
// const or volatile, and not an Unexpected.
template <typename E>
inline constexpr bool IsErrorType =
    std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E> &&
    !std::is_volatile_v<E> && !IsOwnUnexpected<E>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= OwnUnexpect +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The type of OwnUnexpect.
struct OwnUnexpectTag
{
    explicit OwnUnexpectTag() = default;
};

// Asks a constructor of an Expected to make the error in place, from the arguments that
// follow it, as std::unexpect does.
inline constexpr OwnUnexpectTag OwnUnexpect{};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= OwnUnexpected +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// An error of type E, as std::unexpected. An Expected is made from it, or assigned it,
// to hold the error.
template <typename E> class OwnUnexpected
{
    static_assert(IsErrorType<E>,
                  "OwnUnexpected<E> needs an object type E that is not an "
                  "array, const, volatile or an OwnUnexpected");

  public:
    constexpr OwnUnexpected(const OwnUnexpected&) = default;
    constexpr OwnUnexpected(OwnUnexpected&&) = default;

    // Holds the error made from Failed.
    template <typename G = E>
        requires(!std::is_same_v<std::remove_cvref_t<G>, OwnUnexpected> &&
                 !std::is_same_v<std::remove_cvref_t<G>, std::in_place_t> &&
                 std::is_constructible_v<E, G>)
    constexpr explicit OwnUnexpected(G&& Failed) : Failure(std::forward<G>(Failed))
    {
    }

    // Holds the error made in place from Arguments.
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit OwnUnexpected(std::in_place_t, Args&&... Arguments)
        : Failure(std::forward<Args>(Arguments)...)
    {
    }

    // Holds the error made in place from List and Arguments.
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U>&, Args...>
    constexpr explicit OwnUnexpected(std::in_place_t, std::initializer_list<U> List,
                                     Args&&... Arguments)
        : Failure(List, std::forward<Args>(Arguments)...)
    {
    }

    constexpr OwnUnexpected& operator=(const OwnUnexpected&) = default;
    constexpr OwnUnexpected& operator=(OwnUnexpected&&) = default;

    // Returns the error.
    constexpr const E& error() const& noexcept { return Failure; }
    constexpr E& error() & noexcept { return Failure; }
    constexpr const E&& error() const&& noexcept { return std::move(Failure); }
    constexpr E&& error() && noexcept { return std::move(Failure); }

    // Swaps the errors of this and Other.
    constexpr void swap(OwnUnexpected& Other) noexcept(std::is_nothrow_swappable_v<E>)
    {
        static_assert(std::is_swappable_v<E>, "swap() needs an E that can be swapped");
        using std::swap;
        swap(Failure, Other.Failure);
    }

    // True when the errors of X and Y compare equal.
    template <typename E2>
    friend constexpr bool operator==(const OwnUnexpected& X, const OwnUnexpected<E2>& Y)
    {
        return X.error() == Y.error();
    }

    // Swaps the errors of X and Y.
    friend constexpr void swap(OwnUnexpected& X,
                               OwnUnexpected& Y) noexcept(noexcept(X.swap(Y)))
        requires std::is_swappable_v<E>
    {
        X.swap(Y);
    }

  private:
    E Failure; // The error
};

template <typename E> OwnUnexpected(E) -> OwnUnexpected<E>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= OwnBadExpectedAccess +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//

template <typename E> class OwnBadExpectedAccess;

// The base of every BadExpectedAccess, as std::bad_expected_access<void>, so that a
// program catches them all with one handler.
template <> class OwnBadExpectedAccess<void> : public std::exception
{
  public:
    // Says what went wrong, in English.
    const char* what() const noexcept override
    {
        return "The value of a DtNmos::Expected was read while it holds an error.";
    }

  protected:
    OwnBadExpectedAccess() noexcept = default;
    OwnBadExpectedAccess(const OwnBadExpectedAccess&) noexcept = default;
    OwnBadExpectedAccess(OwnBadExpectedAccess&&) noexcept = default;
    OwnBadExpectedAccess& operator=(const OwnBadExpectedAccess&) noexcept = default;
    OwnBadExpectedAccess& operator=(OwnBadExpectedAccess&&) noexcept = default;
    ~OwnBadExpectedAccess() override = default;
};

// What value() of an Expected that holds an error throws, as std::bad_expected_access:
// it holds that error.
template <typename E> class OwnBadExpectedAccess : public OwnBadExpectedAccess<void>
{
  public:
    // Holds the error Failed.
    explicit OwnBadExpectedAccess(E Failed) : Failure(std::move(Failed)) {}

    // Returns the error.
    const E& error() const& noexcept { return Failure; }
    E& error() & noexcept { return Failure; }
    const E&& error() const&& noexcept { return std::move(Failure); }
    E&& error() && noexcept { return std::move(Failure); }

    // Says what went wrong, in English.
    const char* what() const noexcept override
    {
        return OwnBadExpectedAccess<void>::what();
    }

  private:
    E Failure; // The error of the Expected
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What OwnExpected shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// Ends value() of an Expected that holds the error Failed: it throws a
// BadExpectedAccess with the error, or calls std::terminate() in a program built
// without exceptions.
template <typename E> [[noreturn]] void ThrowBadExpectedAccess(E&& Failed)
{
#if defined(__cpp_exceptions)
    throw OwnBadExpectedAccess<std::decay_t<E>>(std::forward<E>(Failed));
#else
    (void)Failed;
    std::terminate();
#endif
}

// Replaces OldObject, the value or the error that an Expected holds, by NewObject made
// from Arguments, as std::expected does when an assignment changes what it holds. When
// making NewObject throws, OldObject is put back, so that the Expected always holds one
// of the two.
template <typename New, typename Old, typename... Args>
constexpr void ReinitExpected(New& NewObject, Old& OldObject, Args&&... Arguments)
{
    if constexpr (std::is_nothrow_constructible_v<New, Args...>)
    {
        std::destroy_at(std::addressof(OldObject));
        std::construct_at(std::addressof(NewObject), std::forward<Args>(Arguments)...);
    }
    else if constexpr (std::is_nothrow_move_constructible_v<New>)
    {
        New Made(std::forward<Args>(Arguments)...);
        std::destroy_at(std::addressof(OldObject));
        std::construct_at(std::addressof(NewObject), std::move(Made));
    }
    else
    {
        Old Kept(std::move(OldObject));
        std::destroy_at(std::addressof(OldObject));
#if defined(__cpp_exceptions)
        try
        {
            std::construct_at(std::addressof(NewObject),
                              std::forward<Args>(Arguments)...);
        }
        catch (...)
        {
            std::construct_at(std::addressof(OldObject), std::move(Kept));
            throw;
        }
#else
        std::construct_at(std::addressof(NewObject), std::forward<Args>(Arguments)...);
#endif
    }
}

// Ask the private constructors of an Expected to hold what a function returns, as the
// value or as the error, for transform() and transform_error().
struct InvokeValueTag
{
};
struct InvokeErrorTag
{
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= OwnExpected +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// A value of type T, or an error of type E, as std::expected<T, E>.
template <typename T, typename E> class DTNMOS_DETAIL_OWN_EXPECTED OwnExpected
{
    static_assert(!std::is_reference_v<T> && !std::is_function_v<T> &&
                      !std::is_array_v<T> &&
                      !std::is_same_v<std::remove_cv_t<T>, std::in_place_t> &&
                      !std::is_same_v<std::remove_cv_t<T>, OwnUnexpectTag> &&
                      !IsOwnUnexpected<std::remove_cv_t<T>>,
                  "OwnExpected<T, E> needs a T that is an object type, not an array, "
                  "in_place_t, OwnUnexpectTag or OwnUnexpected");
    static_assert(IsErrorType<E>,
                  "OwnExpected<T, E> needs an object type E that is not an "
                  "array, const, volatile or an OwnUnexpected");

    // True when an Expected<U, G>, whose value and error are given as UF and GF, may
    // convert to this Expected: T is made from UF, E from GF, and neither T nor
    // Unexpected<E> can be made from the Expected<U, G> itself.
    template <typename U, typename G, typename UF, typename GF>
    static constexpr bool CanConvertFrom =
        std::is_constructible_v<T, UF> && std::is_constructible_v<E, GF> &&
        (std::is_same_v<std::remove_cv_t<T>, bool> ||
         (!std::is_constructible_v<T, OwnExpected<U, G>&> &&
          !std::is_constructible_v<T, OwnExpected<U, G>> &&
          !std::is_constructible_v<T, const OwnExpected<U, G>&> &&
          !std::is_constructible_v<T, const OwnExpected<U, G>> &&
          !std::is_convertible_v<OwnExpected<U, G>&, T> &&
          !std::is_convertible_v<OwnExpected<U, G>&&, T> &&
          !std::is_convertible_v<const OwnExpected<U, G>&, T> &&
          !std::is_convertible_v<const OwnExpected<U, G>&&, T>)) &&
        !std::is_constructible_v<OwnUnexpected<E>, OwnExpected<U, G>&> &&
        !std::is_constructible_v<OwnUnexpected<E>, OwnExpected<U, G>> &&
        !std::is_constructible_v<OwnUnexpected<E>, const OwnExpected<U, G>&> &&
        !std::is_constructible_v<OwnUnexpected<E>, const OwnExpected<U, G>>;

  public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = OwnUnexpected<E>;

    template <typename U> using rebind = OwnExpected<U, error_type>;

    // Holds a value made with no arguments.
    constexpr OwnExpected()
        requires std::is_default_constructible_v<T>
        : Held(), HoldsValue(true)
    {
    }

    // Holds a copy of what Other holds.
    constexpr OwnExpected(const OwnExpected& Other)
        requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>)
        : HoldsValue(Other.HoldsValue)
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Held), Other.Held);
        }
        else
        {
            std::construct_at(std::addressof(Failure), Other.Failure);
        }
    }

    // Holds what Other holds, moved out of it.
    constexpr OwnExpected(OwnExpected&& Other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>)
        requires(std::is_move_constructible_v<T> && std::is_move_constructible_v<E>)
        : HoldsValue(Other.HoldsValue)
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Held), std::move(Other.Held));
        }
        else
        {
            std::construct_at(std::addressof(Failure), std::move(Other.Failure));
        }
    }

    // Holds the value or the error of Other, converted to T or E.
    template <typename U, typename G>
        requires CanConvertFrom<U, G, const U&, const G&>
    constexpr explicit(!std::is_convertible_v<const U&, T> ||
                       !std::is_convertible_v<const G&, E>)
        OwnExpected(const OwnExpected<U, G>& Other)
        : HoldsValue(Other.has_value())
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Held), *Other);
        }
        else
        {
            std::construct_at(std::addressof(Failure), Other.error());
        }
    }

    // Holds the value or the error of Other, moved out of it and converted to T or E.
    template <typename U, typename G>
        requires CanConvertFrom<U, G, U, G>
    constexpr explicit(!std::is_convertible_v<U, T> || !std::is_convertible_v<G, E>)
        OwnExpected(OwnExpected<U, G>&& Other)
        : HoldsValue(Other.has_value())
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Held), std::move(*Other));
        }
        else
        {
            std::construct_at(std::addressof(Failure), std::move(Other.error()));
        }
    }

    // Holds the value made from Value, e.g. a struct a function returns.
    template <typename U = T>
        requires(!std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
                 !std::is_same_v<std::remove_cvref_t<U>, OwnExpected> &&
                 !IsOwnUnexpected<std::remove_cvref_t<U>> &&
                 std::is_constructible_v<T, U> &&
                 (!std::is_same_v<std::remove_cv_t<T>, bool> ||
                  !IsOwnExpected<std::remove_cvref_t<U>>))
    constexpr explicit(!std::is_convertible_v<U, T>) OwnExpected(U&& Value)
        : Held(std::forward<U>(Value)), HoldsValue(true)
    {
    }

    // Holds the error of Failed.
    template <typename G>
        requires std::is_constructible_v<E, const G&>
    constexpr explicit(!std::is_convertible_v<const G&, E>)
        OwnExpected(const OwnUnexpected<G>& Failed)
        : Failure(Failed.error()), HoldsValue(false)
    {
    }

    // Holds the error of Failed, moved out of it.
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr explicit(!std::is_convertible_v<G, E>)
        OwnExpected(OwnUnexpected<G>&& Failed)
        : Failure(std::move(Failed.error())), HoldsValue(false)
    {
    }

    // Holds the value made in place from Arguments.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit OwnExpected(std::in_place_t, Args&&... Arguments)
        : Held(std::forward<Args>(Arguments)...), HoldsValue(true)
    {
    }

    // Holds the value made in place from List and Arguments.
    template <typename U, typename... Args>
        requires std::is_constructible_v<T, std::initializer_list<U>&, Args...>
    constexpr explicit OwnExpected(std::in_place_t, std::initializer_list<U> List,
                                   Args&&... Arguments)
        : Held(List, std::forward<Args>(Arguments)...), HoldsValue(true)
    {
    }

    // Holds the error made in place from Arguments.
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit OwnExpected(OwnUnexpectTag, Args&&... Arguments)
        : Failure(std::forward<Args>(Arguments)...), HoldsValue(false)
    {
    }

    // Holds the error made in place from List and Arguments.
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U>&, Args...>
    constexpr explicit OwnExpected(OwnUnexpectTag, std::initializer_list<U> List,
                                   Args&&... Arguments)
        : Failure(List, std::forward<Args>(Arguments)...), HoldsValue(false)
    {
    }

    constexpr ~OwnExpected()
    {
        if (HoldsValue)
        {
            std::destroy_at(std::addressof(Held));
        }
        else
        {
            std::destroy_at(std::addressof(Failure));
        }
    }

    // Holds a copy of what Other holds.
    constexpr OwnExpected& operator=(const OwnExpected& Other)
        requires(std::is_copy_assignable_v<T> && std::is_copy_constructible_v<T> &&
                 std::is_copy_assignable_v<E> && std::is_copy_constructible_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    {
        if (HoldsValue && Other.HoldsValue)
        {
            Held = Other.Held;
        }
        else if (HoldsValue)
        {
            ReinitExpected(Failure, Held, Other.Failure);
        }
        else if (Other.HoldsValue)
        {
            ReinitExpected(Held, Failure, Other.Held);
        }
        else
        {
            Failure = Other.Failure;
        }
        HoldsValue = Other.HoldsValue;
        return *this;
    }

    // Holds what Other holds, moved out of it.
    constexpr OwnExpected& operator=(OwnExpected&& Other) noexcept(
        std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_assignable_v<E> && std::is_nothrow_move_constructible_v<E>)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T> &&
                 std::is_move_constructible_v<E> && std::is_move_assignable_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    {
        if (HoldsValue && Other.HoldsValue)
        {
            Held = std::move(Other.Held);
        }
        else if (HoldsValue)
        {
            ReinitExpected(Failure, Held, std::move(Other.Failure));
        }
        else if (Other.HoldsValue)
        {
            ReinitExpected(Held, Failure, std::move(Other.Held));
        }
        else
        {
            Failure = std::move(Other.Failure);
        }
        HoldsValue = Other.HoldsValue;
        return *this;
    }

    // Holds the value made from Value.
    template <typename U = T>
        requires(!std::is_same_v<std::remove_cvref_t<U>, OwnExpected> &&
                 !IsOwnUnexpected<std::remove_cvref_t<U>> &&
                 std::is_constructible_v<T, U> && std::is_assignable_v<T&, U> &&
                 (std::is_nothrow_constructible_v<T, U> ||
                  std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr OwnExpected& operator=(U&& Value)
    {
        if (HoldsValue)
        {
            Held = std::forward<U>(Value);
        }
        else
        {
            ReinitExpected(Held, Failure, std::forward<U>(Value));
            HoldsValue = true;
        }
        return *this;
    }

    // Holds the error of Failed.
    template <typename G>
        requires(std::is_constructible_v<E, const G&> &&
                 std::is_assignable_v<E&, const G&> &&
                 (std::is_nothrow_constructible_v<E, const G&> ||
                  std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr OwnExpected& operator=(const OwnUnexpected<G>& Failed)
    {
        if (HoldsValue)
        {
            ReinitExpected(Failure, Held, Failed.error());
            HoldsValue = false;
        }
        else
        {
            Failure = Failed.error();
        }
        return *this;
    }

    // Holds the error of Failed, moved out of it.
    template <typename G>
        requires(std::is_constructible_v<E, G> && std::is_assignable_v<E&, G> &&
                 (std::is_nothrow_constructible_v<E, G> ||
                  std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr OwnExpected& operator=(OwnUnexpected<G>&& Failed)
    {
        if (HoldsValue)
        {
            ReinitExpected(Failure, Held, std::move(Failed.error()));
            HoldsValue = false;
        }
        else
        {
            Failure = std::move(Failed.error());
        }
        return *this;
    }

    // Returns what Function returns for the value, an Expected with the same E; or, when
    // this holds an error, an Expected of that type that holds the error.
    template <typename F>
        requires std::is_constructible_v<E, E&>
    constexpr auto and_then(F&& Function) &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, T&>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function), Held);
        }
        return U(OwnUnexpect, Failure);
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&>
    constexpr auto and_then(F&& Function) const&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function), Held);
        }
        return U(OwnUnexpect, Failure);
    }
    template <typename F>
        requires std::is_constructible_v<E, E&&>
    constexpr auto and_then(F&& Function) &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, T&&>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function), std::move(Held));
        }
        return U(OwnUnexpect, std::move(Failure));
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&&>
    constexpr auto and_then(F&& Function) const&&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T&&>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function), std::move(Held));
        }
        return U(OwnUnexpect, std::move(Failure));
    }

    // Holds the value made in place from Arguments, in place of what this held.
    template <typename... Args>
        requires std::is_nothrow_constructible_v<T, Args...>
    constexpr T& emplace(Args&&... Arguments) noexcept
    {
        if (HoldsValue)
        {
            std::destroy_at(std::addressof(Held));
        }
        else
        {
            std::destroy_at(std::addressof(Failure));
            HoldsValue = true;
        }
        return *std::construct_at(std::addressof(Held), std::forward<Args>(Arguments)...);
    }

    // Holds the value made in place from List and Arguments, in place of what this held.
    template <typename U, typename... Args>
        requires std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args...>
    constexpr T& emplace(std::initializer_list<U> List, Args&&... Arguments) noexcept
    {
        if (HoldsValue)
        {
            std::destroy_at(std::addressof(Held));
        }
        else
        {
            std::destroy_at(std::addressof(Failure));
            HoldsValue = true;
        }
        return *std::construct_at(std::addressof(Held), List,
                                  std::forward<Args>(Arguments)...);
    }

    // Returns the error. This must hold one.
    constexpr const E& error() const& noexcept { return Failure; }
    constexpr E& error() & noexcept { return Failure; }
    constexpr const E&& error() const&& noexcept { return std::move(Failure); }
    constexpr E&& error() && noexcept { return std::move(Failure); }

    // Returns true when this holds a value, false when it holds an error.
    constexpr bool has_value() const noexcept { return HoldsValue; }

    // Returns this Expected's value in what Function returns, an Expected with the same
    // T; or, when this holds an error, what Function returns for the error.
    template <typename F>
        requires std::is_constructible_v<T, T&>
    constexpr auto or_else(F&& Function) &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G(std::in_place, Held);
        }
        return std::invoke(std::forward<F>(Function), Failure);
    }
    template <typename F>
        requires std::is_constructible_v<T, const T&>
    constexpr auto or_else(F&& Function) const&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G(std::in_place, Held);
        }
        return std::invoke(std::forward<F>(Function), Failure);
    }
    template <typename F>
        requires std::is_constructible_v<T, T&&>
    constexpr auto or_else(F&& Function) &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E&&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G(std::in_place, std::move(Held));
        }
        return std::invoke(std::forward<F>(Function), std::move(Failure));
    }
    template <typename F>
        requires std::is_constructible_v<T, const T&&>
    constexpr auto or_else(F&& Function) const&&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E&&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G(std::in_place, std::move(Held));
        }
        return std::invoke(std::forward<F>(Function), std::move(Failure));
    }

    // Swaps what this and Other hold.
    constexpr void swap(OwnExpected& Other) noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T> &&
        std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>)
        requires(std::is_swappable_v<T> && std::is_swappable_v<E> &&
                 std::is_move_constructible_v<T> && std::is_move_constructible_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    {
        using std::swap;
        if (HoldsValue && Other.HoldsValue)
        {
            swap(Held, Other.Held);
        }
        else if (!HoldsValue && !Other.HoldsValue)
        {
            swap(Failure, Other.Failure);
        }
        else if (!HoldsValue)
        {
            Other.swap(*this);
        }
        else
        {
            SwapValueWithError(Other);
        }
    }

    // Returns Function's result for the value, in an Expected with the same E; or, when
    // this holds an error, an Expected of that type that holds the error.
    template <typename F>
        requires std::is_constructible_v<E, E&>
    constexpr auto transform(F&& Function) &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, T&>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, Failure);
        }
        if constexpr (std::is_void_v<U>)
        {
            std::invoke(std::forward<F>(Function), Held);
            return OwnExpected<U, E>();
        }
        else
        {
            return OwnExpected<U, E>(InvokeValueTag{}, std::forward<F>(Function), Held);
        }
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&>
    constexpr auto transform(F&& Function) const&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, const T&>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, Failure);
        }
        if constexpr (std::is_void_v<U>)
        {
            std::invoke(std::forward<F>(Function), Held);
            return OwnExpected<U, E>();
        }
        else
        {
            return OwnExpected<U, E>(InvokeValueTag{}, std::forward<F>(Function), Held);
        }
    }
    template <typename F>
        requires std::is_constructible_v<E, E&&>
    constexpr auto transform(F&& Function) &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, T&&>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, std::move(Failure));
        }
        if constexpr (std::is_void_v<U>)
        {
            std::invoke(std::forward<F>(Function), std::move(Held));
            return OwnExpected<U, E>();
        }
        else
        {
            return OwnExpected<U, E>(InvokeValueTag{}, std::forward<F>(Function),
                                     std::move(Held));
        }
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&&>
    constexpr auto transform(F&& Function) const&&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, const T&&>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, std::move(Failure));
        }
        if constexpr (std::is_void_v<U>)
        {
            std::invoke(std::forward<F>(Function), std::move(Held));
            return OwnExpected<U, E>();
        }
        else
        {
            return OwnExpected<U, E>(InvokeValueTag{}, std::forward<F>(Function),
                                     std::move(Held));
        }
    }

    // Returns the value in an Expected with the error type that Function returns; or,
    // when this holds an error, Function's result for it as the error.
    template <typename F>
        requires std::is_constructible_v<T, T&>
    constexpr auto transform_error(F&& Function) &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>(std::in_place, Held);
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function), Failure);
    }
    template <typename F>
        requires std::is_constructible_v<T, const T&>
    constexpr auto transform_error(F&& Function) const&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>(std::in_place, Held);
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function), Failure);
    }
    template <typename F>
        requires std::is_constructible_v<T, T&&>
    constexpr auto transform_error(F&& Function) &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E&&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>(std::in_place, std::move(Held));
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function),
                                 std::move(Failure));
    }
    template <typename F>
        requires std::is_constructible_v<T, const T&&>
    constexpr auto transform_error(F&& Function) const&&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E&&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>(std::in_place, std::move(Held));
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function),
                                 std::move(Failure));
    }

    // Returns the value. When this holds an error, it throws a BadExpectedAccess with
    // the error, or, without exceptions, calls std::terminate().
    constexpr const T& value() const&
    {
        static_assert(std::is_copy_constructible_v<E>, "value() needs an E to copy");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(std::as_const(Failure));
        }
        return Held;
    }
    constexpr T& value() &
    {
        static_assert(std::is_copy_constructible_v<E>, "value() needs an E to copy");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(std::as_const(Failure));
        }
        return Held;
    }
    constexpr const T&& value() const&&
    {
        static_assert(std::is_copy_constructible_v<E> &&
                          std::is_constructible_v<E, const E&&>,
                      "value() needs an E to copy and to move");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(std::move(Failure));
        }
        return std::move(Held);
    }
    constexpr T&& value() &&
    {
        static_assert(std::is_copy_constructible_v<E> && std::is_constructible_v<E, E&&>,
                      "value() needs an E to copy and to move");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(std::move(Failure));
        }
        return std::move(Held);
    }

    // Returns the value, or Value, converted to T, when this holds an error.
    template <typename U> constexpr T value_or(U&& Value) const&
    {
        static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U, T>,
                      "value_or() needs a T to copy, and a Value that converts to T");
        return HoldsValue ? Held : static_cast<T>(std::forward<U>(Value));
    }
    template <typename U> constexpr T value_or(U&& Value) &&
    {
        static_assert(std::is_move_constructible_v<T> && std::is_convertible_v<U, T>,
                      "value_or() needs a T to move, and a Value that converts to T");
        return HoldsValue ? std::move(Held) : static_cast<T>(std::forward<U>(Value));
    }

    // Returns true when this holds a value.
    constexpr explicit operator bool() const noexcept { return HoldsValue; }

    // Returns the value. This must hold one.
    constexpr const T& operator*() const& noexcept { return Held; }
    constexpr T& operator*() & noexcept { return Held; }
    constexpr const T&& operator*() const&& noexcept { return std::move(Held); }
    constexpr T&& operator*() && noexcept { return std::move(Held); }

    // Points at the value. This must hold one.
    constexpr const T* operator->() const noexcept { return std::addressof(Held); }
    constexpr T* operator->() noexcept { return std::addressof(Held); }

    // True when X and Y both hold values that compare equal, or both errors that do.
    template <typename T2, typename E2>
        requires(!std::is_void_v<T2>)
    friend constexpr bool operator==(const OwnExpected& X, const OwnExpected<T2, E2>& Y)
    {
        if (X.has_value() != Y.has_value())
        {
            return false;
        }
        return X.has_value() ? static_cast<bool>(*X == *Y)
                             : static_cast<bool>(X.error() == Y.error());
    }

    // True when X holds a value that compares equal to Value.
    template <typename T2>
        requires(!IsOwnExpected<T2> && !IsOwnUnexpected<T2>)
    friend constexpr bool operator==(const OwnExpected& X, const T2& Value)
    {
        return X.has_value() && static_cast<bool>(*X == Value);
    }

    // True when X holds an error that compares equal to that of Y.
    template <typename E2>
    friend constexpr bool operator==(const OwnExpected& X, const OwnUnexpected<E2>& Y)
    {
        return !X.has_value() && static_cast<bool>(X.error() == Y.error());
    }

    // Swaps what X and Y hold.
    friend constexpr void swap(OwnExpected& X,
                               OwnExpected& Y) noexcept(noexcept(X.swap(Y)))
        requires(std::is_swappable_v<T> && std::is_swappable_v<E> &&
                 std::is_move_constructible_v<T> && std::is_move_constructible_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    {
        X.swap(Y);
    }

  private:
    template <typename, typename> friend class OwnExpected;

    // Holds what Function returns for Arguments, as the value.
    template <typename F, typename... Args>
    constexpr OwnExpected(InvokeValueTag, F&& Function, Args&&... Arguments)
        : Held(std::invoke(std::forward<F>(Function), std::forward<Args>(Arguments)...)),
          HoldsValue(true)
    {
    }

    // Holds what Function returns for Arguments, as the error.
    template <typename F, typename... Args>
    constexpr OwnExpected(InvokeErrorTag, F&& Function, Args&&... Arguments)
        : Failure(
              std::invoke(std::forward<F>(Function), std::forward<Args>(Arguments)...)),
          HoldsValue(false)
    {
    }

    // Swaps the value of this with the error of Other, for swap(). When moving one of the
    // two throws, both are put back as they were.
    constexpr void SwapValueWithError(OwnExpected& Other)
    {
        if constexpr (std::is_nothrow_move_constructible_v<E>)
        {
            E Kept(std::move(Other.Failure));
            std::destroy_at(std::addressof(Other.Failure));
#if defined(__cpp_exceptions)
            try
            {
                std::construct_at(std::addressof(Other.Held), std::move(Held));
                std::destroy_at(std::addressof(Held));
                std::construct_at(std::addressof(Failure), std::move(Kept));
            }
            catch (...)
            {
                std::construct_at(std::addressof(Other.Failure), std::move(Kept));
                throw;
            }
#else
            std::construct_at(std::addressof(Other.Held), std::move(Held));
            std::destroy_at(std::addressof(Held));
            std::construct_at(std::addressof(Failure), std::move(Kept));
#endif
        }
        else
        {
            T Kept(std::move(Held));
            std::destroy_at(std::addressof(Held));
#if defined(__cpp_exceptions)
            try
            {
                std::construct_at(std::addressof(Failure), std::move(Other.Failure));
                std::destroy_at(std::addressof(Other.Failure));
                std::construct_at(std::addressof(Other.Held), std::move(Kept));
            }
            catch (...)
            {
                std::construct_at(std::addressof(Held), std::move(Kept));
                throw;
            }
#else
            std::construct_at(std::addressof(Failure), std::move(Other.Failure));
            std::destroy_at(std::addressof(Other.Failure));
            std::construct_at(std::addressof(Other.Held), std::move(Kept));
#endif
        }
        HoldsValue = false;
        Other.HoldsValue = true;
    }

    union
    {
        T Held;    // The value, when HoldsValue
        E Failure; // The error, when not HoldsValue
    };
    bool HoldsValue; // True when the Expected holds a value, false for an error
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= OwnExpected<void> +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// Nothing, or an error of type E, as std::expected<void, E>: the result of a call that
// gives no value.
template <typename T, typename E>
    requires std::is_void_v<T>
class OwnExpected<T, E>
{
    static_assert(IsErrorType<E>,
                  "OwnExpected<void, E> needs an object type E that is not "
                  "an array, const, volatile or an OwnUnexpected");

    // True when an Expected<U, G>, whose error is given as GF, may convert to this
    // Expected: U is void, E is made from GF, and Unexpected<E> cannot be made from the
    // Expected<U, G> itself.
    template <typename U, typename G, typename GF>
    static constexpr bool CanConvertFrom =
        std::is_void_v<U> && std::is_constructible_v<E, GF> &&
        !std::is_constructible_v<OwnUnexpected<E>, OwnExpected<U, G>&> &&
        !std::is_constructible_v<OwnUnexpected<E>, OwnExpected<U, G>> &&
        !std::is_constructible_v<OwnUnexpected<E>, const OwnExpected<U, G>&> &&
        !std::is_constructible_v<OwnUnexpected<E>, const OwnExpected<U, G>>;

  public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = OwnUnexpected<E>;

    template <typename U> using rebind = OwnExpected<U, error_type>;

    // Holds nothing, which is success.
    constexpr OwnExpected() noexcept : HoldsValue(true) {}

    // Holds a copy of what Other holds.
    constexpr OwnExpected(const OwnExpected& Other)
        requires std::is_copy_constructible_v<E>
        : HoldsValue(Other.HoldsValue)
    {
        if (!HoldsValue)
        {
            std::construct_at(std::addressof(Failure), Other.Failure);
        }
    }

    // Holds what Other holds, moved out of it.
    constexpr OwnExpected(OwnExpected&& Other) noexcept(
        std::is_nothrow_move_constructible_v<E>)
        requires std::is_move_constructible_v<E>
        : HoldsValue(Other.HoldsValue)
    {
        if (!HoldsValue)
        {
            std::construct_at(std::addressof(Failure), std::move(Other.Failure));
        }
    }

    // Holds nothing, or the error of Other converted to E.
    template <typename U, typename G>
        requires CanConvertFrom<U, G, const G&>
    constexpr explicit(!std::is_convertible_v<const G&, E>)
        OwnExpected(const OwnExpected<U, G>& Other)
        : HoldsValue(Other.has_value())
    {
        if (!HoldsValue)
        {
            std::construct_at(std::addressof(Failure), Other.error());
        }
    }

    // Holds nothing, or the error of Other, moved out of it and converted to E.
    template <typename U, typename G>
        requires CanConvertFrom<U, G, G>
    constexpr explicit(!std::is_convertible_v<G, E>)
        OwnExpected(OwnExpected<U, G>&& Other)
        : HoldsValue(Other.has_value())
    {
        if (!HoldsValue)
        {
            std::construct_at(std::addressof(Failure), std::move(Other.error()));
        }
    }

    // Holds the error of Failed.
    template <typename G>
        requires std::is_constructible_v<E, const G&>
    constexpr explicit(!std::is_convertible_v<const G&, E>)
        OwnExpected(const OwnUnexpected<G>& Failed)
        : Failure(Failed.error()), HoldsValue(false)
    {
    }

    // Holds the error of Failed, moved out of it.
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr explicit(!std::is_convertible_v<G, E>)
        OwnExpected(OwnUnexpected<G>&& Failed)
        : Failure(std::move(Failed.error())), HoldsValue(false)
    {
    }

    // Holds nothing, which is success.
    constexpr explicit OwnExpected(std::in_place_t) noexcept : HoldsValue(true) {}

    // Holds the error made in place from Arguments.
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit OwnExpected(OwnUnexpectTag, Args&&... Arguments)
        : Failure(std::forward<Args>(Arguments)...), HoldsValue(false)
    {
    }

    // Holds the error made in place from List and Arguments.
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U>&, Args...>
    constexpr explicit OwnExpected(OwnUnexpectTag, std::initializer_list<U> List,
                                   Args&&... Arguments)
        : Failure(List, std::forward<Args>(Arguments)...), HoldsValue(false)
    {
    }

    constexpr ~OwnExpected()
    {
        if (!HoldsValue)
        {
            std::destroy_at(std::addressof(Failure));
        }
    }

    // Holds a copy of what Other holds.
    constexpr OwnExpected& operator=(const OwnExpected& Other)
        requires(std::is_copy_assignable_v<E> && std::is_copy_constructible_v<E>)
    {
        if (HoldsValue && !Other.HoldsValue)
        {
            std::construct_at(std::addressof(Failure), Other.Failure);
        }
        else if (!HoldsValue && Other.HoldsValue)
        {
            std::destroy_at(std::addressof(Failure));
        }
        else if (!HoldsValue)
        {
            Failure = Other.Failure;
        }
        HoldsValue = Other.HoldsValue;
        return *this;
    }

    // Holds what Other holds, moved out of it.
    constexpr OwnExpected&
    operator=(OwnExpected&& Other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                            std::is_nothrow_move_assignable_v<E>)
        requires(std::is_move_constructible_v<E> && std::is_move_assignable_v<E>)
    {
        if (HoldsValue && !Other.HoldsValue)
        {
            std::construct_at(std::addressof(Failure), std::move(Other.Failure));
        }
        else if (!HoldsValue && Other.HoldsValue)
        {
            std::destroy_at(std::addressof(Failure));
        }
        else if (!HoldsValue)
        {
            Failure = std::move(Other.Failure);
        }
        HoldsValue = Other.HoldsValue;
        return *this;
    }

    // Holds the error of Failed.
    template <typename G>
        requires(std::is_constructible_v<E, const G&> &&
                 std::is_assignable_v<E&, const G&>)
    constexpr OwnExpected& operator=(const OwnUnexpected<G>& Failed)
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Failure), Failed.error());
            HoldsValue = false;
        }
        else
        {
            Failure = Failed.error();
        }
        return *this;
    }

    // Holds the error of Failed, moved out of it.
    template <typename G>
        requires(std::is_constructible_v<E, G> && std::is_assignable_v<E&, G>)
    constexpr OwnExpected& operator=(OwnUnexpected<G>&& Failed)
    {
        if (HoldsValue)
        {
            std::construct_at(std::addressof(Failure), std::move(Failed.error()));
            HoldsValue = false;
        }
        else
        {
            Failure = std::move(Failed.error());
        }
        return *this;
    }

    // Returns what Function returns, an Expected with the same E; or, when this holds an
    // error, an Expected of that type that holds the error.
    template <typename F>
        requires std::is_constructible_v<E, E&>
    constexpr auto and_then(F&& Function) &
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function));
        }
        return U(OwnUnexpect, Failure);
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&>
    constexpr auto and_then(F&& Function) const&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function));
        }
        return U(OwnUnexpect, Failure);
    }
    template <typename F>
        requires std::is_constructible_v<E, E&&>
    constexpr auto and_then(F&& Function) &&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function));
        }
        return U(OwnUnexpect, std::move(Failure));
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&&>
    constexpr auto and_then(F&& Function) const&&
    {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(IsOwnExpected<U> && std::is_same_v<typename U::error_type, E>,
                      "and_then() needs a function that returns an OwnExpected with E");
        if (HoldsValue)
        {
            return std::invoke(std::forward<F>(Function));
        }
        return U(OwnUnexpect, std::move(Failure));
    }

    // Holds nothing, in place of what this held.
    constexpr void emplace() noexcept
    {
        if (!HoldsValue)
        {
            std::destroy_at(std::addressof(Failure));
            HoldsValue = true;
        }
    }

    // Returns the error. This must hold one.
    constexpr const E& error() const& noexcept { return Failure; }
    constexpr E& error() & noexcept { return Failure; }
    constexpr const E&& error() const&& noexcept { return std::move(Failure); }
    constexpr E&& error() && noexcept { return std::move(Failure); }

    // Returns true when this holds nothing, which is success, and false for an error.
    constexpr bool has_value() const noexcept { return HoldsValue; }

    // Returns an Expected<void> that holds nothing; or, when this holds an error, what
    // Function returns for it.
    template <typename F> constexpr auto or_else(F&& Function) &
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G();
        }
        return std::invoke(std::forward<F>(Function), Failure);
    }
    template <typename F> constexpr auto or_else(F&& Function) const&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G();
        }
        return std::invoke(std::forward<F>(Function), Failure);
    }
    template <typename F> constexpr auto or_else(F&& Function) &&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E&&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G();
        }
        return std::invoke(std::forward<F>(Function), std::move(Failure));
    }
    template <typename F> constexpr auto or_else(F&& Function) const&&
    {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E&&>>;
        static_assert(IsOwnExpected<G> && std::is_same_v<typename G::value_type, T>,
                      "or_else() needs a function that returns an OwnExpected with T");
        if (HoldsValue)
        {
            return G();
        }
        return std::invoke(std::forward<F>(Function), std::move(Failure));
    }

    // Swaps what this and Other hold.
    constexpr void
    swap(OwnExpected& Other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                      std::is_nothrow_swappable_v<E>)
        requires(std::is_swappable_v<E> && std::is_move_constructible_v<E>)
    {
        if (HoldsValue && Other.HoldsValue)
        {
            return;
        }
        if (!HoldsValue && !Other.HoldsValue)
        {
            using std::swap;
            swap(Failure, Other.Failure);
            return;
        }
        if (!HoldsValue)
        {
            Other.swap(*this);
            return;
        }
        std::construct_at(std::addressof(Failure), std::move(Other.Failure));
        std::destroy_at(std::addressof(Other.Failure));
        HoldsValue = false;
        Other.HoldsValue = true;
    }

    // Returns Function's result in an Expected with the same E; or, when this holds an
    // error, an Expected of that type that holds the error.
    template <typename F>
        requires std::is_constructible_v<E, E&>
    constexpr auto transform(F&& Function) &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, Failure);
        }
        return Transformed<U>(std::forward<F>(Function));
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&>
    constexpr auto transform(F&& Function) const&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, Failure);
        }
        return Transformed<U>(std::forward<F>(Function));
    }
    template <typename F>
        requires std::is_constructible_v<E, E&&>
    constexpr auto transform(F&& Function) &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, std::move(Failure));
        }
        return Transformed<U>(std::forward<F>(Function));
    }
    template <typename F>
        requires std::is_constructible_v<E, const E&&>
    constexpr auto transform(F&& Function) const&&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!HoldsValue)
        {
            return OwnExpected<U, E>(OwnUnexpect, std::move(Failure));
        }
        return Transformed<U>(std::forward<F>(Function));
    }

    // Returns an Expected<void> with the error type that Function returns, which holds
    // nothing; or, when this holds an error, Function's result for it as the error.
    template <typename F> constexpr auto transform_error(F&& Function) &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>();
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function), Failure);
    }
    template <typename F> constexpr auto transform_error(F&& Function) const&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>();
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function), Failure);
    }
    template <typename F> constexpr auto transform_error(F&& Function) &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E&&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>();
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function),
                                 std::move(Failure));
    }
    template <typename F> constexpr auto transform_error(F&& Function) const&&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E&&>>;
        if (HoldsValue)
        {
            return OwnExpected<T, G>();
        }
        return OwnExpected<T, G>(InvokeErrorTag{}, std::forward<F>(Function),
                                 std::move(Failure));
    }

    // Checks that this holds nothing. When it holds an error, it throws a
    // BadExpectedAccess with the error, or, without exceptions, calls std::terminate().
    constexpr void value() const&
    {
        static_assert(std::is_copy_constructible_v<E>, "value() needs an E to copy");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(Failure);
        }
    }
    constexpr void value() &&
    {
        static_assert(std::is_copy_constructible_v<E> && std::is_move_constructible_v<E>,
                      "value() needs an E to copy and to move");
        if (!HoldsValue)
        {
            ThrowBadExpectedAccess(std::move(Failure));
        }
    }

    // Returns true when this holds nothing, which is success.
    constexpr explicit operator bool() const noexcept { return HoldsValue; }

    // Does nothing. This must hold nothing rather than an error.
    constexpr void operator*() const noexcept {}

    // True when X and Y both hold nothing, or both errors that compare equal.
    template <typename T2, typename E2>
        requires std::is_void_v<T2>
    friend constexpr bool operator==(const OwnExpected& X, const OwnExpected<T2, E2>& Y)
    {
        if (X.has_value() != Y.has_value())
        {
            return false;
        }
        return X.has_value() || static_cast<bool>(X.error() == Y.error());
    }

    // True when X holds an error that compares equal to that of Y.
    template <typename E2>
    friend constexpr bool operator==(const OwnExpected& X, const OwnUnexpected<E2>& Y)
    {
        return !X.has_value() && static_cast<bool>(X.error() == Y.error());
    }

    // Swaps what X and Y hold.
    friend constexpr void swap(OwnExpected& X,
                               OwnExpected& Y) noexcept(noexcept(X.swap(Y)))
        requires(std::is_swappable_v<E> && std::is_move_constructible_v<E>)
    {
        X.swap(Y);
    }

  private:
    template <typename, typename> friend class OwnExpected;

    // Holds what Function returns for Arguments, as the error.
    template <typename F, typename... Args>
    constexpr OwnExpected(InvokeErrorTag, F&& Function, Args&&... Arguments)
        : Failure(
              std::invoke(std::forward<F>(Function), std::forward<Args>(Arguments)...)),
          HoldsValue(false)
    {
    }

    // Returns Function's result in an Expected<U, E>, for transform() of an Expected
    // that holds nothing.
    template <typename U, typename F>
    static constexpr OwnExpected<U, E> Transformed(F&& Function)
    {
        if constexpr (std::is_void_v<U>)
        {
            std::invoke(std::forward<F>(Function));
            return OwnExpected<U, E>();
        }
        else
        {
            return OwnExpected<U, E>(InvokeValueTag{}, std::forward<F>(Function));
        }
    }

    union
    {
        E Failure; // The error, when not HoldsValue
    };
    bool HoldsValue; // True when the Expected holds nothing, false for an error
};

} // namespace DtNmos::Detail
