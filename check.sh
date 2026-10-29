#!/bin/sh
# check.sh - auto check: build, boot on a temp port with a throwaway data dir, run checks.txt
# checks.txt lines: METHOD PATH EXPECTED_STATUS [JSON_BODY]   (# = comment, "LOGIN" = sign in as test admin)
set -u
OUT=build/check
./build.sh "$OUT" > "$OUT.log" 2>&1 || { grep -E 'Error|Fatal' "$OUT.log"; echo "BUILD FAILED"; exit 1; }
echo "build ok"
PORT=${CHECK_PORT:-4099}
TMP=$(mktemp -d)
cp -R seed/. "$TMP/" 2>/dev/null || true
JAR="$TMP/cookies"
export DATA_DIR="$TMP" PORT RUN_SCHEDULER=false JWT_SECRET=check-secret-check-secret SLOWNESS_MS=0
"$OUT/sedev" create-user checkadmin checkpass123 superadmin > /dev/null 2>&1
"$OUT/sedev" > "$TMP/server.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; wait $PID 2>/dev/null; rm -rf "$TMP"' EXIT
i=0; until curl -s "localhost:$PORT/api/health" > /dev/null; do i=$((i+1)); [ $i -gt 50 ] && { echo "server did not start"; cat "$TMP/server.log"; exit 1; }; sleep 0.2; done
FAIL=0; N=0
while IFS= read -r line; do
  case "$line" in ''|\#*) continue;; esac
  if [ "$line" = "LOGOUT" ]; then rm -f "$JAR"; continue; fi
  if [ "$line" = "LOGIN" ]; then
    curl -s -c "$JAR" -H 'Content-Type: application/json' -d '{"username":"checkadmin","password":"checkpass123"}' "localhost:$PORT/api/auth/login" > /dev/null
    continue
  fi
  M=$(echo "$line" | awk '{print $1}'); P=$(echo "$line" | awk '{print $2}'); E=$(echo "$line" | awk '{print $3}')
  B=$(echo "$line" | cut -d' ' -f4-); [ "$B" = "$line" ] && B=""
  N=$((N+1))
  if [ -n "$B" ] && [ "$B" != "$E" ]; then
    S=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" -X "$M" -H 'Content-Type: application/json' -d "$B" "localhost:$PORT$P")
  else
    S=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" -X "$M" "localhost:$PORT$P")
  fi
  if [ "$S" = "$E" ]; then echo "ok   $M $P $S"; else echo "FAIL $M $P got $S want $E"; FAIL=$((FAIL+1)); fi
done <<CHECKS
$(for f in checks.txt checks.d/*.txt; do echo LOGOUT; cat "$f"; done 2>/dev/null)
CHECKS
echo "$((N-FAIL))/$N checks passed"
grep -i 'unhandled' "$TMP/server.log" | head -20
[ $FAIL -eq 0 ]
