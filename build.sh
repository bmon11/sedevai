#!/bin/sh
# build.sh [outdir] - compiles sedev.dpr with Free Pascal in Delphi mode
OUT=${1:-build/main}
mkdir -p "$OUT"
EXTRA=""
if command -v xcrun > /dev/null 2>&1; then
  SDK=$(xcrun --show-sdk-path)
  EXTRA="-XR$SDK -Fl$SDK/usr/lib"
fi
fpc -Mdelphi $EXTRA -O1 -vewn -FU"$OUT" -FE"$OUT" -o"$OUT/sedev" sedev.dpr
