---
name: coding-style-review
description: Review, edit, and validate C++17, Qt, CMake, clangd, clang-format, and clang-tidy changes in the RemoteControl repository. Use when changing .h, .cpp, .ui, CMake, VS Code, Qt Creator, build-script, formatting, static-analysis, naming, const-correctness, ownership, protocol, or Windows integration code in this project.
---

# RemoteControl Coding Style Review

Apply this project-specific standard whenever reviewing or modifying the repository. Preserve
working Qt 5.15/Qt 6, MSVC, VS Code, and Qt Creator behavior while enforcing the rules below.

## Workflow

1. Inspect the relevant files, `.clang-format`, `.clang-tidy`, `.clangd`, and nearby conventions.
2. Preserve unrelated user changes in the dirty worktree.
3. Apply every relevant rule below. Prefer a clear project-specific solution over a generic rule
   that conflicts with Qt, MSVC, or Windows APIs.
4. Format changed C++ files with the repository `.clang-format`.
5. Configure and build through `scripts/Build.ps1` so the MSVC Developer Environment is loaded.
6. Run relevant tests and `clang-tidy`. Report any validation limitation.
7. Never commit or push without explicit user approval.

## Language and Build

- Use C++17. Keep `CMAKE_CXX_STANDARD` set to `17`, required, with extensions disabled.
- Keep the current CMake minimum version unless a verified compatibility requirement justifies a
  change. Never lower it merely because CMake 3.10 could support a subset of the project.
- Target Windows with MSVC and Ninja. Do not add unrequested cross-platform abstractions.
- Keep source, configuration, and documentation files in UTF-8.
- Use ASCII identifiers and English comments. Unicode is allowed in user-visible strings,
  translations, protocol data, and tests that verify UTF-8 behavior.
- Leave layout decisions to `.clang-format`; do not hand-format against it.

## Files and Directories

- Name folders with lowercase words separated by underscores.
- Name C++ headers and sources in PascalCase. Give matching header/source pairs the same base name
  and corresponding `include/<module>` and `src/<module>` paths.
- Keep generated files and build output under `build/`; never commit them.
- Do not add documentation or helper scripts when an existing file or script can carry the same
  information clearly.

## Headers and Includes

- Use `#pragma once`.
- Prefer forward declarations when a complete type is not required.
- Include project headers from the `include` root with their full module path, such as
  `#include "common/Packet.h"`.
- Use double quotes for project/generated headers and angle brackets for Qt, Windows, and standard
  library headers.
- Never use parent-relative paths such as `../common/Packet.h`.
- Put declarations in headers and ordinary definitions in sources. Mark a non-template function
  defined in a header `inline` when it is not already implicitly inline.
- Remove genuinely unused includes manually. Do not enable `misc-include-cleaner`: LLVM 18 produces
  false positives for directly included Qt 5, MSVC, and Windows headers in this project.
- Preserve Qt-generated includes such as `ui_MainWindow.h` and MOC includes where required.

## Naming

- Use PascalCase for C++ file names, classes, structs, enums, and enum values.
- Use camelCase for functions, methods, local variables, and struct data members.
- Prefix function parameters with `_`, followed by camelCase.
- Prefix non-static class members with `m_`, followed by camelCase.
- Prefix mutable global variables with `g_`; avoid mutable globals.
- Use PascalCase for `constexpr` and other named constants, matching `.clang-tidy` and the current
  project style. Do not import the original skill's `ALL_CAPS_WITH_UNDERSCORES` convention.
- Avoid one-character names except conventional loop indices `i`, `j`, and `k`.
- Use `enum class` and PascalCase enum values.
- Inside non-static member functions, qualify access to non-static members with `this->`.

## Initialization

- Use brace initialization for variables, data members, temporaries, and constructor initializer
  lists.
- Initialize non-static members in their declarations when the value is independent of constructor
  arguments.
- Order constructor initializers as the base class first, followed by members in declaration order.
- Use parentheses only when braces change semantics or when required by an API/language construct.
- Do not rewrite syntactic uses of `=`, including assignment, default arguments, enum values,
  comparisons, lambda captures, and `= default`/`= delete`, as if they were initialization errors.
- Avoid redundant initialization when value initialization already provides the intended value.

## Const Correctness and Variables

- Use east-const style, for example `QString const value{}` and `Widget* const widget{}`.
- Add `const` to local values, parameters, and member functions when semantics allow it.
- Add `constexpr` when a value is compile-time constant.
- Give each variable its own declaration statement.
- Prefer direct initialization over default construction followed by assignment.
- Avoid needlessly const-qualifying both a pointer and its pointee. Select the qualifier that
  expresses the intended ownership or mutability.
- Use module-local constants in an anonymous namespace in the source file. Put a truly shared
  constant in the relevant common header as `inline constexpr`.
- Replace repeated or domain-specific magic numbers with named constants. Leave intrinsic values
  such as zero/one checks and simple arithmetic coefficients unnamed when their meaning is obvious.

## Functions and Interfaces

- Pass primitive and enum values by value.
- Pass read-only Qt/standard class values by `T const&` unless copying or moving by value is
  intentional.
- Use pointers for optional output parameters and document their purpose.
- Add `[[nodiscard]]` to getters and results whose loss can hide failure or discard meaningful
  data. Do not add it to Qt signals or fluent APIs without a semantic reason.
- Add `const`, `noexcept`, `override`, `= default`, and `= delete` where their contracts are correct.
- Make single-argument constructors `explicit` unless implicit conversion is intentionally part of
  the API.
- Declare a virtual destructor for polymorphic non-`QObject` bases. Use Qt's existing virtual
  destructor behavior for `QObject` subclasses.
- Do not force project functions to return `ErrorCode`; this application uses Qt return values,
  signals, socket errors, and status packets.
- Keep declaration and definition parameter names consistent.

## Types, Casts, and Modern C++

- Use `using`, not `typedef`, in project code.
- Avoid new preprocessor macros. Use typed constants and functions when possible.
- Use `nullptr`, never `NULL` or integer zero as a null pointer.
- Forbid C-style casts.
- Prefer `static_cast` for explicit numeric and enum conversions.
- Use `reinterpret_cast` only at verified Windows API or binary protocol boundaries where the
  representation conversion is required. Keep the cast local and obvious.
- Avoid `dynamic_cast`; prefer explicit design or Qt facilities when runtime type handling is
  genuinely needed.
- Use `const_cast` only when an external API incorrectly omits constness and no safer option exists.
- Lambdas are allowed for short Qt signal handlers and local callbacks.
- Anonymous namespaces are allowed and preferred for source-file-local helpers and constants.
- Standard-library facilities and containers are allowed. Always use the `std::` prefix.

## Ownership and Resource Safety

- Never use `malloc` or `free`.
- Give `QObject` instances a Qt parent whenever practical and let parent ownership destroy them.
- Use `std::unique_ptr` for exclusive ownership of non-`QObject` resources.
- Use raw pointers for non-owning observations and Qt parent-owned objects; make ownership clear at
  the allocation site.
- Avoid manual deletion of parent-owned `QObject` objects.
- Keep allocations exception-safe and release Windows handles/resources on every exit path.
- Smart pointers are allowed and encouraged where they express ownership; do not import the
  original skill's smart-pointer prohibition.

## Errors, Protocol, and Windows Boundaries

- Check meaningful return values from Qt, socket, file, registry, and Windows APIs.
- Translate failures into the project's existing status packets, signals, error strings, or process
  exit codes.
- Use `EXIT_SUCCESS` and `EXIT_FAILURE` for process results.
- Preserve packet size, checksum, length validation, and incomplete-buffer behavior.
- Treat values returned by Windows pointer typedefs according to their API contract. Use `auto const`
  when east-const spelling is obscured by a pointer typedef such as `HINSTANCE`.
- Prefer Qt when it provides equivalent behavior. Keep all remaining direct Windows API calls and
  Windows data types isolated in `PlatformIntegration.cpp`; do not expose them to business or UI
  classes.
- Use assertions only for true programmer invariants or tests, never for recoverable runtime input.

## Comments and Documentation

- Write all code comments in English.
- Use `//` for implementation comments.
- Use `/** ... */` Doxygen comments for reusable public protocol interfaces and non-obvious Windows
  platform APIs.
- Add concise class/interface documentation for client/server components. Do not repeat a function
  name in prose when the declaration is already self-explanatory.
- Explain intent, ownership, protocol layout, platform constraints, and surprising behavior rather
  than narrating individual statements.
- Keep comments accurate when behavior changes.

## Static Analysis Configuration

- Keep enforceable rules in `.clang-tidy`; do not duplicate coding rules in `README.md`.
- Preserve naming checks for PascalCase types/constants, camelCase functions/locals,
  `_camelCase` parameters, and `m_camelCase` private members.
- Preserve checks covering brace/member initialization, const correctness, `noexcept`,
  `[[nodiscard]]`, `nullptr`, `using`, C-style casts, `malloc`, globals, macros, magic numbers,
  boolean simplification, redundant code, parameter passing, and common performance mistakes.
- Do not enable `readability-redundant-access-specifiers`: it misdiagnoses Qt's required transition
  from `private slots:` to `private:`.
- Add a new check only after running it against every project translation unit. Exclude checks that
  systematically report Qt/MSVC/Windows false positives.
- Keep `.clangd` responsible for compiler compatibility flags and clangd diagnostics; keep
  `.clang-tidy` editor-independent so VS Code and Qt Creator share the same rules.

## Validation

Run formatting:

```powershell
$files = rg --files include src -g '*.h' -g '*.cpp'
clang-format --dry-run --Werror --style=file $files
```

Configure and build with the MSVC environment:

```powershell
.\scripts\Build.ps1 -Action build -Config Debug
```

Run tests:

```powershell
ctest --test-dir .\build\vscode-debug --output-on-failure
```

Run static analysis. Standalone LLVM 18 needs the same compatibility macro already supplied to
clangd by `.clangd` when used with the installed Visual Studio 2026 standard library:

```powershell
$files = rg --files src -g '*.cpp'
clang-tidy -p build/vscode-debug `
    --extra-arg=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH `
    --quiet $files
```

Treat diagnostics in project files as actionable. System-header warning counts printed by
`clang-tidy` are not project findings when no diagnostic location points into this repository.

## Explicitly Excluded Original Rules

Do not apply rules tied to a different geometry/kernel architecture:

- `ErrorCode`, `checkError`, `ObjectBase`, `Object`, `AuxObject`, session-managed objects, factory
  construction, or memory-pool macros.
- Project-specific aliases such as `Int`, `Double`, `Bool`, `String`, or a custom `Math` class.
- Prohibitions on STL containers, smart pointers, lambdas, and anonymous namespaces.
- A blanket ASCII prohibition for user-facing strings or UTF-8 protocol tests.
- A blanket `reinterpret_cast` prohibition at Windows and binary wire-format boundaries.

When reviewing, report only concrete findings with file/line references and suggested fixes. When
asked to modify code, fix the findings and validate the result instead of returning a review only.
