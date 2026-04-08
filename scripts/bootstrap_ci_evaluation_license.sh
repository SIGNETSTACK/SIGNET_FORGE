#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HELPER_BUILD_DIR="${SIGNET_CI_LICENSE_HELPER_BUILD_DIR:-$ROOT_DIR/build-ci-license-helper}"
KEY_DIR="${SIGNET_CI_LICENSE_DIR:-$ROOT_DIR/.ci-license}"
HEADER_PATH="$ROOT_DIR/include/signet/crypto/license.hpp"
TOKEN_PATH="$KEY_DIR/license.token"
PUB_BLOCK_PATH="$KEY_DIR/signing.pub.block"
META_PATH="$KEY_DIR/ci_evaluation_license_metadata.txt"

mkdir -p "$KEY_DIR"

cmake_args=(
  -S "$ROOT_DIR"
  -B "$HELPER_BUILD_DIR"
  -DSIGNET_ENABLE_COMMERCIAL=ON
  -DSIGNET_BUILD_TOOLS=ON
  -DSIGNET_BUILD_TESTS=OFF
  -DSIGNET_BUILD_EXAMPLES=OFF
  -DSIGNET_BUILD_BENCHMARKS=OFF
  -DSIGNET_BUILD_PYTHON=OFF
  -DSIGNET_BUILD_WASM=OFF
  -DCMAKE_BUILD_TYPE=Release
)

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    if [[ -z "${CMAKE_GENERATOR:-}" ]]; then
      cmake_args+=( -A x64 )
    fi
    ;;
esac

cmake "${cmake_args[@]}" >/dev/null
cmake --build "$HELPER_BUILD_DIR" --target signet_license_gen --config Release --parallel >/dev/null

license_tool=""
for candidate in \
  "$HELPER_BUILD_DIR/signet_license_gen" \
  "$HELPER_BUILD_DIR/signet_license_gen.exe" \
  "$HELPER_BUILD_DIR/Release/signet_license_gen" \
  "$HELPER_BUILD_DIR/Release/signet_license_gen.exe"
do
  if [[ -f "$candidate" ]]; then
    license_tool="$candidate"
    break
  fi
done

if [[ -z "$license_tool" ]]; then
  license_tool="$(find "$HELPER_BUILD_DIR" -maxdepth 3 \( -name signet_license_gen -o -name signet_license_gen.exe \) | head -n 1)"
fi

if [[ -z "$license_tool" ]]; then
  echo "Failed to locate signet_license_gen in $HELPER_BUILD_DIR" >&2
  exit 1
fi

"$license_tool" keygen --output "$KEY_DIR" >/dev/null

od -An -v -t x1 "$KEY_DIR/signing.pub" | awk '
  BEGIN { c = 0 }
  {
    for (i = 1; i <= NF; ++i) {
      if (c % 8 == 0) {
        printf(c == 0 ? "    " : ",\n    ");
      } else {
        printf(", ");
      }
      printf("0x%s", $i);
      ++c;
    }
  }
  END { printf("\n"); }
' > "$PUB_BLOCK_PATH"

PUB_BLOCK_PATH="$PUB_BLOCK_PATH" perl -0pi -e '
  my $block = do {
    local @ARGV = ($ENV{PUB_BLOCK_PATH});
    local $/;
    <>
  };
  s#static constexpr Ed25519PublicKey SIGNET_LICENSE_PUBKEY = \{.*?\n\};#static constexpr Ed25519PublicKey SIGNET_LICENSE_PUBKEY = {\n$block};#s;
' "$HEADER_PATH"

license_token="$({
  "$license_tool" issue \
    --customer "GitHub Actions CI" \
    --customer-id "github-actions" \
    --tier evaluation \
    --expiry 30d \
    --max-nodes 2 \
    --max-users 5 \
    --max-rows-month 50000000 \
    --signing-key "$KEY_DIR/signing.key" \
    2> "$KEY_DIR/license_issue.stderr"
})"

printf '%s' "$license_token" > "$TOKEN_PATH"

cat > "$META_PATH" <<META
customer=GitHub Actions CI
customer_id=github-actions
tier=evaluation
expiry=30d
max_nodes=2
max_users=5
max_rows_month=50000000
header_path=$HEADER_PATH
helper_build_dir=$HELPER_BUILD_DIR
signing_key_path=$KEY_DIR/signing.key
public_key_path=$KEY_DIR/signing.pub
token_path=$TOKEN_PATH
META

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    printf 'SIGNET_COMMERCIAL_LICENSE_KEY<<__SIGNET_CI_LICENSE__\n'
    printf '%s\n' "$license_token"
    printf '__SIGNET_CI_LICENSE__\n'
  } >> "$GITHUB_ENV"
else
  printf 'export SIGNET_COMMERCIAL_LICENSE_KEY=%q\n' "$license_token"
fi

echo "Bootstrapped signed evaluation license: 30d / 50,000,000 rows"
echo "Token written to: $TOKEN_PATH"
echo "Metadata written to: $META_PATH"
