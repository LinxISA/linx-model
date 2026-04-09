#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "missing compile_commands.json in ${build_dir}" >&2
  exit 1
fi

files=()
while IFS= read -r file; do
  files+=("${file}")
done < <(find src tests -type f -name '*.cpp' | sort)

if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi

clang-tidy -p "${build_dir}" -header-filter='^(include|src|tests)/' "${files[@]}"
