#!/bin/bash

# PhotoRec gRPC Wrapper Build Script
# This script automates the build process for the PhotoRec gRPC wrapper

set -e  # Exit on any error

## Following code may cause the grpc++ not found
# export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

INSTALL_DIR=/usr/local

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to get CPU count (works on both Linux and macOS)
get_cpu_count() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 1
    fi
}

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    print_error "CMakeLists.txt not found. Please run this script from the project root directory."
    exit 1
fi

# Check for required tools
print_status "Checking build dependencies..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_error "CMake not found. Please install CMake 3.20 or higher."
    exit 1
fi

# Check CMake version
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d'.' -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d'.' -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 20 ]); then
    print_error "CMake version $CMAKE_VERSION found, but 3.20 or higher is required."
    exit 1
fi

print_success "CMake $CMAKE_VERSION found"

# Check for make
if ! command -v make &> /dev/null; then
    print_error "Make not found. Please install make."
    exit 1
fi

print_success "Make found"

# Check for C++ compiler
if ! command -v g++ &> /dev/null; then
    print_error "G++ compiler not found. Please install a C++20 compatible compiler."
    exit 1
fi

print_success "G++ compiler found"

# Check for required libraries
print_status "Checking for required libraries..."

# Check for protobuf
if ! pkg-config --exists protobuf; then
    print_error "Protobuf development libraries not found."
    print_warning "Install with: sudo apt install libprotobuf-dev protobuf-compiler"
    exit 1
fi

print_success "Protobuf found"

# Check for gRPC
if ! pkg-config --exists grpc++; then
    print_error "gRPC++ development libraries not found."
    print_warning "Install with: sudo apt install libgrpc++-dev"
    # exit 1
fi

print_success "gRPC++ found"

# Check for zlib
if ! pkg-config --exists zlib; then
    print_error "zlib development libraries not found."
    print_warning "Install with: sudo apt install zlib1g-dev"
    exit 1
fi

print_success "zlib found"

# Check for PhotoRec library
if [ ! -f "${INSTALL_DIR}/lib/libphotorec.a" ]; then
    print_error "PhotoRec library (${INSTALL_DIR}/lib/libphotorec.a) not found."
    print_warning "Please ensure the PhotoRec library is available in the ${INSTALL_DIR}/lib/ directory."
    exit 1
fi

print_success "PhotoRec library found"

# Create build directory
print_status "Creating build directory..."
if [ -d "build" ]; then
    print_warning "Build directory already exists. Cleaning..."
    rm -rf build
fi

mkdir -p build
cd build

# Configure with CMake
print_status "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${INSTALL_DIR}

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed."
    exit 1
fi

print_success "CMake configuration completed"

# Build the project
print_status "Building project..."
CPU_COUNT=$(get_cpu_count)
make -j$CPU_COUNT

if [ $? -ne 0 ]; then
    print_error "Build failed."
    exit 1
fi

print_success "Build completed successfully!"

# Check if executables were created
if [ -f "testdisk_grpc_wrapper" ]; then
    print_success "Server executable created: testdisk_grpc_wrapper"
else
    print_error "Server executable not found after build."
    exit 1
fi

if [ -f "client_example" ]; then
    print_success "Client example created: client_example"
else
    print_warning "Client example not found after build."
fi

# Print usage information
echo ""
print_success "Build completed! You can now run:"
echo ""
echo "  # Start the server:"
echo "  ./testdisk_grpc_wrapper"
echo ""
echo "  # Or with custom address:"
echo "  ./testdisk_grpc_wrapper --address 127.0.0.1:50051"
echo ""
echo "  # Run the client example (in another terminal):"
echo "  ./client_example localhost:50051 /dev/sda /tmp/recovery"
echo ""
print_warning "Note: You may need root privileges to access disk devices."
echo ""
