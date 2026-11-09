# Pinned, hash-checked upstream sources. No protoc installation or network is
# required by the released app. Archives cached in build/downloads work offline.
include(FetchContent)
function(easy_dependency name archive url hash)
    set(source "${url}")
    if(EXISTS "${CMAKE_BINARY_DIR}/downloads/${archive}")
        set(source "${CMAKE_BINARY_DIR}/downloads/${archive}")
    endif()
    FetchContent_Declare(${name} URL "${source}" URL_HASH "SHA256=${hash}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
endfunction()
easy_dependency(absl abseil-20250512.1.tar.gz
    https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.tar.gz
    9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db)
easy_dependency(protobuf protobuf-34.2.tar.gz
    https://github.com/protocolbuffers/protobuf/releases/download/v34.2/protobuf-34.2.tar.gz
    d1e04237fa1a9f141f943ff0302b4fe241cd88ecd1a2e76b4bc9bb77c7fc2345)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(absl protobuf)
# libprotobuf includes the import/parser front end in this version; no protoc
# executable or language code-generator library is built or shipped.
add_library(EasyProtoImport INTERFACE)
target_link_libraries(EasyProtoImport INTERFACE protobuf::libprotobuf)
target_include_directories(EasyProtoImport SYSTEM INTERFACE "${protobuf_SOURCE_DIR}/src" "${absl_SOURCE_DIR}")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Standard imports are embedded, including their original copyright notices.
set(easy_proto_resources "<RCC><qresource prefix=\"/protocols\">")
foreach(name any api cpp_features descriptor duration empty field_mask source_context struct timestamp type wrappers)
    string(APPEND easy_proto_resources "<file alias=\"google/protobuf/${name}.proto\">${protobuf_SOURCE_DIR}/src/google/protobuf/${name}.proto</file>")
endforeach()
string(APPEND easy_proto_resources "<file alias=\"PROTOBUF-LICENSE\">${protobuf_SOURCE_DIR}/LICENSE</file><file alias=\"ABSEIL-LICENSE\">${absl_SOURCE_DIR}/LICENSE</file><file alias=\"UTF8-RANGE-LICENSE\">${protobuf_SOURCE_DIR}/third_party/utf8_range/LICENSE</file></qresource></RCC>")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/protobuf-resources.qrc" "${easy_proto_resources}")
