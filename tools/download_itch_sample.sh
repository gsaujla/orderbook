#!/usr/bin/env bash
# Download one day of NASDAQ TotalView-ITCH 5.0 sample data (about 4.8 GB, gzipped).
# The parser reads the .gz directly, so there is no need to unzip it (about 12 GB).
#
#   tools/download_itch_sample.sh [MMDDYYYY] [dest_dir]
#
# Defaults to 01302019 into data/. Other dates are listed at
# https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/
set -euo pipefail

DAY="${1:-01302019}"
DEST="${2:-data}"
FILE="${DAY}.NASDAQ_ITCH50.gz"
URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${FILE}"

mkdir -p "$DEST"
echo "downloading $URL -> $DEST/$FILE"

# The server sometimes stalls mid-transfer. Abort if the speed stays under 10 KB/s for
# 60 s, then resume from where it stopped (--continue-at -). Re-running the script
# also resumes.
for attempt in $(seq 1 20); do
    if curl --fail --location --continue-at - --speed-limit 10000 --speed-time 60 \
        --output "$DEST/$FILE" "$URL"; then
        echo "done: $(du -h "$DEST/$FILE" | cut -f1) $DEST/$FILE"
        exit 0
    fi
    echo "attempt $attempt stalled or failed; resuming in 5 s..."
    sleep 5
done
echo "giving up after 20 attempts; re-run to resume" >&2
exit 1
