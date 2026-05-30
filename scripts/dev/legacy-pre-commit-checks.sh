#!/bin/sh
# Signet Forge — pre-commit hook
# Prevents accidental commit of secrets, credentials, and internal documents.
# Install: cp _pre-commit-hook.sh .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit

BLOCKED_PATTERNS="\.env$|\.pem$|\.key$|\.p12$|\.pfx$|\.secret$|\.credentials$|id_rsa|id_ed25519|CLAUDE\.md|\.claude/"
BLOCKED_DIRS="docs/internal/|docs/product-knowledge/"

staged=$(git diff --cached --name-only --diff-filter=ACM)

for f in $staged; do
    if echo "$f" | grep -qE "$BLOCKED_PATTERNS"; then
        echo "BLOCKED: $f matches a sensitive file pattern."
        echo "If this is intentional, use: git commit --no-verify"
        exit 1
    fi
    if echo "$f" | grep -qE "$BLOCKED_DIRS"; then
        echo "BLOCKED: $f is in an internal-only directory."
        echo "If this is intentional, use: git commit --no-verify"
        exit 1
    fi
done

exit 0
