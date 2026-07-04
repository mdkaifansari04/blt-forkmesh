cmake_minimum_required(VERSION 3.16)
project(ForkMesh VERSION 0.5.58 LANGUAGES CXX)

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
    src/CrashHandler.h
    src/CrashHandler.cpp
    src/PlatformLogFilter.h
    src/PlatformLogFilter.cpp
    src/SingleInstance.h
    src/SingleInstance.cpp
    src/Theme.h
    src/ForkMeshIdentity.h
    src/ForkMeshIdentity.cpp
    src/ChatBackend.h
    src/RoomCrypto.h
    src/RoomCrypto.cpp
    src/CoveCrypto.h
    src/CoveCrypto.cpp
    src/MirrorCrypto.h
    src/MirrorCrypto.cpp
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
    src/MarkupCanvas.h
    src/MarkupCanvas.cpp
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
    src/ScreenshotMarkupWindow.h
    src/ScreenshotMarkupWindow.cpp
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
    src/MainWindowShortcuts.cpp
    src/MainWindowSearch.cpp
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
    src/forkmesh_autogen.cpp
)

# The app compiles its Qt meta-object code through the single, checked-in
# src/forkmesh_autogen.cpp above rather than AUTOMOC's machine-generated
# forkmesh_autogen/mocs_compilation.cpp (which lands under a hashed build
# directory and stands out from the hand-written sources). Turn AUTOMOC off for
# just this target and moc its Q_OBJECT headers explicitly; forkmesh_autogen.cpp
# #includes the results. The test targets keep AUTOMOC so their
# FORKMESH_WINDOW_TESTS-only Q_INVOKABLE hooks are still moc'd correctly.
set_target_properties(forkmesh PROPERTIES AUTOMOC OFF)
qt_wrap_cpp(FORKMESH_MOC_SOURCES
    src/ActionRunner.h
    src/AgentRunner.h
    src/BackoffNetworkAccessManager.h
    src/ChatBackend.h
    src/ClaudeIdeBridge.h
    src/ClaudeStreamSession.h
    src/ClaudeTranscriptView.h
    src/HeadlessConsole.h
    src/KebabHeaderView.h
    src/MainWindow.h
    src/MarkdownEditor.h
    src/MarkupCanvas.h
    src/MessageRow.h
    src/RepoHost.h
    src/ScreenCaptureOverlay.h
    src/ScreenDrawOverlay.h
    src/ScreenshotMarkupWindow.h
    src/ScrollJumpButtons.h
    src/ServerNode.h
    src/StallWatchdog.h
    src/TerminalWidget.h
    TARGET forkmesh
)
# The generated units are #included by forkmesh_autogen.cpp, so keep them out of
# the object list (HEADER_FILE_ONLY) and make the unity file depend on them so
# they are regenerated whenever a header changes.
set_source_files_properties(${FORKMESH_MOC_SOURCES} PROPERTIES HEADER_FILE_ONLY ON)
set_source_files_properties(src/forkmesh_autogen.cpp PROPERTIES
    OBJECT_DEPENDS "${FORKMESH_MOC_SOURCES}")
target_sources(forkmesh PRIVATE ${FORKMESH_MOC_SOURCES})
# forkmesh_autogen.cpp #includes the moc output by bare name, and each moc unit
# in turn #includes its header, so both the moc output dir(s) and the src tree
# must be on the include path now that AUTOMOC no longer manages them.
set(FORKMESH_MOC_DIRS "")
foreach(_moc ${FORKMESH_MOC_SOURCES})
    get_filename_component(_moc_dir "${_moc}" DIRECTORY)
    list(APPEND FORKMESH_MOC_DIRS "${_moc_dir}")
endforeach()
list(REMOVE_DUPLICATES FORKMESH_MOC_DIRS)
# qt_wrap_cpp() above marks each INPUT header it moc's with the source
# property SKIP_AUTOMOC ON (see Qt6CoreMacros.cmake) -- note FORKMESH_MOC_SOURCES
# itself now holds the *generated* moc_*.cpp outputs, not these headers. Source
# file properties are directory-scoped, not target-scoped, so that also
# suppresses AUTOMOC for every one of these headers when an AUTOMOC-enabled
# test target below lists it as a plain source (every one of them is also
# part of FORKMESH_APP_SOURCES) -- their Q_OBJECT vtables/staticMetaObjects
# never get moc'd there, leaving forkmesh-tests/forkmesh-window-tests/
# forkmesh-perf-tests with undefined references at link time (caught building
# forkmesh-perf-tests for issue #357 from a clean configure --
# forkmesh-window-tests has the exact same latent bug). The forkmesh target
# doesn't need SKIP_AUTOMOC (its whole-target AUTOMOC is already OFF), so it's
# safe to reset it here for the still-AUTOMOC-enabled test targets.
set_source_files_properties(
    src/ActionRunner.h
    src/AgentRunner.h
    src/BackoffNetworkAccessManager.h
    src/ChatBackend.h
    src/ClaudeIdeBridge.h
    src/ClaudeStreamSession.h
    src/ClaudeTranscriptView.h
    src/HeadlessConsole.h
    src/KebabHeaderView.h
    src/MainWindow.h
    src/MarkdownEditor.h
    src/MarkupCanvas.h
    src/MessageRow.h
    src/RepoHost.h
    src/ScreenCaptureOverlay.h
    src/ScreenDrawOverlay.h
    src/ScreenshotMarkupWindow.h
    src/ScrollJumpButtons.h
    src/ServerNode.h
    src/StallWatchdog.h
    src/TerminalWidget.h
    PROPERTIES SKIP_AUTOMOC OFF SKIP_AUTOGEN OFF
)
target_include_directories(forkmesh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${FORKMESH_MOC_DIRS})

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
        src/BackoffNetworkAccessManager.h
        src/BackoffNetworkAccessManager.cpp
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
        src/MirrorCrypto.h
        src/MirrorCrypto.cpp
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

    # Nightly end-to-end mesh-loop test (issue #352): drives the real client
    # tunnel/host + signed-issue code against an in-process relay stub through
    # the full publish -> browse -> clone -> issue -> agent PR -> merge loop.
    # A deterministic stub agent stands in for the model so the run never needs
    # an API quota or the network.
    qt_add_executable(forkmesh-e2e-stub-agent
        tests/e2e_stub_agent.cpp
    )
    target_link_libraries(forkmesh-e2e-stub-agent PRIVATE Qt6::Core)
    if(APPLE)
        set_target_properties(forkmesh-e2e-stub-agent PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    qt_add_executable(forkmesh-e2e
        tests/test_mesh_e2e.cpp
        src/BackoffNetworkAccessManager.h
        src/BackoffNetworkAccessManager.cpp
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
        src/MirrorCrypto.h
        src/MirrorCrypto.cpp
        src/RepoHost.h
        src/RepoHost.cpp
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
    target_link_libraries(forkmesh-e2e PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    # RepoHost.h's SKIP_AUTOMOC is already cleared for every AUTOMOC-enabled
    # test target above (it's one of the qt_wrap_cpp() inputs reset alongside
    # MainWindow.h/ChatBackend.h/etc.).
    if(APPLE)
        set_target_properties(forkmesh-e2e PROPERTIES MACOSX_BUNDLE FALSE)
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

    # A third suite (issue #357): performance budgets for startup and
    # repo-detail tab switches. Kept in its own binary rather than folded into
    # forkmesh-window-tests, which is known-flaky in some sandboxed
    # environments — a perf gate must be individually skippable
    # (FORKMESH_SKIP_PERF_TESTS=1) without disabling window-tests' functional
    # coverage.
    qt_add_executable(forkmesh-perf-tests
        tests/test_perf_budgets.cpp
        ${FORKMESH_APP_SOURCES}
    )
    target_link_libraries(forkmesh-perf-tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg OpenSSL::Crypto)
    if(UNIX AND NOT APPLE)
        target_link_libraries(forkmesh-perf-tests PRIVATE util Qt6::DBus)
        target_compile_definitions(forkmesh-perf-tests PRIVATE FORKMESH_HAVE_PORTAL)
    endif()
    target_compile_definitions(forkmesh-perf-tests PRIVATE FORKMESH_WINDOW_TESTS)
    if(APPLE)
        set_target_properties(forkmesh-perf-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Register all suites with CTest so they run independently of the app:
    #   ctest --test-dir build            (runs the already-built binaries)
    #   cmake --build build --target check (builds the tests, then runs them)
    # The headless smoke tests need an offscreen Qt platform.
    enable_testing()
    add_test(NAME forkmesh-tests COMMAND forkmesh-tests)
    add_test(NAME forkmesh-ide-tests COMMAND forkmesh-ide-tests)
    add_test(NAME forkmesh-window-tests COMMAND forkmesh-window-tests)
    add_test(NAME forkmesh-perf-tests COMMAND forkmesh-perf-tests)
    add_test(NAME forkmesh-e2e COMMAND forkmesh-e2e)
    set_tests_properties(forkmesh-tests forkmesh-window-tests forkmesh-perf-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # The e2e loop drives real git subprocesses and needs the stub agent's path.
    set_tests_properties(forkmesh-e2e PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FORKMESH_E2E_STUB_AGENT=$<TARGET_FILE:forkmesh-e2e-stub-agent>")
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL)
    add_dependencies(check forkmesh-tests forkmesh-ide-tests forkmesh-window-tests
        forkmesh-perf-tests forkmesh-e2e forkmesh-e2e-stub-agent)
endif()
