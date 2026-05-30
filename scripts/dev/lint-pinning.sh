#!/usr/bin/env bash
# scripts/dev/lint-pinning.sh
# Signet Forge supply-chain pinning lint
#
# Phase 1 (default): warns but exit 0 (shadow mode — observe what would be blocked)
# Phase 3: SIGNET_LINT_PHASE=enforce → exit 1 on any violation
#
# Bypass: operator can `git commit --no-verify` for emergency hotfixes; bypass
# usage is logged + reviewed.

set -e
GATE_PHASE="${SIGNET_LINT_PHASE:-shadow}"  # shadow | enforce
VIOLATIONS=0

# 1. package.json — flag `^` or `~` ranges (anti-substitution: exact pinning required)
if [ -f package.json ]; then
  while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue
    echo "PINNING-LINT [package.json]: unpinned range — $LINE"
    VIOLATIONS=$((VIOLATIONS+1))
  done < <(grep -E '"[^"]+":[[:space:]]*"[\^~]' package.json 2>/dev/null || true)
fi

# 2. requirements.txt — flag absence of hash pinning
if [ -f requirements.txt ]; then
  if ! grep -q 'sha256:' requirements.txt; then
    echo "PINNING-LINT [requirements.txt]: file present but no sha256 hashes (use pip-compile --generate-hashes)"
    VIOLATIONS=$((VIOLATIONS+1))
  fi
fi

# 3. Dockerfile — flag FROM without @sha256:
while IFS= read -r DOCKERFILE; do
  while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue
    echo "PINNING-LINT [$DOCKERFILE]: unpinned FROM — $LINE"
    VIOLATIONS=$((VIOLATIONS+1))
  done < <(grep -E '^FROM ' "$DOCKERFILE" 2>/dev/null | grep -vE '@sha256:' || true)
done < <(find . -maxdepth 3 -name 'Dockerfile*' -type f 2>/dev/null)

# 4. .github/workflows/*.yml — flag `uses: org/action@vN` without SHA
if [ -d .github/workflows ]; then
  while IFS= read -r WF; do
    while IFS= read -r LINE; do
      [ -z "$LINE" ] && continue
      echo "PINNING-LINT [$WF]: unpinned action — $LINE"
      VIOLATIONS=$((VIOLATIONS+1))
    done < <(grep -E 'uses: [^@]+@v[0-9]+($|[^a-f0-9])' "$WF" 2>/dev/null || true)
  done < <(find .github/workflows -name '*.yml' -type f 2>/dev/null)
fi

# 5. Cargo.toml — flag `*` deps
if [ -f Cargo.toml ]; then
  while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue
    echo "PINNING-LINT [Cargo.toml]: unpinned dep — $LINE"
    VIOLATIONS=$((VIOLATIONS+1))
  done < <(grep -E '=[[:space:]]*"\*"' Cargo.toml 2>/dev/null || true)
fi

# Exit policy
if [ "$VIOLATIONS" -eq 0 ]; then
  exit 0
fi

echo ""
echo "PINNING-LINT: $VIOLATIONS unpinned dependency reference(s) found"

if [ "$GATE_PHASE" = "enforce" ]; then
  echo "PINNING-LINT: Phase 3 hard-enforce — failing"
  exit 1
else
  echo "PINNING-LINT: Phase 1/2 shadow/soft — warning only (set SIGNET_LINT_PHASE=enforce to fail)"
  exit 0
fi
