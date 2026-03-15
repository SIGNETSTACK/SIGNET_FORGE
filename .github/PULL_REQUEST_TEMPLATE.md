## Description

<!-- What does this PR do? Why is it needed? -->

## Type of Change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to change)
- [ ] Refactoring (no functional changes)
- [ ] Documentation
- [ ] Build / CI

## Testing

<!-- How was this tested? Include relevant test commands. -->

```bash
cmake --preset dev-tests && cmake --build build --target signet_tests
cd build && ctest --output-on-failure
```

## Checklist

- [ ] All tests pass (`ctest --output-on-failure`)
- [ ] No new compiler warnings
- [ ] SPDX license headers on new files (AGPL-3.0-or-later)
- [ ] CHANGELOG.md updated (if user-facing change)
- [ ] Code formatted with `.clang-format`
- [ ] New public API has tests in `tests/` and benchmark in `benchmarks/`
- [ ] Compliance-related code cites the specific regulatory article
- [ ] Commercial tier files reviewed by maintainer (if applicable)
