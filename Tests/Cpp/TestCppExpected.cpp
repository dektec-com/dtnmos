// #*#*#*#*#*#*#*#*#*#*#*#*# TestCppExpected.cpp *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of Detail::OwnExpected, the C++ API's own std::expected
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Each case is a function template over a family: the types of Detail, and, where the
// standard library has them, those of std. The same checks run on both, so that a
// program that uses DtNmos::Expected sees the same behaviour under C++20 as under C++23.
// The monadic functions of std::expected came after the rest, with __cpp_lib_expected
// 202211L, so the cases that use them run on std only from that version.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <version>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    #include <expected>
    #define TEST_STD_EXPECTED 1
    #if __cpp_lib_expected >= 202211L
        #define TEST_STD_MONADIC 1
    #endif
#endif

#include "NmosTest.h"
#include "dtnmos_expected.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Families -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// The types of Detail.
struct Own
{
    template <typename T, typename E> using Expected = DtNmos::Detail::OwnExpected<T, E>;
    template <typename E> using Unexpected = DtNmos::Detail::OwnUnexpected<E>;
    template <typename E> using BadAccess = DtNmos::Detail::OwnBadExpectedAccess<E>;
    static constexpr const DtNmos::Detail::OwnUnexpectTag& Unexpect =
        DtNmos::Detail::OwnUnexpect;
};

#if defined(TEST_STD_EXPECTED)
// The types of std, as the cases compare with them.
struct Std
{
    template <typename T, typename E> using Expected = std::expected<T, E>;
    template <typename E> using Unexpected = std::unexpected<E>;
    template <typename E> using BadAccess = std::bad_expected_access<E>;
    static constexpr const std::unexpect_t& Unexpect = std::unexpect;
};
#endif

// Runs Case on Own, and on Std where the standard library has std::expected.
#if defined(TEST_STD_EXPECTED)
    #define RUN_ON_BOTH(Case)                                                            \
        Case<Own>();                                                                     \
        Case<Std>()
#else
    #define RUN_ON_BOTH(Case) Case<Own>()
#endif

// Runs Case on Own, and on Std where std::expected has the monadic functions.
#if defined(TEST_STD_MONADIC)
    #define RUN_MONADIC(Case)                                                            \
        Case<Own>();                                                                     \
        Case<Std>()
#else
    #define RUN_MONADIC(Case) Case<Own>()
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Types -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// The error of the cases: a code and a text, as DtNmos::Error has.
struct Fault
{
    Fault() = default;
    Fault(int FaultCode, std::string FaultText)
        : Code(FaultCode), Text(std::move(FaultText))
    {
    }

    int Code = 0;
    std::string Text;

    friend bool operator==(const Fault&, const Fault&) = default;
};

// A struct that a function returns in braces, as the headers return an HttpResponse.
struct Point
{
    int X = 0;
    int Y = 0;
};

// A base and a derived class, for a std::unique_ptr that converts.
struct Base
{
    virtual ~Base() = default;
    virtual int Kind() const { return 1; }
};
struct Derived : Base
{
    int Kind() const override { return 2; }
};

// A value that cannot be copied or moved, which transform() must make in place.
struct Pinned
{
    explicit Pinned(int PinnedValue) : Value(PinnedValue) {}
    Pinned(const Pinned&) = delete;
    Pinned& operator=(const Pinned&) = delete;

    int Value;
};

// A value made from a list without throwing, for emplace(), which needs that.
struct Listed
{
    Listed(std::initializer_list<int> List, int ListBase) noexcept
        : Count(List.size()), Base(ListBase)
    {
    }

    std::size_t Count;
    int Base;
};

#if defined(__cpp_exceptions)
// A value whose construction from a negative int throws, and that moves without
// throwing.
struct Picky
{
    Picky(int PickyValue) : Value(PickyValue)
    {
        if (PickyValue < 0)
        {
            throw std::invalid_argument("negative");
        }
    }

    int Value;
};

// A value like Picky whose move may throw too.
struct Sticky
{
    Sticky(int StickyValue) : Value(StickyValue)
    {
        if (StickyValue < 0)
        {
            throw std::invalid_argument("negative");
        }
    }
    Sticky(const Sticky&) = default;
    Sticky(Sticky&& Other) noexcept(false) : Value(Other.Value) {}
    Sticky& operator=(const Sticky&) = default;
    Sticky& operator=(Sticky&&) noexcept(false) = default;

    int Value;
};
#endif

// Returns a Point in braces, which converts to the Expected.
template <typename F> typename F::template Expected<Point, Fault> MakePoint()
{
    return Point{1, 2};
}

// Returns success as {}.
template <typename F> typename F::template Expected<void, Fault> Succeed()
{
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedTraits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What can be made from what, and what cannot: a move-only value makes the Expected
// move-only; a value and an Unexpected convert implicitly, a raw pointer to a
// std::unique_ptr only explicitly; a move that does not throw is noexcept.
//
template <typename F> static void ExpectedTraits()
{
    using IntOr = typename F::template Expected<int, Fault>;
    using PtrOr = typename F::template Expected<std::unique_ptr<int>, Fault>;
    using VoidOr = typename F::template Expected<void, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    static_assert(std::is_same_v<typename IntOr::value_type, int>);
    static_assert(std::is_same_v<typename IntOr::error_type, Fault>);
    static_assert(std::is_same_v<typename IntOr::unexpected_type, Failed>);
    static_assert(std::is_same_v<typename IntOr::template rebind<long>,
                                 typename F::template Expected<long, Fault>>);

    static_assert(std::is_copy_constructible_v<IntOr>);
    static_assert(!std::is_copy_constructible_v<PtrOr>);
    static_assert(!std::is_copy_assignable_v<PtrOr>);
    static_assert(std::is_move_constructible_v<PtrOr>);
    static_assert(std::is_move_assignable_v<PtrOr>);
    static_assert(std::is_nothrow_move_constructible_v<PtrOr>);
    static_assert(std::is_nothrow_move_constructible_v<VoidOr>);

    static_assert(std::is_convertible_v<int, IntOr>);
    static_assert(std::is_convertible_v<Failed, IntOr>);
    static_assert(std::is_convertible_v<Failed, VoidOr>);
    static_assert(std::is_constructible_v<PtrOr, int*>);
    static_assert(!std::is_convertible_v<int*, PtrOr>);
    static_assert(
        std::is_convertible_v<IntOr, typename F::template Expected<long, Fault>>);
    static_assert(!std::is_constructible_v<IntOr, std::string>);
    static_assert(
        !std::is_default_constructible_v<typename F::template Expected<Pinned, Fault>>);

    static_assert(std::is_swappable_v<IntOr>);
    static_assert(std::is_swappable_v<VoidOr>);
}

NMOS_TEST(CppExpectedTraits)
{
    RUN_ON_BOTH(ExpectedTraits);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedConstructs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An Expected is made with a default value, from a value, from a struct in braces, from
// a std::unique_ptr of a derived class, from an Unexpected, in place, with Unexpect,
// from an Expected of another type, and as a copy; an Expected<void> holds nothing or an
// error.
//
template <typename F> static void ExpectedConstructs()
{
    using IntOr = typename F::template Expected<int, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    const IntOr Zero;
    NMOS_ASSERT(Zero.has_value() && *Zero == 0);
    const IntOr Seven = 7;
    NMOS_ASSERT(Seven && *Seven == 7);

    const auto Made = MakePoint<F>();
    NMOS_ASSERT(Made.has_value() && Made->X == 1 && Made->Y == 2);

    const typename F::template Expected<std::unique_ptr<Base>, Fault> Object =
        std::make_unique<Derived>();
    NMOS_ASSERT(Object.has_value() && (*Object)->Kind() == 2);

    const IntOr Refused = Failed(Fault{3, "three"});
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error() == Fault(3, "three"));
    const Failed Kept(std::in_place, 4, "four");
    NMOS_ASSERT(Kept.error().Code == 4);
    const IntOr Copied = Kept;
    NMOS_ASSERT(!Copied && Copied.error().Text == "four");

    const typename F::template Expected<std::string, Fault> Text(std::in_place, 3, 'a');
    NMOS_ASSERT(*Text == "aaa");
    const typename F::template Expected<std::vector<int>, Fault> List(std::in_place,
                                                                      {1, 2, 3});
    NMOS_ASSERT(List->size() == 3 && (*List)[2] == 3);
    const IntOr InPlace(F::Unexpect, 5, "five");
    NMOS_ASSERT(!InPlace && InPlace.error().Code == 5);

    const typename F::template Expected<long, Fault> Wider = Seven;
    NMOS_ASSERT(Wider && *Wider == 7L);
    const typename F::template Expected<long, Fault> WiderFailure = IntOr(Refused);
    NMOS_ASSERT(!WiderFailure && WiderFailure.error().Code == 3);

    const IntOr Copy = Refused;
    NMOS_ASSERT(!Copy && Copy.error() == Refused.error());
    IntOr Source = 8;
    const IntOr Moved = std::move(Source);
    NMOS_ASSERT(Moved && *Moved == 8);

    const typename F::template Expected<void, Fault> Nothing;
    NMOS_ASSERT(Nothing.has_value());
    NMOS_ASSERT(Succeed<F>().has_value());
    const typename F::template Expected<void, Fault> InPlaceNothing(std::in_place);
    NMOS_ASSERT(InPlaceNothing.has_value());
    const typename F::template Expected<void, Fault> VoidRefused =
        Failed(Fault{6, "six"});
    NMOS_ASSERT(!VoidRefused && VoidRefused.error().Code == 6);
    const typename F::template Expected<void, Fault> VoidInPlace(F::Unexpect, 7, "seven");
    NMOS_ASSERT(!VoidInPlace && VoidInPlace.error().Text == "seven");
    const typename F::template Expected<void, Fault> VoidCopy = VoidRefused;
    NMOS_ASSERT(!VoidCopy && VoidCopy.error().Code == 6);
}

NMOS_TEST(CppExpectedConstructs)
{
    RUN_ON_BOTH(ExpectedConstructs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedMovesOnly -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An Expected of a move-only value moves, and gives its value out with *, value() and
// value_or() of an rvalue.
//
template <typename F> static void ExpectedMovesOnly()
{
    using PtrOr = typename F::template Expected<std::unique_ptr<int>, Fault>;

    PtrOr First = std::make_unique<int>(5);
    PtrOr Second = std::move(First);
    NMOS_ASSERT(Second && **Second == 5);

    PtrOr Third = typename F::template Unexpected<Fault>(Fault{1, "none"});
    Third = std::move(Second);
    NMOS_ASSERT(Third && **Third == 5);

    std::unique_ptr<int> Out = *std::move(Third);
    NMOS_ASSERT(Out && *Out == 5);
    PtrOr Fourth = std::make_unique<int>(6);
    Out = std::move(Fourth).value();
    NMOS_ASSERT(*Out == 6);
    PtrOr Fifth = typename F::template Unexpected<Fault>(Fault{2, "none"});
    Out = std::move(Fifth).value_or(std::make_unique<int>(7));
    NMOS_ASSERT(*Out == 7);
}

NMOS_TEST(CppExpectedMovesOnly)
{
    RUN_ON_BOTH(ExpectedMovesOnly);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedObserves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// has_value(), the bool, *, ->, value(), value_or() and error() give what the Expected
// holds. value() of an error throws a BadExpectedAccess with the error, which the base
// for void catches too.
//
template <typename F> static void ExpectedObserves()
{
    using TextOr = typename F::template Expected<std::string, Fault>;

    TextOr Held = std::string("held");
    NMOS_ASSERT(Held.has_value() && static_cast<bool>(Held));
    NMOS_ASSERT(Held->size() == 4 && *Held == "held" && Held.value() == "held");
    Held->append("!");
    Held.value() += "?";
    NMOS_ASSERT(*Held == "held!?");
    NMOS_ASSERT(Held.value_or("other") == "held!?");

    const TextOr Refused = typename F::template Unexpected<Fault>(Fault{9, "nine"});
    NMOS_ASSERT(!Refused.has_value() && !static_cast<bool>(Refused));
    NMOS_ASSERT(Refused.value_or("other") == "other");
    NMOS_ASSERT(Refused.error().Code == 9);
    NMOS_ASSERT(std::move(Refused).error().Text == "nine");

#if defined(__cpp_exceptions)
    bool Thrown = false;
    try
    {
        (void)Refused.value();
    }
    catch (const typename F::template BadAccess<Fault>& Access)
    {
        Thrown = Access.error().Code == 9 && Access.what() != nullptr;
    }
    NMOS_ASSERT(Thrown);

    bool ThrownAsBase = false;
    const typename F::template Expected<void, Fault> VoidRefused =
        typename F::template Unexpected<Fault>(Fault{10, "ten"});
    try
    {
        VoidRefused.value();
    }
    catch (const typename F::template BadAccess<void>&)
    {
        ThrownAsBase = true;
    }
    NMOS_ASSERT(ThrownAsBase);
#endif
}

NMOS_TEST(CppExpectedObserves)
{
    RUN_ON_BOTH(ExpectedObserves);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedAssigns -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Assignment changes what an Expected holds, between a value and an error in each
// direction, from another Expected, a value and an Unexpected; emplace() makes a value
// in place of a value or an error. An assignment whose new value throws leaves the
// error that was held.
//
template <typename F> static void ExpectedAssigns()
{
    using TextOr = typename F::template Expected<std::string, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    TextOr Target = std::string("one");
    const TextOr Refused = Failed(Fault{1, "refused"});
    Target = Refused;
    NMOS_ASSERT(!Target && Target.error().Code == 1);
    Target = TextOr(std::string("two"));
    NMOS_ASSERT(Target && *Target == "two");
    Target = Failed(Fault{2, "again"});
    NMOS_ASSERT(!Target && Target.error().Code == 2);
    const Failed Third(Fault{3, "third"});
    Target = Third;
    NMOS_ASSERT(Target.error().Code == 3);
    Target = "three";
    NMOS_ASSERT(Target && *Target == "three");
    Target = "four";
    NMOS_ASSERT(*Target == "four");
    Target = TextOr(Refused);
    NMOS_ASSERT(!Target && Target.error() == Refused.error());

    typename F::template Expected<int, Fault> Number = Failed(Fault{4, "four"});
    NMOS_ASSERT(Number.emplace(9) == 9 && *Number == 9);
    NMOS_ASSERT(Number.emplace(10) == 10);
    typename F::template Expected<Listed, Fault> List = Failed(Fault{4, "four"});
    List.emplace({4, 5}, 6);
    NMOS_ASSERT(List && List->Count == 2 && List->Base == 6);

    typename F::template Expected<void, Fault> Nothing;
    Nothing = Failed(Fault{5, "five"});
    NMOS_ASSERT(!Nothing && Nothing.error().Code == 5);
    Nothing = typename F::template Expected<void, Fault>();
    NMOS_ASSERT(Nothing);
    Nothing = Failed(Fault{6, "six"});
    Nothing.emplace();
    NMOS_ASSERT(Nothing);

#if defined(__cpp_exceptions)
    typename F::template Expected<Picky, Fault> PickyTarget = Failed(Fault{7, "seven"});
    try
    {
        PickyTarget = -1;
    }
    catch (const std::invalid_argument&)
    {
    }
    NMOS_ASSERT(!PickyTarget && PickyTarget.error().Code == 7);

    typename F::template Expected<Sticky, Fault> StickyTarget = Failed(Fault{8, "eight"});
    try
    {
        StickyTarget = -1;
    }
    catch (const std::invalid_argument&)
    {
    }
    NMOS_ASSERT(!StickyTarget && StickyTarget.error().Code == 8);
    StickyTarget = 8;
    NMOS_ASSERT(StickyTarget && StickyTarget->Value == 8);
#endif
}

NMOS_TEST(CppExpectedAssigns)
{
    RUN_ON_BOTH(ExpectedAssigns);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedSwaps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// swap(), as a member and found by ADL, swaps two values, two errors, and a value with
// an error in either order, of an Expected and of an Expected<void>.
//
template <typename F> static void ExpectedSwaps()
{
    using TextOr = typename F::template Expected<std::string, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    TextOr A = std::string("a");
    TextOr B = std::string("b");
    A.swap(B);
    NMOS_ASSERT(*A == "b" && *B == "a");

    TextOr C = Failed(Fault{1, "c"});
    TextOr D = Failed(Fault{2, "d"});
    using std::swap;
    swap(C, D);
    NMOS_ASSERT(C.error().Code == 2 && D.error().Code == 1);

    A.swap(C);
    NMOS_ASSERT(!A && A.error().Code == 2 && C && *C == "b");
    A.swap(C);
    NMOS_ASSERT(A && *A == "b" && !C && C.error().Code == 2);
    swap(C, A);
    NMOS_ASSERT(!A && C && *C == "b");

    typename F::template Expected<void, Fault> Nothing;
    typename F::template Expected<void, Fault> Refused = Failed(Fault{3, "e"});
    Nothing.swap(Refused);
    NMOS_ASSERT(!Nothing && Nothing.error().Code == 3 && Refused);
    swap(Nothing, Refused);
    NMOS_ASSERT(Nothing && !Refused);

    Failed X(Fault{4, "x"});
    Failed Y(Fault{5, "y"});
    swap(X, Y);
    NMOS_ASSERT(X.error().Code == 5 && Y.error().Code == 4);
}

NMOS_TEST(CppExpectedSwaps)
{
    RUN_ON_BOTH(ExpectedSwaps);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedCompares -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// == and != compare two Expecteds, of the same or of other types, an Expected with a
// value and with an Unexpected, in either order, and two Unexpecteds.
//
template <typename F> static void ExpectedCompares()
{
    using IntOr = typename F::template Expected<int, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    const IntOr One = 1;
    const IntOr AlsoOne = 1;
    const IntOr Two = 2;
    const IntOr Refused = Failed(Fault{1, "one"});
    const typename F::template Expected<long, Fault> LongOne = 1L;

    NMOS_ASSERT(One == AlsoOne && One != Two && One == LongOne && LongOne == One);
    NMOS_ASSERT(One != Refused && Refused != One);
    NMOS_ASSERT(One == 1 && 1 == One && One != 2 && 2 != One && Refused != 1);
    NMOS_ASSERT(Refused == Failed(Fault{1, "one"}) && Failed(Fault{1, "one"}) == Refused);
    NMOS_ASSERT(Refused != Failed(Fault{2, "two"}) && One != Failed(Fault{1, "one"}));
    NMOS_ASSERT(Refused == IntOr(Failed(Fault{1, "one"})));

    const typename F::template Expected<void, Fault> Nothing;
    const typename F::template Expected<void, Fault> VoidRefused =
        Failed(Fault{1, "one"});
    using VoidOr = typename F::template Expected<void, Fault>;
    NMOS_ASSERT(Nothing == VoidOr());
    NMOS_ASSERT(Nothing != VoidRefused && VoidRefused != Nothing);
    NMOS_ASSERT(VoidRefused == Failed(Fault{1, "one"}) &&
                Nothing != Failed(Fault{1, "one"}));

    NMOS_ASSERT(Failed(Fault{1, "one"}) == Failed(Fault{1, "one"}));
    NMOS_ASSERT(Failed(Fault{1, "one"}) != Failed(Fault{1, "two"}));
}

NMOS_TEST(CppExpectedCompares)
{
    RUN_ON_BOTH(ExpectedCompares);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppExpectedChains -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// and_then(), or_else(), transform() and transform_error() call the function on the
// value or on the error, and pass the other on, for an lvalue, a const lvalue, an rvalue
// and a const rvalue, which each pass the value as such. transform() makes a value that
// cannot be moved in place, and makes an Expected<void> from a function that returns
// nothing.
//
template <typename F> static void ExpectedChains()
{
    using IntOr = typename F::template Expected<int, Fault>;
    using TextOr = typename F::template Expected<std::string, Fault>;
    using Failed = typename F::template Unexpected<Fault>;

    IntOr Two = 2;
    const IntOr ConstTwo = 2;
    const IntOr Refused = Failed(Fault{1, "refused"});

    NMOS_ASSERT(*Two.and_then([](int& V) { return TextOr(std::to_string(V)); }) == "2");
    NMOS_ASSERT(*ConstTwo.and_then([](const int& V)
                                   { return TextOr(std::to_string(V)); }) == "2");
    NMOS_ASSERT(*IntOr(2).and_then([](int&& V) { return TextOr(std::to_string(V)); }) ==
                "2");
    NMOS_ASSERT(*std::move(ConstTwo).and_then(
                    [](const int&& V) { return TextOr(std::to_string(V)); }) == "2");
    const TextOr Passed = Refused.and_then([](const int&) { return TextOr("never"); });
    NMOS_ASSERT(!Passed && Passed.error().Code == 1);

    NMOS_ASSERT(*Refused.or_else([](const Fault& Why) { return IntOr(Why.Code + 10); }) ==
                11);
    NMOS_ASSERT(*IntOr(Refused).or_else([](Fault&& Why) { return IntOr(Why.Code); }) ==
                1);
    NMOS_ASSERT(*Two.or_else([](Fault&) { return IntOr(0); }) == 2);
    const IntOr Still =
        Refused.or_else([](const Fault&) { return IntOr(Failed(Fault{2, "still"})); });
    NMOS_ASSERT(!Still && Still.error().Code == 2);

    NMOS_ASSERT(*Two.transform([](int& V) { return V * 3; }) == 6);
    NMOS_ASSERT(*ConstTwo.transform([](const int& V) { return std::to_string(V); }) ==
                "2");
    NMOS_ASSERT(*IntOr(2).transform([](int&& V) { return V + 1; }) == 3);
    NMOS_ASSERT(*std::move(ConstTwo).transform([](const int&& V) { return V; }) == 2);
    const auto PinnedResult = Two.transform([](int V) { return Pinned(V); });
    NMOS_ASSERT(PinnedResult && PinnedResult->Value == 2);
    int Seen = 0;
    const auto Nothing = Two.transform([&Seen](int V) { Seen = V; });
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(Nothing)>,
                                 typename F::template Expected<void, Fault>>);
    NMOS_ASSERT(Nothing && Seen == 2);
    const auto NotTransformed = Refused.transform([](int V) { return V; });
    NMOS_ASSERT(!NotTransformed && NotTransformed.error().Code == 1);

    const auto AsText =
        Refused.transform_error([](const Fault& Why) { return Why.Text; });
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(AsText)>,
                                 typename F::template Expected<int, std::string>>);
    NMOS_ASSERT(!AsText && AsText.error() == "refused");
    NMOS_ASSERT(*Two.transform_error([](Fault&) { return 0; }) == 2);
    NMOS_ASSERT(
        IntOr(Refused).transform_error([](Fault&& Why) { return Why.Code; }).error() ==
        1);

    using VoidOr = typename F::template Expected<void, Fault>;
    VoidOr Done;
    const VoidOr VoidRefused = Failed(Fault{3, "void"});
    NMOS_ASSERT(*Done.and_then([] { return IntOr(4); }) == 4);
    NMOS_ASSERT(VoidRefused.and_then([] { return IntOr(4); }).error().Code == 3);
    NMOS_ASSERT(Done.or_else([](Fault&) { return VoidOr(Failed(Fault{0, ""})); }));
    NMOS_ASSERT(VoidRefused.or_else([](const Fault&) { return VoidOr(); }));
    NMOS_ASSERT(*Done.transform([] { return 5; }) == 5);
    NMOS_ASSERT(VoidRefused.transform([] { return 5; }).error().Code == 3);
    NMOS_ASSERT(std::move(Done).transform([] {}));
    NMOS_ASSERT(
        VoidRefused.transform_error([](const Fault& Why) { return Why.Code; }).error() ==
        3);
    NMOS_ASSERT(VoidOr().transform_error([](Fault&&) { return 0; }));
}

NMOS_TEST(CppExpectedChains)
{
    RUN_MONADIC(ExpectedChains);
}

NMOS_TEST_MAIN("CppExpected", NMOS_RUN(CppExpectedTraits),
               NMOS_RUN(CppExpectedConstructs), NMOS_RUN(CppExpectedMovesOnly),
               NMOS_RUN(CppExpectedObserves), NMOS_RUN(CppExpectedAssigns),
               NMOS_RUN(CppExpectedSwaps), NMOS_RUN(CppExpectedCompares),
               NMOS_RUN(CppExpectedChains))
