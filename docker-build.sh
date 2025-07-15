mkdir -p .cache

CMAKE_MAJOR_VERSION=3
CMAKE_MINOR_VERSION=31
CMAKE_PATCH_VERSION=0
CMAKE_VERSION_TAG=${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}
CMAKE_VERSION=${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}.${CMAKE_PATCH_VERSION}
GRPC_VERSION=1.73.1

# Download CMake release archive if not present
if [ ! -f .cache/cmake-${CMAKE_VERSION}.tar.gz ]; then
    wget https://cmake.org/files/v${CMAKE_VERSION_TAG}/cmake-${CMAKE_VERSION}.tar.gz -O .cache/cmake-${CMAKE_VERSION}.tar.gz
fi

# Clone gRPC source code if not present
if [ ! -d .cache/grpc-${GRPC_VERSION} ]; then
    git clone --recurse-submodules -b v${GRPC_VERSION} --depth 1 --shallow-submodules https://github.com/grpc/grpc .cache/grpc-${GRPC_VERSION}
fi

docker build -t testdisk_grpc_server:debug \
    --build-arg GRPC_VERSION=${GRPC_VERSION} \
    --build-arg CMAKE_VERSION=${CMAKE_VERSION} \
    .
