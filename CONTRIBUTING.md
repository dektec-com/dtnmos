# Contributing to dtnmos

dtnmos follows the coding rules of CDTAPI, DekTec's C library for its SDI and SMPTE ST
2110 interfaces, so that DekTec's two C libraries read alike. What follows is CDTAPI's
`CONTRIBUTING.md` for dtnmos: the rules about DekTec's driver are left out, and one rule
about structs is added.

## Coding rules

1. **Everything is in English**: code, identifiers, comments, documentation and commit
   messages.
2. **Comments describe what the code does, never what it used to do.** No "changed
   from…", no "used to be…", no commented-out previous versions. The reason for a change
   belongs in the commit message, which is where the history lives.
3. **Lint enforces the rules.** See below.
4. **No line longer than 90 characters**, in code and in comments alike.
5. **Every source file starts with a header** describing what the file contains.
6. **An opening brace goes on its own line** (Allman), for functions, `if`, `for`,
   `while`, `switch` and struct definitions.
7. **A value whose width matters has a fixed-width type** from `<stdint.h>`, such as
   `uint8_t`, `uint16_t`, `uint32_t` and `uint64_t`, in the public headers too.
   - Counters and indices stay `int`, sizes stay `size_t`, and text is `char`.
   - A value that is true or false is `bool`, from `<stdbool.h>`: a field, a parameter, a
     result and a callback's result alike, in the public headers and inside.
   - `long` and `unsigned long` appear only where an interface of the operating system
     or of a library defines them, such as `timeval.tv_sec` or libcurl's options.
8. **A header guards itself with `#pragma once`**, as the first line after the file
   header, rather than with an `#ifndef` guard.
9. **A variable is declared where it is first needed**, with its first value when it has
   one, one declaration per line; a loop counter in its `for`. Results are `DtNmosResult`,
   and a failure leaves its message for `DtNmos_GetLastError()`. No `goto`: a function that
   must clean up after failures hands the steps to a helper and cleans up after it.
10. **A function that is not static is named `Class_Function`**: the type it works on,
    or the component it belongs to, then an underscore.
    - What is public begins with `DtNmos`, such as `DtNmosNode_AddSender`.
    - What is internal begins with `Nmos`, such as `NmosJson_Parse`. That keeps it apart
      from CDTAPI's internal `Dt` and `Os` names in a program that links both.
    - A static function has a plain name.
11. **The public headers declare each section's functions in alphabetical order**, so
    that a function is found without searching; types come first, where the functions
    need them. A new function goes in its alphabetical place in any file, header or
    implementation.
12. **A struct a caller fills in for the library begins with `Size`**, which the caller
    sets to the struct's `sizeof`, so that a later version can add fields without
    breaking a program built against an earlier one. The library checks it with
    `DTNMOS_CHECK_SIZE()` wherever it is given one, and a struct it fills in it sets
    itself:

    | `Size` | Result |
    |---|---|
    | 0 | `DTNMOS_E_INVALID_ARGUMENT`: not set |
    | below the struct's first version | `DTNMOS_E_INVALID_ARGUMENT` |
    | a version the library knows | the fields after it take their defaults |
    | above the struct the library knows | `DTNMOS_E_INVALID_ARGUMENT`: the library is older than the header |

    A field added to a struct goes at its end, and the code that reads it checks first
    that `Size` reaches it.

`Scripts/check_style.sh` checks rule 11 in the public headers. In a `.c` file the
functions keep the order in which each is defined before its first use, and a new one
goes in its alphabetical place where that order allows it.

### File header

```c
// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosNode.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The NMOS node: its devices, senders and receivers, and its registration
//
// SPDX-License-Identifier: BSD-3-Clause
```

The first line must name the file it is in. The style check verifies that, because a
copy-pasted header naming the wrong file otherwise survives for years.
`Scripts/fix_banners.py` draws a banner at the right width.

## The C++ API

The `.hpp` headers in `Include/` are a C++23 wrapper over the C API, header only, beside
the C header each wraps. The rules above hold for them, but for rule 10, which is for C
functions, and with these:

- **Names are those of the C API without the prefix**, in PascalCase: types, functions,
  fields, parameters and local variables alike, e.g. `dtnmos::Node::AddSender()`. A
  getter whose name would be a type's is `Get...`, e.g. `GetId()`, and a parameter does
  not take the name of a type. What the standard library calls by name keeps the
  standard's name: `begin()`, `end()`, `size()`, `value_type`.
- **A private member has no mark**, no `_` and no `m`. It never takes the name of a type
  or of a function of its class: the types that only the wrapper uses are in
  `dtnmos::detail`, not nested in the class, and the C handle of a class is `Native`.
- **No C struct and no C enum in the API.** Each struct has a value type and each enum an
  `enum class` with the C values; the wrapper converts at the boundary. A conversion
  asserts with `DTNMOS_DETAIL_LAST_FIELD` which field it knows to be the last of its C
  struct, and converts an enum with a `switch` over every value and no `default`, so that
  the compiler refuses a field or a value that the C API adds and the wrapper does not
  convert. A field small enough to fit in the struct's padding escapes the assertion, so
  **a field added to a public struct, or a value to a public enum, is converted in the
  same commit**, and review checks it.
- **A call that can fail returns an `Expected` or a `Status`**, marked `[[nodiscard]]`;
  the wrapper throws nothing of its own, and builds without exceptions.
- **Rule 11 holds for the public members of a class**: alphabetical, with the static
  functions that make an object, such as `Open`, first. Review checks it, as
  `Scripts/check_style.sh` reads only the `DTNMOS_API` lines of a `.h`.

`Tests/Cpp` has its suite, built with `DTNMOS_WITH_CPP`, which is on where CMake finds a
C++ compiler; it is built a second time with `-fno-exceptions`, except by MSVC.

## Tools

`Scripts/check_tools.sh`, or `Scripts/check_tools.ps1` on Windows, says what is there and
what is missing:

| Tool | Version | For |
|---|---|---|
| clang-format | exactly 18.1.8 | Rules 4 and 6. Another build formats differently and would reformat files that are right. `pip install clang-format==18.1.8` is the surest way to the same one the CI uses |
| CMake | 3.25 or newer | Everything |
| Python | 3.8 or newer | `Scripts/fix_banners.py` |
| Visual Studio | 2022 or newer; 2022 17.3 for the C++ API | The Windows presets |
| GCC | 11 or newer; 12 for the C++ API | The Linux presets. With GCC 11, configure with `-DDTNMOS_WITH_CPP=OFF` |
| Ninja | any | What the Linux presets build with |
| vcpkg | any | The presets `windows-full` and `linux-full`, which build with libcurl and civetweb; set `VCPKG_ROOT` |
| Git | any | |

`CLANG_FORMAT` points at another clang-format, for a machine where the right version is
not the one on the path.

## A comment describes this code

A comment says what the code it stands over does, and why it does it that way. It does
not say what the code used to do, and it does not describe code outside this project.
The behaviour a standard asks for, of IS-04, IS-05 or an SDP, belongs in a comment
whenever it explains a choice. State it as a fact about this code, and name the standard
and its section.

### How a comment is written

A comment is written for a programmer who has not seen the source and wants to use the
function. Before anything else, it tells what the function is for.

- **The first sentence says what the function does, in ordinary words**, starting with
  a verb: "Registers the node and its resources with the registry", not "Registers what
  is not registered of the node". It names the purpose, not the parameters in the order
  of the signature.
- **Then what the caller must know to use it**: what must be true before the call, what
  each parameter means when its name does not say it, what the function fills in or
  returns, who owns what, and on which thread to call it.
- **Short sentences with a subject and a verb.** No chains of "of" ("the flow of the
  media of the receiver"), no noun phrase where a sentence belongs, and no word made to
  do the work of a clause.
- **Only what a user needs.** How the function does its work, constants it uses inside
  and the reasons behind its design belong in the `.c` file or a design document.
- **The results as a short table**: the code, then when it occurs. "And the errors of
  X()" stands for those of a function called underneath.
- **A field of a struct has its own comment**, saying what it holds and when it is used.
  When the comment does not fit after the field, it goes on the line above.
- **A result code has its value written out**, `DTNMOS_E + n`, as a program built
  against one version compares with these numbers. A value never changes, and a new
  code is added at the end.
- **A section of a header starts by explaining the concept** before its functions: what
  the parts are, how they relate, and the steps a program takes.

Read the comment back as someone who does not know the code. If a sentence needs
reading twice, rewrite it.

### What a comment says, and where

- **One place per fact.** A function's contract, its parameters, units, results and
  side effects, stands above its declaration in the header. The `.c` file says how and
  why the body works, not the contract again. A static function is described where it
  is defined.
- **A result list names every code the function can return**, directly or through "the
  errors of X()".
- **Units and origins are stated once, on the declaration**: bytes, milliseconds,
  nanoseconds; counting from 0 or from 1.
- **A number in a comment is the code's number.** Name the constant rather than spelling
  out its value.
- **Scope words mean what they say.** "Every", "only", "always", "never" are written
  where the code makes them true; otherwise the cases are named.
- **A banner names the function directly below it**, and `Scripts/fix_banners.py` draws
  it.
- **A test's comment says what its assertions check**, with the numbers they use. A
  suite is one file under `Tests/`, built on `Tests/NmosTest.h` into a program of its
  own: a case is an `NMOS_TEST` with a line in the file's `NMOS_TEST_MAIN`, and a new
  suite a line in `Tests/CMakeLists.txt`.
- **A change of behaviour updates every description of it**, not only the comment above
  the change: search `Include/`, `README.md` and the examples for what the old behaviour
  was called.
- **Plain English.** Split a sentence rather than stack relative clauses, and reflow the
  whole paragraph after an edit rather than leave a short line behind.

## How the rules are enforced

| Rule | Enforced by |
|---|---|
| 1 | Review |
| 2 | `Scripts/check_style.sh` (tripwire on common phrasings), then review |
| 4 | `clang-format` and `Scripts/check_style.sh` |
| 5 | `Scripts/check_style.sh` |
| 6 | `clang-format` |
| 7 | Review |
| 8 | `Scripts/check_style.sh` |
| 9 | `Scripts/check_style.sh` for `goto`, then review |
| 10 | Review |
| 11 | Review |
| 12 | Review |
| Everything else | The compilers' warnings, as errors, and review. `.clang-tidy` configures clang-tidy for a run by hand; no gate runs it |

Run them locally:

    Scripts/build.sh --lint-only

Install the pre-commit hook once, and the same checks run before each commit, on the
files the commit adds or changes:

    Scripts/install_hooks.sh

The hook is a convenience, not the gate. CI runs the checks on the whole tree and is what
actually blocks a merge, so a contributor without the hook installed is still stopped.

## Design documents

Design decisions are recorded as numbered documents, and a decision that changes gets a
new document rather than a rewrite of the old one. They are not in this repository,
which is public: they live in DekTec's private `dtnmos-design`, and a comment or a commit
message refers to them by number, such as "plan 0001". The style check refuses a file
under `Documentation/` or `Docs/Plans/` here, so that one cannot walk back in by
accident.
