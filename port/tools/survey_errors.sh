#!/bin/sh
# Print the first error of every file the survey failed to compile.
# Usage: sh port/tools/survey_errors.sh [build/pc/survey]
dir=${1:-build/pc/survey}
grep '^FAIL' "$dir/result.txt" | while read -r _ src; do
    b=$(basename "$src" .c)
    echo "== $src"
    grep -m3 'error' "$dir/$b.o.err"
done
