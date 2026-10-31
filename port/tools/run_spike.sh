#!/bin/sh
# Build and run the D1-a spike. Run from the repository root.
#   sh port/tools/run_spike.sh [core|full] [frames]
# GBA_BUILD_DIR must hold a GBA build of the same commit (fireemblem8.map/.elf,
# its objects) and baserom.gba. It defaults to this tree when the GBA `make`
# has been run here (the pinned oracle, D6), else ~/fe8-feu. It is only read.
# FE8PC_TIMEOUT (seconds, default 60) bounds the run. JOBS (default: the
# number of CPUs) is the build's -j; port/check-all.mk passes its CHECK_JOBS.
set -e
variant=${1:-core}
frames=${2:-60}
if [ -z "$GBA_BUILD_DIR" ]; then
    if [ -f fireemblem8.elf ]; then GBA_BUILD_DIR=.; else GBA_BUILD_DIR=$HOME/fe8-feu; fi
fi
gba=$GBA_BUILD_DIR
full=0
out=build/pc
if [ "$variant" = full ]; then full=1; out=build/pc-full; fi

make -f port/Makefile -j"${JOBS:-$(nproc)}" FULL=$full GBA_DIR="$gba" 2>&1 \
    | grep -E "error:|gen_host_link:|fixptr (convert|relocs):|unknown symbol" || true
FE8PC_FRAMES=$frames timeout "${FE8PC_TIMEOUT:-60}" "./$out/fe8pc" "$gba/baserom.gba" || echo "fe8pc exit status $?"
