#!/bin/bash
# TopNotchNotes Build Script
# Builds both the C++ harness and Go pilot

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  TopNotchNotes Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo

# Parse arguments
BUILD_HARNESS=true
BUILD_PILOT=true
BUILD_TYPE="Release"
RUN_TESTS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --harness-only)
            BUILD_PILOT=false
            shift
            ;;
        --pilot-only)
            BUILD_HARNESS=false
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --test)
            RUN_TESTS=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build C++ Harness
if [ "$BUILD_HARNESS" = true ]; then
    echo -e "${YELLOW}Building C++ Harness...${NC}"
    
    HARNESS_DIR="$PROJECT_ROOT/harness"
    BUILD_DIR="$HARNESS_DIR/build"
    
    # Check for required tools
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}Error: CMake not found. Please install CMake 3.28+${NC}"
        exit 1
    fi
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure
    echo "Configuring CMake..."
    cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
             -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    # Build
    echo "Building..."
    cmake --build . --parallel $(nproc)
    
    # Run tests if requested
    if [ "$RUN_TESTS" = true ]; then
        echo -e "${YELLOW}Running Harness tests...${NC}"
        ctest --output-on-failure
    fi
    
    echo -e "${GREEN}✓ Harness built successfully${NC}"
    echo "  Binary: $BUILD_DIR/harness"
    echo
fi

# Build Go Pilot
if [ "$BUILD_PILOT" = true ]; then
    echo -e "${YELLOW}Building Go Pilot...${NC}"
    
    PILOT_DIR="$PROJECT_ROOT/pilot"
    
    # Check for required tools
    if ! command -v go &> /dev/null; then
        echo -e "${RED}Error: Go not found. Please install Go 1.22+${NC}"
        exit 1
    fi
    
    cd "$PILOT_DIR"
    
    # Download dependencies
    echo "Downloading dependencies..."
    go mod tidy 2>/dev/null || go mod download
    
    # Build
    echo "Building..."
    go build -o "$PROJECT_ROOT/bin/topnotchnotes" .
    
    # Run tests if requested
    if [ "$RUN_TESTS" = true ]; then
        echo -e "${YELLOW}Running Pilot tests...${NC}"
        go test ./...
    fi
    
    echo -e "${GREEN}✓ Pilot built successfully${NC}"
    echo "  Binary: $PROJECT_ROOT/bin/topnotchnotes"
    echo
fi

# Copy harness to bin directory if both built
if [ "$BUILD_HARNESS" = true ] && [ "$BUILD_PILOT" = true ]; then
    mkdir -p "$PROJECT_ROOT/bin"
    cp "$PROJECT_ROOT/harness/build/harness" "$PROJECT_ROOT/bin/" 2>/dev/null || true
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo
echo "To run TopNotchNotes:"
echo "  cd $PROJECT_ROOT/bin && ./topnotchnotes"
