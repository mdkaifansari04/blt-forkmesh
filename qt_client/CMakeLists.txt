cmake_minimum_required(VERSION 3.16)
project(ForkMesh VERSION 0.5.0 LANGUAGES CXX)

# Baked into the binaries: shown in the UI, and the quick-update button uses
# the source dir to git pull + rebuild when running from a checkout.
add_compile_definitions(
    FORKMESH_VERSION="${PROJECT_VERSION}"
    FORKMESH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Svg)
find_package(OpenSSL REQUIRED)

set(FORKMESH_APP_SOURCES
    resources/icons.qrc
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
    src/IssueBurnup.h
    src/IssueBurnup.cpp
    src/PullStore.h
    src/PullStore.cpp
    src/CommitCommentStore.h
    src/CommitCommentStore.cpp
    src/ActionFile.h
    src/ActionFile.cpp
    src/ActionStore.h
    src/ActionStore.cpp
    src/ActionRunner.h
    src/ActionRunner.cpp
    src/AgentStore.h
    src/AgentStore.cpp
    src/AgentRunner.h
    src/AgentRunner.cpp
    src/QrCode.h
    src/QrCode.cpp
    src/MessageRow.h
    src/MessageRow.cpp
    src/MarkdownEditor.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

qt_add_executable(forkmesh
    src/main.cpp
    ${FORKMESH_APP_SOURCES}
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
        resources/icons.qrc
        src/ForkMeshIdentity.h
        src/ForkMeshIdentity.cpp
        src/RoomCrypto.h
        src/RoomCrypto.cpp
        src/IssueStore.h
        src/IssueStore.cpp
        src/IssueBurnup.h
        src/IssueBurnup.cpp
        src/PullStore.h
        src/PullStore.cpp
        src/CommitCommentStore.h
        src/CommitCommentStore.cpp
    )
    target_link_libraries(forkmesh-tests PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # A second suite that drives the real MainWindow (needs the full app sources).
    qt_add_executable(forkmesh-window-tests
        tests/test_window_resize.cpp
        ${FORKMESH_APP_SOURCES}
    )
    target_link_libraries(forkmesh-window-tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg OpenSSL::Crypto)
    target_compile_definitions(forkmesh-window-tests PRIVATE FORKMESH_WINDOW_TESTS)
    if(APPLE)
        set_target_properties(forkmesh-window-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Register both suites with CTest so they run independently of the app:
    #   ctest --test-dir build            (runs the already-built binaries)
    #   cmake --build build --target check (builds the tests, then runs them)
    # The headless smoke tests need an offscreen Qt platform.
    enable_testing()
    add_test(NAME forkmesh-tests COMMAND forkmesh-tests)
    add_test(NAME forkmesh-window-tests COMMAND forkmesh-window-tests)
    set_tests_properties(forkmesh-tests forkmesh-window-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL)
    add_dependencies(check forkmesh-tests forkmesh-window-tests)
endif()
