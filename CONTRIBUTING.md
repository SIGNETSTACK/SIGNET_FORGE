# Contributing to Signet Forge

Thank you for your interest in contributing to Signet Forge. This document explains the
contribution process, coding standards, and requirements for submitting changes.

## Before You Start

1. **Read the [Code of Conduct](CODE_OF_CONDUCT.md)** — all contributors must follow it.
2. **Check existing issues** — your idea or bug may already be tracked.
3. **Open an issue first** for significant changes — discuss the approach before writing code.

## Contributor License Agreement (CLA)

All contributors must sign a Contributor License Agreement before their first PR can be merged.
The CLA bot will automatically comment on your PR with instructions when you submit it.

**Why**: The CLA ensures clean IP ownership for the project and protects both contributors and users.

We use the Apache Foundation ICLA template (Individual CLA). Companies submitting on behalf of
employees should contact us about the Corporate CLA (CCLA).

## Development Workflow

### 1. Fork and Clone

```bash
git clone https://github.com/<your-username>/signet-forge.git
cd signet-forge
```

### 2. Create a Branch

```bash
git checkout -b feat/my-feature    # Features
git checkout -b fix/issue-42       # Bug fixes
git checkout -b refactor/cleanup   # Refactoring
```

### 3. Build and Test

```bash
cmake --preset dev-tests
cmake --build build --target signet_tests
cd build && ctest --output-on-failure
```

All tests must pass before submitting a PR.

### 4. Submit a Pull Request

- Target the `main` branch
- Fill in the PR template (description, testing, checklist)
- Ensure the CLA bot check passes
- Wait for CI to pass (all 7 jobs must be green)

## Coding Standards

### Language

- C++20 (`-std=c++20`)
- Compiler support: Apple Clang 17+, GCC 13+, Clang 18+, MSVC 2022+

### Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes / Structs | `PascalCase` | `ParquetWriter`, `WalManager` |
| Functions / Methods | `snake_case` | `write_column()`, `flush_row_group()` |
| Variables | `snake_case` | `row_count`, `file_offset_` |
| Private members | `snake_case_` (trailing underscore) | `schema_`, `options_` |
| Constants | `UPPER_SNAKE_CASE` | `WAL_RECORD_MAGIC`, `PARQUET_MAX_PAGE_SIZE` |
| Namespaces | `lower_snake_case` | `signet::forge`, `signet::forge::detail` |
| Template parameters | `PascalCase` | `typename T`, `typename Func` |
| Enum values | `UPPER_SNAKE_CASE` | `Encoding::PLAIN`, `Compression::SNAPPY` |

### Style Rules

- 4-space indentation (no tabs)
- Opening brace on same line as statement (Attach style)
- Column limit: 100 characters (soft limit; readability takes priority)
- `#pragma once` for header guards
- `[[nodiscard]]` on all functions returning values
- RAII for all resources — no manual `new`/`delete`
- `tl::expected<T, Error>` (via `signet::expected`) for error handling — no exceptions
- Prefer `std::string_view` over `const std::string&` for non-owning references
- Use `static constexpr` for compile-time constants
- Private implementation details go in `namespace detail {}`

### Formatting

This project includes a `.clang-format` file. Please format new code before submitting:

```bash
clang-format -i include/signet/my_new_file.hpp
```

Do **not** reformat existing files in a separate commit — this makes review difficult.

### Include Order

1. Project headers (`"signet/..."`)
2. Standard library headers (`<algorithm>`, `<string>`, etc.)
3. Platform headers (`<fcntl.h>`, `<windows.h>`) — guarded by `#if`

### License Headers

Every source file must start with an SPDX license header.

**AGPL-3.0-or-later files** (all library code):
```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
```

**AGPL-3.0-or-later commercial tier files** (audit/compliance):
```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.
```

## Test Requirements

- **Every new public API** must have tests in `tests/`
- **Every new public API** should have a benchmark in `benchmarks/`
- Use Catch2 `TEST_CASE` and `REQUIRE` / `CHECK` macros
- Tag tests appropriately: `[writer]`, `[wal]`, `[encryption]`, `[hardening]`, etc.
- Security-sensitive code must include negative tests (invalid input, boundary values)
- Test with `expected<>` — always `REQUIRE(result.has_value())` before dereferencing

### Running Specific Tests

```bash
# All tests
cd build && ctest --output-on-failure

# Specific tag
./build/signet_tests "[wal]"

# Hardening tests only
ctest -L hardening

# Sanitizers
cmake --preset asan && cmake --build build-asan && ctest --preset asan
cmake --preset tsan && cmake --build build-tsan && ctest --preset tsan
```

## Compliance Code

Code touching audit trail or regulatory compliance functionality must:

1. Cite the specific regulatory article (e.g., MiFID II RTS 24 Annex I, EU AI Act Art. 12)
2. Include field-level mapping comments showing which Parquet columns satisfy which regulatory fields
3. Include positive and negative tests
4. Be reviewed by the maintainer (enforced via CODEOWNERS)

## Commit Message Format

```
type: short description

Optional longer description explaining the "why" behind the change.
```

**Types**: `feat`, `fix`, `refactor`, `test`, `docs`, `ci`, `bench`, `chore`

**Examples**:
```
feat: add LZ4 compression support
fix: prevent WAL reader crash on truncated record
test: add negative test for Thrift string bomb
docs: update quickstart with Python example
ci: add coverage reporting to CI pipeline
```

## PR Checklist

Before submitting, verify:

- [ ] All tests pass (`cmake --preset dev-tests && cmake --build build && cd build && ctest`)
- [ ] No new compiler warnings
- [ ] SPDX license header present on all new files
- [ ] CHANGELOG.md entry added under `[Unreleased]`
- [ ] New public API has tests and benchmarks
- [ ] Code formatted with `.clang-format`
- [ ] Commit messages follow the format above

## AGPL-3.0 Commercial Tier Code

PRs touching any of the following files require maintainer review (enforced via CODEOWNERS):

- `include/signet/ai/audit_chain.hpp`
- `include/signet/ai/decision_log.hpp`
- `include/signet/ai/inference_log.hpp`
- `include/signet/ai/compliance/compliance_types.hpp`
- `include/signet/ai/compliance/mifid2_reporter.hpp`
- `include/signet/ai/compliance/eu_ai_act_reporter.hpp`
- `include/signet/crypto/key_metadata.hpp`
- `include/signet/crypto/pme.hpp`
- `include/signet/crypto/post_quantum.hpp`

These files are licensed under AGPL-3.0-or-later. By contributing to them, you agree that your contributions
will be licensed under the same terms as the existing code.

## Questions?

- Open a [GitHub Discussion](../../discussions) for questions
- File a [GitHub Issue](../../issues) for bugs or feature requests
- Email: johnson@signetstack.io
