#!/bin/bash
# TopNotchNotes Development Runner
# Starts both the harness and pilot in development mode

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."

# Check if binaries exist
HARNESS_BIN="$PROJECT_ROOT/harness/build/harness"
PILOT_BIN="$PROJECT_ROOT/bin/topnotchnotes"

if [ ! -f "$HARNESS_BIN" ] || [ ! -f "$PILOT_BIN" ]; then
    echo "Binaries not found. Building..."
    "$SCRIPT_DIR/build.sh"
fi

# Create data directory
DATA_DIR="$HOME/.config/TopNotchNotes"
mkdir -p "$DATA_DIR/recordings"

echo "Starting TopNotchNotes..."
echo "Data directory: $DATA_DIR"

# Run the pilot (which will spawn the harness)
cd "$PROJECT_ROOT/bin"
./topnotchnotes
