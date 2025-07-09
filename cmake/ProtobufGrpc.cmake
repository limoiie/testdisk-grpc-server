# ProtobufGrpc.cmake - Custom protobuf and gRPC generation functions

# Function to generate protobuf C++ files
function(protobuf_generate_cpp PROTO_SRCS PROTO_HDRS)
    if(NOT ARGN)
        message(SEND_ERROR "Error: protobuf_generate_cpp() called without any proto files")
        return()
    endif()

    if(PROTOBUF_GENERATE_CPP_APPEND_PATH)
        set(PROTO_FILES "")
        foreach(FIL ${ARGN})
            get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
            get_filename_component(ABS_PATH ${ABS_FIL} PATH)
            list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
            if(${_contains_already} EQUAL -1)
                list(APPEND _protobuf_include_path -I ${ABS_PATH})
            endif()
            list(APPEND PROTO_FILES ${ABS_FIL})
        endforeach()
    else()
        set(PROTO_FILES ${ARGN})
    endif()

    set(${PROTO_SRCS})
    set(${PROTO_HDRS})

    foreach(FIL ${PROTO_FILES})
        get_filename_component(FIL_WE ${FIL} NAME_WE)
        get_filename_component(FIL_DIR ${FIL} DIRECTORY)

        list(APPEND ${PROTO_SRCS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc")
        list(APPEND ${PROTO_HDRS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h")

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc"
                   "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h"
            COMMAND protobuf::protoc
            ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto ${FIL}
            DEPENDS ${FIL} protobuf::protoc
            COMMENT "Running C++ protocol buffer compiler on ${FIL}"
            VERBATIM)
    endforeach()

    set_source_files_properties(${${PROTO_SRCS}} ${${PROTO_HDRS}} PROPERTIES GENERATED TRUE)
    set(${PROTO_SRCS} ${${PROTO_SRCS}} PARENT_SCOPE)
    set(${PROTO_HDRS} ${${PROTO_HDRS}} PARENT_SCOPE)
endfunction()

# Function to generate gRPC C++ files
function(protobuf_generate_grpc_cpp GRPC_SRCS GRPC_HDRS)
    if(NOT ARGN)
        message(SEND_ERROR "Error: protobuf_generate_grpc_cpp() called without any proto files")
        return()
    endif()

    if(PROTOBUF_GENERATE_CPP_APPEND_PATH)
        set(PROTO_FILES "")
        foreach(FIL ${ARGN})
            get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
            get_filename_component(ABS_PATH ${ABS_FIL} PATH)
            list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
            if(${_contains_already} EQUAL -1)
                list(APPEND _protobuf_include_path -I ${ABS_PATH})
            endif()
            list(APPEND PROTO_FILES ${ABS_FIL})
        endforeach()
    else()
        set(PROTO_FILES ${ARGN})
    endif()

    set(${GRPC_SRCS})
    set(${GRPC_HDRS})

    foreach(FIL ${PROTO_FILES})
        get_filename_component(FIL_WE ${FIL} NAME_WE)
        get_filename_component(FIL_DIR ${FIL} DIRECTORY)

        list(APPEND ${GRPC_SRCS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.cc")
        list(APPEND ${GRPC_HDRS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.h")

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.cc"
                   "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.grpc.pb.h"
            COMMAND protobuf::protoc
            ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
                 --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
                 --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto ${FIL}
            DEPENDS ${FIL} protobuf::protoc
            COMMENT "Running gRPC protocol buffer compiler on ${FIL}"
            VERBATIM)
    endforeach()

    set_source_files_properties(${${GRPC_SRCS}} ${${GRPC_HDRS}} PROPERTIES GENERATED TRUE)
    set(${GRPC_SRCS} ${${GRPC_SRCS}} PARENT_SCOPE)
    set(${GRPC_HDRS} ${${GRPC_HDRS}} PARENT_SCOPE)
endfunction() 