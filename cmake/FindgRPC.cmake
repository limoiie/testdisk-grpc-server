# FindgRPC.cmake - Custom gRPC finder to avoid protobuf target conflicts

# Try to find gRPC using pkg-config first
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GRPC grpc++)
    pkg_check_modules(GRPCPP grpc++)
    pkg_check_modules(GRPCPP_REFLECTION grpc++_reflection)
endif()

# Find gRPC libraries
find_library(GRPC_LIBRARY
    NAMES grpc
    PATHS ${GRPC_LIBRARY_DIRS}
    PATH_SUFFIXES lib
)

find_library(GRPCPP_LIBRARY
    NAMES grpc++
    PATHS ${GRPCPP_LIBRARY_DIRS}
    PATH_SUFFIXES lib
)

find_library(GRPCPP_REFLECTION_LIBRARY
    NAMES grpc++_reflection
    PATHS ${GRPCPP_REFLECTION_LIBRARY_DIRS}
    PATH_SUFFIXES lib
)

# Find gRPC headers
find_path(GRPC_INCLUDE_DIR
    NAMES grpc/grpc.h
    PATHS ${GRPC_INCLUDE_DIRS}
    PATH_SUFFIXES include
)

find_path(GRPCPP_INCLUDE_DIR
    NAMES grpcpp/grpcpp.h
    PATHS ${GRPCPP_INCLUDE_DIRS}
    PATH_SUFFIXES include
)

# Find protoc-gen-grpc-cpp plugin
find_program(GRPC_CPP_PLUGIN
    NAMES grpc_cpp_plugin
    PATHS ${GRPC_CPP_PLUGIN_DIRS}
    PATH_SUFFIXES bin
)

# Set up imported targets
if(GRPC_LIBRARY AND GRPCPP_LIBRARY AND GRPC_INCLUDE_DIR AND GRPCPP_INCLUDE_DIR)
    set(gRPC_FOUND TRUE)
    
    # Create imported target for grpc
    if(NOT TARGET gRPC::grpc)
        add_library(gRPC::grpc UNKNOWN IMPORTED)
        set_target_properties(gRPC::grpc PROPERTIES
            IMPORTED_LOCATION "${GRPC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GRPC_INCLUDE_DIR}"
        )
    endif()
    
    # Create imported target for grpc++
    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ UNKNOWN IMPORTED)
        set_target_properties(gRPC::grpc++ PROPERTIES
            IMPORTED_LOCATION "${GRPCPP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GRPCPP_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "gRPC::grpc"
        )
    endif()
    
    # Create imported target for grpc++_reflection if found
    if(GRPCPP_REFLECTION_LIBRARY AND NOT TARGET gRPC::grpc++_reflection)
        add_library(gRPC::grpc++_reflection UNKNOWN IMPORTED)
        set_target_properties(gRPC::grpc++_reflection PROPERTIES
            IMPORTED_LOCATION "${GRPCPP_REFLECTION_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GRPCPP_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "gRPC::grpc++"
        )
    endif()
    
    # Set variables for protobuf generation
    if(GRPC_CPP_PLUGIN)
        set(GRPC_CPP_PLUGIN_EXECUTABLE ${GRPC_CPP_PLUGIN})
    endif()
    
else()
    set(gRPC_FOUND FALSE)
endif()

# Handle the QUIET and REQUIRED arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(gRPC
    REQUIRED_VARS GRPC_LIBRARY GRPCPP_LIBRARY GRPC_INCLUDE_DIR GRPCPP_INCLUDE_DIR
    FOUND_VAR gRPC_FOUND
)

mark_as_advanced(
    GRPC_LIBRARY
    GRPCPP_LIBRARY
    GRPCPP_REFLECTION_LIBRARY
    GRPC_INCLUDE_DIR
    GRPCPP_INCLUDE_DIR
    GRPC_CPP_PLUGIN
) 