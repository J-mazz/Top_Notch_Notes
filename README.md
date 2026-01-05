# TopNotchNotes

Academic productivity dashboard with live transcription. Twin-engine architecture: **C++23 harness** for hardware + **Go/Fyne pilot** for UI.

## Quick Start

```bash
# Harness (C++)
cd harness && mkdir build && cd build && cmake .. && make

# Pilot (Go)
cd pilot && go build -o topnotchnotes .

# Run
./pilot/topnotchnotes
```

## Requirements

| Component | Stack |
|-----------|-------|
| Harness | Clang 18+, CMake 3.28+, C++23 |
| Pilot | Go 1.22+, Fyne v2 |

## Architecture

```
┌─────────────────┐    JSON/stdin    ┌─────────────────┐
│  Pilot (Go)     │◄────────────────►│  Harness (C++)  │
│  • Fyne UI      │    stdout/IPC    │  • Audio capture│
│  • Sessions     │                  │  • Transcription│
│  • Course mgmt  │                  │  • File I/O     │
└─────────────────┘                  └─────────────────┘
```

Apache 2.0 License

