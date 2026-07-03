cmake_minimum_required(VERSION 3.16)
project(ForkMesh VERSION 0.5.37 LANGUAGES CXX)

# The version stamped into the binary. It tracks the project() version above on
# every configure, so bumping that line (e.g. cutting a release) immediately
# updates the version the app reports — including the window title — even when an
# existing build dir is reconfigured in place by the in-app quick-update. The
# release workflow can still pin an exact tag with -DFORKMESH_VERSION_OVERRIDE=<tag>
# so the published artifact reports the release tag it was cut from (see
# .forkmesh/release.yml).
#
# FORKMESH_VERSION itself must NOT be a CACHE variable: a cached default is
# written once at the first configure and never overwritten on later configures
# (set(... CACHE ...) leaves an existing cache entry alone), so a reused build
# dir would keep baking the original version into the binary and the title would
# keep showing the old version after a release. The override is a separate cache
# variable, and dereferencing FORKMESH_VERSION below reads this normal variable,
# which shadows any stale FORKMESH_VERSION left in an older build dir's cache.
set(FORKMESH_VERSION_OVERRIDE "" CACHE STRING
    "Pin the version stamped into the binary (release workflow); empty tracks project()")
if(FORKMESH_VERSION_OVERRIDE)
    set(FORKMESH_VERSION "${FORKMESH_VERSION_OVERRIDE}")
else()
    set(FORKMESH_VERSION "${PROJECT_VERSION}")
endif()

# Baked into the binaries: shown in the UI, and the quick-update button uses
# the source dir to git pull + rebuild when running from a checkout.
add_compile_definitions(
    FORKMESH_VERSION="${FORKMESH_VERSION}"
    FORKMESH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Svg)
find_package(OpenSSL REQUIRED)

# On Linux the screen-region capture must go through the XDG Desktop Portal
# (direct grabs return black under Wayland), which needs Qt's D-Bus module.
# Other platforms grab the screen directly, so it's a Linux-only dependency.
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
endif()

set(FORKMESH_APP_SOURCES
    resources/icons.qrc
    src/PlatformLogFilter.h
    src/PlatformLogFilter.cpp
    src/Theme.h
    src/ForkMeshIdentity.h
    src/ForkMeshIdentity.cpp
    src/ChatBackend.h
    src/RoomCrypto.h
    src/RoomCrypto.cpp
    src/CoveCrypto.h
    src/CoveCrypto.cpp
    src/CoveStore.h
    src/CoveStore.cpp
    src/ServerNode.h
    src/ServerNode.cpp
    src/NetworkBackoff.h
    src/BackoffNetworkAccessManager.h
    src/BackoffNetworkAccessManager.cpp
    src/RepoHost.h
    src/RepoHost.cpp
    src/RepoSecurity.h
    src/RepoSecurity.cpp
    src/IssueStore.h
    src/IssueStore.cpp
    src/IssueBurnup.h
    src/IssueBurnup.cpp
    src/DiscussionStore.h
    src/DiscussionStore.cpp
    src/PullStore.h
    src/PullStore.cpp
    src/PullReviewModel.h
    src/PullReviewModel.cpp
    src/PullAiReview.h
    src/PullAiReview.cpp
    src/CommitCommentStore.h
    src/CommitCommentStore.cpp
    src/ReferenceLinks.h
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
    src/ReactionEmoji.h
    src/ReactionEmoji.cpp
    src/MessageRow.h
    src/MessageRow.cpp
    src/MarkdownEditor.cpp
    src/KebabHeaderView.h
    src/KebabHeaderView.cpp
    src/TerminalWidget.h
    src/TerminalWidget.cpp
    src/ClaudeIdeBridge.h
    src/ClaudeIdeBridge.cpp
    src/ClaudeStreamSession.h
    src/ClaudeStreamSession.cpp
    src/ClaudeTranscriptView.h
    src/ClaudeTranscriptView.cpp
    src/ClaudeSessionScan.h
    src/ClaudeSessionScan.cpp
    src/ScrollJumpButtons.h
    src/ScrollJumpButtons.cpp
    src/ScreenCaptureOverlay.h
    src/ScreenCaptureOverlay.cpp
    src/ScreenDrawOverlay.h
    src/ScreenDrawOverlay.cpp
    src/ScreenAlignmentTarget.h
    src/ScreenAlignmentTarget.cpp
    src/StallWatchdog.h
    src/StallWatchdog.cpp
    src/SystemStats.h
    src/SystemStats.cpp
    src/MainWindowInternal.h
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindowShared.cpp
    src/MainWindowSetup.cpp
    src/MainWindowChat.cpp
    src/MainWindowIssues.cpp
    src/MainWindowSourceControl.cpp
    src/MainWindowDiscussions.cpp
    src/MainWindowPulls.cpp
    src/MainWindowAgents.cpp
    src/MainWindowRepoDetail.cpp
    src/MainWindowBranches.cpp
    src/MainWindowReleases.cpp
    src/MainWindowSettings.cpp
    src/MainWindowData.cpp
    src/MainWindowMessages.cpp
    src/MainWindowRepos.cpp
    src/MainWindowActions.cpp
    src/MainWindowCoves.cpp
    src/HeadlessConsole.h
    src/HeadlessConsole.cpp
)

qt_add_executable(forkmesh
    src/main.cpp
    ${FORKMESH_APP_SOURCES}
)

target_link_libraries(forkmesh PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg OpenSSL::Crypto)
# TerminalWidget runs commands under a PTY via forkpty(); on Linux that lives in
# libutil (macOS provides it in libSystem, so no extra link there).
if(UNIX AND NOT APPLE)
    target_link_libraries(forkmesh PRIVATE util)
    # XDG Desktop Portal screenshot path (see ScreenCaptureOverlay).
    target_link_libraries(forkmesh PRIVATE Qt6::DBus)
    target_compile_definitions(forkmesh PRIVATE FORKMESH_HAVE_PORTAL)
    # Export symbols so the StallWatchdog's backtrace_symbols() resolves function
    # names (not just addresses) when it samples a frozen UI thread.
    target_link_options(forkmesh PRIVATE -rdynamic)
endif()

if(APPLE)
    set_target_properties(forkmesh PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ForkMesh"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.forkmesh.client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${FORKMESH_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${FORKMESH_VERSION}"
        OUTPUT_NAME "ForkMesh"
    )
endif()

option(FORKMESH_BUILD_TESTS "Build the headless smoke tests" ON)
if(FORKMESH_BUILD_TESTS)
    qt_add_executable(forkmesh-tests
        tests/test_crypto.cpp
        resources/icons.qrc
        src/ActionFile.h
        src/ActionFile.cpp
        src/ForkMeshIdentity.h
        src/ForkMeshIdentity.cpp
        src/RoomCrypto.h
        src/RoomCrypto.cpp
        src/CoveCrypto.h
        src/CoveCrypto.cpp
        src/CoveStore.h
        src/CoveStore.cpp
        src/IssueStore.h
        src/IssueStore.cpp
        src/IssueBurnup.h
        src/IssueBurnup.cpp
        src/DiscussionStore.h
        src/DiscussionStore.cpp
        src/PullStore.h
        src/PullStore.cpp
        src/PullReviewModel.h
        src/PullReviewModel.cpp
        src/PullAiReview.h
        src/PullAiReview.cpp
        src/CommitCommentStore.h
        src/CommitCommentStore.cpp
        src/AgentStore.h
        src/AgentStore.cpp
        src/ActionStore.h
        src/ActionStore.cpp
        src/RepoSecurity.h
        src/RepoSecurity.cpp
        src/ReferenceLinks.h
    )
    target_link_libraries(forkmesh-tests PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # A focused suite for the Claude Code IDE bridge: drives its WebSocket /
    # JSON-RPC server the way the CLI does (no Widgets needed).
    qt_add_executable(forkmesh-ide-tests
        tests/test_ide_bridge.cpp
        src/ClaudeIdeBridge.h
        src/ClaudeIdeBridge.cpp
    )
    target_link_libraries(forkmesh-ide-tests PRIVATE Qt6::Core Qt6::Network)
    if(APPLE)
        set_target_properties(forkmesh-ide-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # A second suite that drives the real MainWindow (needs the full app sources).
    qt_add_executable(forkmesh-window-tests
        tests/test_window_resize.cpp
        ${FORKMESH_APP_SOURCES}
    )
    target_link_libraries(forkmesh-window-tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg OpenSSL::Crypto)
    if(UNIX AND NOT APPLE)
        target_link_libraries(forkmesh-window-tests PRIVATE util Qt6::DBus)
        target_compile_definitions(forkmesh-window-tests PRIVATE FORKMESH_HAVE_PORTAL)
    endif()
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
    add_test(NAME forkmesh-ide-tests COMMAND forkmesh-ide-tests)
    add_test(NAME forkmesh-window-tests COMMAND forkmesh-window-tests)
    set_tests_properties(forkmesh-tests forkmesh-window-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL)
    add_dependencies(check forkmesh-tests forkmesh-ide-tests forkmesh-window-tests)
endif()
