cmake_minimum_required(VERSION 3.16)
project(ForkMesh VERSION 0.3.0 LANGUAGES CXX)

# Baked into the binaries: shown in the UI, and the quick-update button uses
# the source dir to git pull + rebuild when running from a checkout.
add_compile_definitions(
    FORKMESH_VERSION="${PROJECT_VERSION}"
    FORKMESH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Svg)
find_package(OpenSSL REQUIRED)

qt_add_executable(forkmesh
    src/main.cpp
    src/Theme.h
    src/ForkMeshIdentity.h
    src/ForkMeshIdentity.cpp
    src/ChatBackend.h
    src/RoomCrypto.h
    src/RoomCrypto.cpp
    src/ServerNode.h
    src/ServerNode.cpp
    src/RepoHost.h
    src/RepoHost.cpp
    src/IssueStore.h
    src/IssueStore.cpp
    src/PullStore.h
    src/PullStore.cpp
    src/ActionFile.h
    src/ActionFile.cpp
    src/ActionStore.h
    src/ActionStore.cpp
    src/ActionRunner.h
    src/ActionRunner.cpp
    src/MessageRow.h
    src/MessageRow.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(forkmesh PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg OpenSSL::Crypto)

if(APPLE)
    set_target_properties(forkmesh PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ForkMesh"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.forkmesh.client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        OUTPUT_NAME "ForkMesh"
    )
endif()

option(FORKMESH_BUILD_TESTS "Build the headless smoke tests" ON)
if(FORKMESH_BUILD_TESTS)
    qt_add_executable(forkmesh-tests
        tests/test_crypto.cpp
        src/ForkMeshIdentity.h
        src/ForkMeshIdentity.cpp
        src/RoomCrypto.h
        src/RoomCrypto.cpp
        src/IssueStore.h
        src/IssueStore.cpp
    )
    target_link_libraries(forkmesh-tests PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()
endif()
