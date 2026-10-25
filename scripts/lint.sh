#!/usr/bin/env bash
# Google C++ Style Guide gate for the doccraft tree.
#
#   scripts/lint.sh            # check formatting, fail on any diff
#   scripts/lint.sh --fix      # rewrite sources in place
#   scripts/lint.sh --tidy     # also run clang-tidy (needs build/compile_commands.json)
#   scripts/lint.sh --cpplint  # also run cpplint when it is installed
#
# third_party/ is vendored upstream code and intentionally excluded.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

fix=0
run_tidy=0
run_cpplint=0
for arg in "$@"; do
  case "${arg}" in
    --fix) fix=1 ;;
    --tidy) run_tidy=1 ;;
    --cpplint) run_cpplint=1 ;;
    -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: ${arg}" >&2; exit 2 ;;
  esac
done

clang_format="${CLANG_FORMAT:-clang-format}"
if ! command -v "${clang_format}" >/dev/null 2>&1; then
  echo "error: clang-format not found (install it or set CLANG_FORMAT)" >&2
  exit 2
fi

sources=()
while IFS= read -r file; do sources+=("${file}"); done < <(
  find src tests \( -name '*.cc' -o -name '*.h' \) -print | sort
)
if [ "${#sources[@]}" -eq 0 ]; then
  echo "error: no sources found" >&2
  exit 2
fi

status=0

if [ "${fix}" -eq 1 ]; then
  "${clang_format}" -i "${sources[@]}"
  echo "clang-format: rewrote ${#sources[@]} files"
else
  if ! "${clang_format}" --dry-run --Werror "${sources[@]}"; then
    echo "clang-format: formatting violations found; run scripts/lint.sh --fix" >&2
    status=1
  else
    echo "clang-format: clean (${#sources[@]} files)"
  fi
fi

if [ "${run_cpplint}" -eq 1 ]; then
  if command -v cpplint >/dev/null 2>&1; then
    if ! cpplint --quiet --recursive --extensions=cc,h --linelength=80 src tests; then
      echo "cpplint: violations found" >&2
      status=1
    else
      echo "cpplint: clean"
    fi
  else
    echo "cpplint: not installed (pip install cpplint); skipped" >&2
  fi
fi

if [ "${run_tidy}" -eq 1 ]; then
  if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy: not installed; skipped" >&2
  elif [ ! -f build/compile_commands.json ]; then
    echo "clang-tidy: build/compile_commands.json missing; configure with" >&2
    echo "  cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    status=1
  elif ! clang-tidy -p build "${sources[@]}"; then
    echo "clang-tidy: diagnostics reported (advisory, not failing the gate)" >&2
  else
    echo "clang-tidy: clean"
  fi
fi

exit "${status}"
