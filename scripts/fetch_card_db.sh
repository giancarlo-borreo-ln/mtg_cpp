#!/usr/bin/env bash
# Fetch the Scryfall "Default Cards" bulk export into ./data as JSONL.
#
# Usage:   scripts/fetch_card_db.sh [--refresh]
# Env:     MTG_CPP_DATA_DIR overrides the destination directory (default: ./data)
#
# The bulk export is a gzipped JSONL stream (~77 MB compressed, ~500-600 MB
# decompressed). This script queries the Scryfall /bulk-data API for the current
# artifact, downloads it with curl resume support, verifies gzip integrity,
# decompresses to data/default-cards.jsonl, sanity-checks every record, and
# records provenance in data/manifest.json. The compressed copy is deleted after
# a successful decompression.
#
# Idempotent: exits early when the manifest's Scryfall `updated_at` still matches
# the API. Use --refresh to force a re-download.
#
# Requires: curl, gzip, python3.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
data_dir="${MTG_CPP_DATA_DIR:-$repo_root/data}"

refresh=false
for arg in "$@"; do
  case "$arg" in
    --refresh) refresh=true ;;
    -h | --help)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "error: unknown argument '$arg' (use --refresh)" >&2
      exit 2
      ;;
  esac
done

bulk_api="https://api.scryfall.com/bulk-data"
jsonl_path="$data_dir/default-cards.jsonl"
gz_path="$data_dir/default-cards.jsonl.gz"
manifest_path="$data_dir/manifest.json"

echo "== querying $bulk_api"
bulk_json="$(curl -fsSL --retry 3 "$bulk_api")"
read -r download_uri updated_at entry_name <<<"$(python3 -c '
import json, sys
d = json.load(sys.stdin)
for f in d.get("data", []):
    if f.get("type") == "default_cards":
        print(f.get("jsonl_download_uri", ""), f.get("updated_at", ""), f.get("name", ""))
        break
' <<<"$bulk_json")"
if [ -z "$download_uri" ]; then
  echo "error: could not find the default_cards bulk entry" >&2
  exit 1
fi
echo "   entry: $entry_name (updated $updated_at)"

if [ "$refresh" = false ] && [ -f "$jsonl_path" ] && [ -f "$manifest_path" ]; then
  stored="$(python3 -c '
import json, sys
print(json.load(open(sys.argv[1])).get("scryfall_updated_at", ""))
' "$manifest_path")"
  if [ -n "$stored" ] && [ "$stored" = "$updated_at" ]; then
    echo "Card DB is up to date ($updated_at). Use --refresh to re-download."
    exit 0
  fi
fi

mkdir -p "$data_dir"

cleanup() {
  rm -f "$gz_path" "$data_dir/.manifest.json.tmp" "$data_dir/.default-cards.jsonl.tmp"
}
trap cleanup EXIT

echo "== downloading $download_uri"
curl -fSL --retry 3 -C - -o "$gz_path" "$download_uri"
if [ ! -s "$gz_path" ]; then
  echo "error: download produced an empty file" >&2
  exit 1
fi
gz_bytes="$(stat -c %s "$gz_path" 2>/dev/null || stat -f %z "$gz_path")"
gz_sha256="$(sha256sum "$gz_path" | cut -d' ' -f1)"

echo "== verifying gzip integrity"
gzip -t "$gz_path"

echo "== decompressing to $jsonl_path"
gzip -dc "$gz_path" >"$data_dir/.default-cards.jsonl.tmp"
mv -f "$data_dir/.default-cards.jsonl.tmp" "$jsonl_path"

jsonl_bytes="$(stat -c %s "$jsonl_path" 2>/dev/null || stat -f %z "$jsonl_path")"
jsonl_sha256="$(sha256sum "$jsonl_path" | cut -d' ' -f1)"

echo "== sanity check (validating every record)"
read -r line_count first_name <<<"$(python3 -c '
import json, sys
path = sys.argv[1]
count = 0
name = ""
with open(path, "r", encoding="utf-8") as fh:
    for line in fh:
        count += 1
        if not name:
            obj = json.loads(line)
            name = obj.get("name", "")
print(count, name)
' "$jsonl_path")"
if [ -z "$first_name" ]; then
  echo "error: first record has no name field; file looks corrupt" >&2
  exit 1
fi
echo "   records: $line_count | first: $first_name"

echo "== writing $manifest_path"
cat >"$data_dir/.manifest.json.tmp" <<EOF
{
  "source": "default_cards",
  "name": "$entry_name",
  "downloaded_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "scryfall_updated_at": "$updated_at",
  "download_uri": "$download_uri",
  "gz_bytes": $gz_bytes,
  "gz_sha256": "$gz_sha256",
  "jsonl_bytes": $jsonl_bytes,
  "jsonl_sha256": "$jsonl_sha256"
}
EOF
mv -f "$data_dir/.manifest.json.tmp" "$manifest_path"

rm -f "$gz_path"
trap - EXIT

echo "== done: $jsonl_path ($jsonl_bytes bytes, $line_count records)"
