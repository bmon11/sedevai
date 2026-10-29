#!/bin/sh
# import-mongo.sh <MONGO_URL> [DATA_DIR] - copies the sedevmn Mongo collections into the JSON-file store.
# ObjectIds become 24-hex strings, dates become ISO strings. Users keep bcrypt hashes, which the
# Delphi build cannot verify: reset passwords with `sedev create-user` afterwards.
set -eu
URL=${1:?usage: import-mongo.sh <MONGO_URL> [DATA_DIR]}
OUT=${2:-data}
mkdir -p "$OUT"
for c in users settings sources articles socialaccounts socialposts lenses entities mentions runs topics editions chatlogs; do
  mongoexport --uri="$URL" --collection="$c" --jsonArray --quiet --out="$OUT/$c.raw.json" || { echo "skip $c"; continue; }
  jq 'walk(if type == "object" then
        (if has("$oid") then .["$oid"]
         elif has("$date") then (.["$date"] | if type == "object" then (.["$numberLong"] | tonumber / 1000 | todate) else . end)
         elif has("$numberLong") then (.["$numberLong"] | tonumber)
         elif has("$numberDouble") then (.["$numberDouble"] | tonumber)
         elif has("$binary") then .["$binary"].base64
         else . end)
      else . end)' "$OUT/$c.raw.json" > "$OUT/$c.json"
  rm "$OUT/$c.raw.json"
  echo "$c: $(jq length "$OUT/$c.json")"
done
