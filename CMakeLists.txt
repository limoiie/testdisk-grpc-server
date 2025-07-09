cmake_minimum_required(VERSION 3.31)
project(testdisk_grpc_wrapper)

set(CMAKE_CXX_STANDARD 20)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find protobuf first
find_package(Protobuf REQUIRED)

# Include our custom CMake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# Find gRPC with our custom finder
find_package(gRPC REQUIRED)

# Include directories
include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(/opt/homebrew/include)

# Library directories
link_directories(lib)
link_directories(/opt/homebrew/lib)

# Protobuf generation
set(PROTO_FILES proto/photorec.proto)

# Generate protobuf files using standard CMake functions with proper proto path
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
# Override the protobuf generation to use correct proto path
foreach(FIL ${PROTO_FILES})
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    get_filename_component(FIL_ABS ${FIL} ABSOLUTE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc"
               "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h"
        COMMAND protobuf::protoc
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto ${FIL_ABS}
        DEPENDS ${FIL} protobuf::protoc
        COMMENT "Running C++ protocol buffer compiler on ${FIL}"
        VERBATIM)
endforeach()

# Generate gRPC files manually
set(GRPC_SRCS)
set(GRPC_HDRS)
foreach(FIL ${PROTO_FILES})
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    list(APPEND GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.cc")
    list(APPEND GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.h")
endforeach()

# Add custom commands for gRPC generation
foreach(FIL ${PROTO_FILES})
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    get_filename_component(FIL_ABS ${FIL} ABSOLUTE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.cc"
               "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.h"
        COMMAND protobuf::protoc
        ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto ${FIL_ABS}
        DEPENDS ${FIL} protobuf::protoc
        COMMENT "Running gRPC protocol buffer compiler on ${FIL}"
        VERBATIM)
endforeach()

set_source_files_properties(${GRPC_SRCS} ${GRPC_HDRS} PROPERTIES GENERATED TRUE)

# Add server executable
add_executable(testdisk_grpc_wrapper 
    src/main.cpp
    src/photorec_grpc_server.cpp
    src/photorec_grpc_server.h
    src/logger.cpp
    src/logger.h
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${GRPC_SRCS}
    ${GRPC_HDRS}
)

# Add client example executable
add_executable(client_example
    examples/client_example.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${GRPC_SRCS}
    ${GRPC_HDRS}
)

# Include directories for generated files
target_include_directories(testdisk_grpc_wrapper PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(client_example PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Use pkg-config to get gRPC++ and absl linker flags
execute_process(
    COMMAND pkg-config --libs grpc++
    OUTPUT_VARIABLE GRPC_ABSL_PKG_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(GRPC_ABSL_PKG_LIBS)

# Link the static library and required system libraries for server
target_link_libraries(testdisk_grpc_wrapper 
    photorec    # Links libphotorec.a
    z           # Required by libphotorec.a (zlib)
    protobuf::libprotobuf
    ${GRPC_ABSL_PKG_LIBS}
)

# Link libraries for client example
target_link_libraries(client_example
    protobuf::libprotobuf
    ${GRPC_ABSL_PKG_LIBS}
)

# Set compiler flags
target_compile_options(testdisk_grpc_wrapper PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_compile_options(client_example PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Print configuration info
message(STATUS "PhotoRec gRPC Wrapper Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Protobuf: ${Protobuf_VERSION}")
message(STATUS "  gRPC: Found")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Targets: testdisk_grpc_wrapper, client_example")
