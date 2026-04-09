#!/usr/bin/env bash
set -euo pipefail

files=()
while IFS= read -r file; do
  files+=("${file}")
done < <(find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort)

if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
