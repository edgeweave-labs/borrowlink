#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
version_file="$project_dir/.idf-version"

if [[ ! -f "$version_file" ]]; then
  printf 'error: missing %s\n' "$version_file" >&2
  exit 1
fi
idf_version="$(cat "$version_file")"
if [[ ! "$idf_version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   [[ "$(wc -l < "$version_file" | tr -d ' ')" != 1 ]]; then
  printf 'error: %s must contain one vMAJOR.MINOR.PATCH line\n' "$version_file" >&2
  exit 1
fi

common_dir="$(git -C "$project_dir" rev-parse --git-common-dir)"
if [[ "$common_dir" != /* ]]; then
  common_dir="$project_dir/$common_dir"
fi
main_checkout_dir="$(cd "$common_dir/.." && pwd)"
workspace_dir="$(cd "$main_checkout_dir/.." && pwd)"
idf_dir="$workspace_dir/.tools/esp-idf-$idf_version"

legacy_link="$project_dir/.tools/esp-idf"
if [[ -L "$legacy_link" ]]; then
  printf 'error: remove legacy symlink %s -> %s\n' \
    "$legacy_link" "$(readlink "$legacy_link")" >&2
  exit 1
fi

mkdir -p "$workspace_dir/.tools"
if [[ ! -d "$idf_dir/.git" ]]; then
  git clone --depth 1 --branch "$idf_version" --recurse-submodules \
    --shallow-submodules https://github.com/espressif/esp-idf.git "$idf_dir"
fi

actual_version="$(git -C "$idf_dir" describe --tags --exact-match 2>/dev/null || true)"
if [[ "$actual_version" != "$idf_version" ]]; then
  printf 'error: %s is %s, expected %s\n' \
    "$idf_dir" "${actual_version:-not an exact tag}" "$idf_version" >&2
  exit 1
fi

"$idf_dir/install.sh" esp32s3
