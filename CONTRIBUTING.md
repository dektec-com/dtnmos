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
   - `long` and `unsigned long` appear only where an interface of the operating system
     or of a library defines them, such as `timeval.tv_sec` or libcurl's options.
8. **A header guards itself with `#pragma once`**, as the first line after the file
   header, rather than with an `#ifndef` guard.
9. **A variable is declared where it is first needed**, with its first value when it has
   one, one declaration per line; a loop counter in its `for`. No `goto`: a function that
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
    breaking a program built against an earlier one.

Rules 10, 11 and 12 describe where the code is going. The renames of plan 0001 bring the
existing code to them, a step at a time.

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

## Tools

`Scripts/check_tools.sh`, or `Scripts/check_tools.ps1` on Windows, says what is there and
what is missing:

| Tool | Version | For |
|---|---|---|
| clang-format | exactly 18.1.8 | Rules 4 and 6. Another build formats differently and would reformat files that are right. `pip install clang-format==18.1.8` is the surest way to the same one the CI uses |
| CMake | 3.25 or newer | Everything |
| Python | 3.8 or newer | `Scripts/fix_banners.py` |
| Visual Studio | 2022 or newer | The Windows presets |
| GCC | 11 or newer | The Linux presets |
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

### What a comment says, and where

- **One place per fact.** A function's contract, its parameters, units, results and
  side effects, stands above its declaration in the header. The `.c` file says how and
  why the body works, not the contract again. A static function is described where it
  is defined.
- **A result list is complete and in the order the code checks.** It names every code
  the function can return.
- **Units and origins are stated once, on the declaration**: bytes, milliseconds,
  nanoseconds; counting from 0 or from 1.
- **A number in a comment is the code's number.** Name the constant rather than spelling
  out its value.
- **Scope words mean what they say.** "Every", "only", "always", "never" are written
  where the code makes them true; otherwise the cases are named.
- **A banner names the function directly below it**, and `Scripts/fix_banners.py` draws
  it.
- **A test's comment says what its assertions check**, with the numbers they use.
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
