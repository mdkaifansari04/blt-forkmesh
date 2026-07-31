cmake_minimum_required(VERSION 3.16)
project(ForkMesh VERSION 0.7.6 LANGUAGES CXX)

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

# Embed exact code-source provenance independently of semver. Metadata-only
# commits under .forkmesh/releases are excluded because they land after the
# artifact exists. The generated header is refreshed before every build, so a
# dirty tracked OR untracked relevant file produces "dirty" rather than letting
# a development binary falsely claim the last committed revision.
set(FORKMESH_BUILD_COMMIT_OVERRIDE "" CACHE STRING
    "Exact Git commit embedded in the binary; required for archive release builds")
if(FORKMESH_BUILD_COMMIT_OVERRIDE)
    string(TOLOWER "${FORKMESH_BUILD_COMMIT_OVERRIDE}"
           FORKMESH_BUILD_COMMIT_OVERRIDE)
    string(LENGTH "${FORKMESH_BUILD_COMMIT_OVERRIDE}"
           FORKMESH_BUILD_COMMIT_OVERRIDE_LENGTH)
    if(NOT FORKMESH_BUILD_COMMIT_OVERRIDE MATCHES "^[0-9a-f]+$" OR
       (NOT FORKMESH_BUILD_COMMIT_OVERRIDE_LENGTH EQUAL 40 AND
        NOT FORKMESH_BUILD_COMMIT_OVERRIDE_LENGTH EQUAL 64))
        message(FATAL_ERROR
            "FORKMESH_BUILD_COMMIT_OVERRIDE must be an exact 40- or 64-hex Git commit")
    endif()
endif()

# Baked into the binaries: the quick-update button uses the source dir to
# git pull + rebuild when running from a checkout. Global is fine here — the
# value never changes between builds of the same checkout, so it never
# invalidates incremental builds. (It also must stay global: the inline
# updateClientDir() in MainWindowInternal.h reads it, so every TU has to see
# the same value or the ODR is violated.)
add_compile_definitions(
    FORKMESH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# FORKMESH_VERSION changes on every release bump, so it must NOT be on compile
# command lines at all — as a global definition it turned every bump into a
# full rebuild of every target, and even a per-source COMPILE_DEFINITIONS
# property rewrites the target's build.make, which every object in the target
# depends on under the Makefile generator (same full rebuild). Instead it goes
# into a generated header that only the handful of TUs reading the version
# include; configure_file rewrites it only when the version actually changes,
# so a bump recompiles just those files.
configure_file(
    cmake/GenerateForkMeshVersion.cmake.in
    GenerateForkMeshVersion.cmake
    @ONLY)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_CURRENT_BINARY_DIR}/GenerateForkMeshVersion.cmake"
    RESULT_VARIABLE FORKMESH_VERSION_GENERATE_RESULT)
if(NOT FORKMESH_VERSION_GENERATE_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not generate ForkMesh build provenance")
endif()
add_custom_target(forkmesh-version-header ALL
    COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_CURRENT_BINARY_DIR}/GenerateForkMeshVersion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/ForkMeshVersion.h"
    COMMENT "Refreshing ForkMesh source provenance")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Wrap compilation in ccache when available: object files survive build-dir
# wipes (run.sh rebuild) and branch switches, so those come back warm.
find_program(FORKMESH_CCACHE ccache)
if(FORKMESH_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${FORKMESH_CCACHE}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Svg Concurrent)
find_package(OpenSSL REQUIRED)
include(GNUInstallDirs)

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
    src/AccountCapability.h
    src/ForkMeshIdentity.h
    src/ForkMeshIdentity.cpp
    src/ChatBackend.h
    src/MainnodeRoom.h
    src/RoomCrypto.h
    src/RoomCrypto.cpp
    src/CoveCrypto.h
    src/CoveCrypto.cpp
    src/MirrorCrypto.h
    src/MirrorCrypto.cpp
    src/PrivateMirrorStore.h
    src/PrivateMirrorStore.cpp
    src/PrivateMirrorRuntime.h
    src/PrivateMirrorRuntime.cpp
    src/PublicMirrorRuntime.h
    src/PublicMirrorRuntime.cpp
    src/UpstreamCheckoutSync.h
    src/UpstreamCheckoutSync.cpp
    src/CoveStore.h
    src/CoveStore.cpp
    src/ChatHistoryLimits.h
    src/ChatVisitorPresence.h
    src/ServerNode.h
    src/ServerNode.cpp
    src/OfficeChannelMirror.h
    src/OfficeChannelMirror.cpp
    src/NodeEventSocket.h
    src/NodeEventSocket.cpp
    src/BackgroundActivity.h
    src/ActionTelemetry.h
    src/ActionTelemetry.cpp
    src/NetworkBackoff.h
    src/BackoffNetworkAccessManager.h
    src/BackoffNetworkAccessManager.cpp
    src/ControlNode.h
    src/ControlNode.cpp
    src/MirrorActionsConfiguration.h
    src/MirrorActionsConfiguration.cpp
    src/MirrorActionsSummary.h
    src/MirrorActionsSummary.cpp
    src/RewardPoolSigner.h
    src/RewardPoolSigner.cpp
    src/RepoHost.h
    src/RepoHost.cpp
    src/RepoContributionSnapshot.h
    src/RepoContributionSnapshotInternal.h
    src/RepoContributionSnapshot.cpp
    src/StrictGitReader.h
    src/StrictGitReader.cpp
    src/RepoSecurity.h
    src/RepoSecurity.cpp
    src/IssueStore.h
    src/IssueStore.cpp
    src/IssueBurnup.h
    src/IssueBurnup.cpp
    src/ProjectStore.h
    src/ProjectStore.cpp
    src/ProjectGantt.h
    src/DiscussionStore.h
    src/DiscussionStore.cpp
    src/FederatedThreadView.h
    src/FederatedThreadView.cpp
    src/PullStore.h
    src/PullStore.cpp
    src/PullReviewModel.h
    src/PullReviewModel.cpp
    src/PullAiReview.h
    src/PullAiReview.cpp
    src/PullBadgeWidget.h
    src/PullBadgeWidget.cpp
    src/PacmanProgress.h
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
    src/AgentJail.h
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
    src/WorldSpeechBridge.h
    src/WorldSpeechBridge.cpp
    src/ClaudeStreamSession.h
    src/ClaudeStreamSession.cpp
    src/CodexAppServerSession.h
    src/CodexAppServerSession.cpp
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
    src/StallWatchdog.h
    src/StallWatchdog.cpp
    src/SystemStats.h
    src/SystemStats.cpp
    src/LocalBackupStore.h
    src/LocalBackupStore.cpp
    src/McpConnector.h
    src/McpConnector.cpp
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
    src/MainWindowProjects.cpp
    src/MainWindowBranches.cpp
    src/MainWindowReleases.cpp
    src/MainWindowShortcuts.cpp
    src/MainWindowSearch.cpp
    src/MainWindowSettings.cpp
    src/MainWindowMcp.cpp
    src/MainWindowData.cpp
    src/MainWindowMessages.cpp
    src/MainWindowRepos.cpp
    src/MainWindowActions.cpp
    src/MainWindowCoves.cpp
    src/MainWindowControlNode.cpp
    src/MainWindowRewardPool.cpp
    src/HeadlessConsole.h
    src/HeadlessConsole.cpp
)

qt_add_executable(forkmesh
    src/main.cpp
    ${FORKMESH_APP_SOURCES}
    src/forkmesh_autogen.cpp
)
add_dependencies(forkmesh forkmesh-version-header)

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
    src/WorldSpeechBridge.h
    src/ClaudeStreamSession.h
    src/ClaudeTranscriptView.h
    src/HeadlessConsole.h
    src/KebabHeaderView.h
    src/MainWindow.h
    src/MarkdownEditor.h
    src/MarkupCanvas.h
    src/MessageRow.h
    src/NodeEventSocket.h
    src/OfficeChannelMirror.h
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
# CodexAppServerSession is compiled as a regular moc unit rather than included
# by the checked-in forkmesh_autogen.cpp, keeping this transport self-contained.
qt_wrap_cpp(FORKMESH_CODEX_MOC_SOURCES
    src/CodexAppServerSession.h
    TARGET forkmesh
)
target_sources(forkmesh PRIVATE ${FORKMESH_CODEX_MOC_SOURCES})
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
    src/WorldSpeechBridge.h
    src/ClaudeStreamSession.h
    src/CodexAppServerSession.h
    src/ClaudeTranscriptView.h
    src/HeadlessConsole.h
    src/KebabHeaderView.h
    src/MainWindow.h
    src/MarkdownEditor.h
    src/MarkupCanvas.h
    src/MessageRow.h
    src/NodeEventSocket.h
    src/OfficeChannelMirror.h
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

target_link_libraries(forkmesh PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent OpenSSL::Crypto)
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

# The file/folder tree icons under ../icons/ are loaded at runtime through
# MainWindow::iconsDir() (MainWindowRepoDetail.cpp), which falls back to a
# loose "icons" directory beside the executable when it isn't running from a
# source checkout. None of the packagers (AppImage, NSIS, .dmg) or the bare
# published binary ship that directory alongside the executable, so those
# icons never appeared on anything but a from-source dev build (issue #417).
# Bake the whole set into the binary as a Qt resource instead, so it's always
# present regardless of how the app was installed.
file(GLOB FORKMESH_TREE_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../icons/*.svg")
qt_add_resources(forkmesh "treeicons"
    PREFIX "/icons/tree"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../icons"
    BIG_RESOURCES
    FILES ${FORKMESH_TREE_ICON_FILES}
)

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

# A release install must contain everything the token-only Cloudflare flow reads
# from the ForkMesh checkout. Keep the same sibling layout as the source tree:
# <resource-root>/{tools,cloudflare_worker}. The Python launchers can therefore
# resolve Worker source, generated/static assets, migrations, and their pinned
# pywrangler wrapper without a developer checkout or an absolute build path.
if(APPLE)
    set(FORKMESH_RESOURCE_INSTALL_ROOT
        "ForkMesh.app/Contents/Resources/forkmesh")
    install(TARGETS forkmesh BUNDLE DESTINATION ".")
else()
    set(FORKMESH_RESOURCE_INSTALL_ROOT
        "${CMAKE_INSTALL_DATADIR}/forkmesh")
    install(TARGETS forkmesh RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(PROGRAMS
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/cloudflare_bootstrap.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/cloudflare_tunnel_bootstrap.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/cloudflared_install.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/headless_mirror_refresh.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/hosted_repository_sync.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/mirror_gateway.py"
    # Settings -> MCP points external agents at this script, so an installed
    # client has to ship it too, not just a source checkout.
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/forkmesh_mcp_server.py"
    DESTINATION "${FORKMESH_RESOURCE_INSTALL_ROOT}/tools")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/wrangler.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/pyproject.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/uv.lock"
    DESTINATION "${FORKMESH_RESOURCE_INSTALL_ROOT}/cloudflare_worker")
install(PROGRAMS
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/pywrangler.sh"
    # The control node's "Deploy to Cloudflare" button runs this script, so an
    # installed client ships it beside the Worker bundle it deploys.
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/deploy.sh"
    DESTINATION "${FORKMESH_RESOURCE_INSTALL_ROOT}/cloudflare_worker")
install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/public"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/migrations"
    DESTINATION "${FORKMESH_RESOURCE_INSTALL_ROOT}/cloudflare_worker"
    PATTERN "__pycache__" EXCLUDE
    PATTERN ".pytest_cache" EXCLUDE)
install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../cloudflare_worker/tools/"
    DESTINATION "${FORKMESH_RESOURCE_INSTALL_ROOT}/cloudflare_worker/tools"
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE)

option(FORKMESH_BUILD_TESTS "Build the headless smoke tests" ON)
if(FORKMESH_BUILD_TESTS)
    qt_add_executable(forkmesh-tests
        tests/test_crypto.cpp
        resources/icons.qrc
        src/PlatformLogFilter.h
        src/PlatformLogFilter.cpp
        src/BackgroundActivity.h
        src/ActionTelemetry.h
        src/ActionTelemetry.cpp
        src/BackoffNetworkAccessManager.h
        src/BackoffNetworkAccessManager.cpp
        src/ActionFile.h
        src/ActionFile.cpp
        src/AccountCapability.h
        src/ForkMeshIdentity.h
        src/ForkMeshIdentity.cpp
        src/RepoContributionSnapshot.h
        src/RepoContributionSnapshotInternal.h
        src/RepoContributionSnapshot.cpp
        src/StrictGitReader.h
        src/StrictGitReader.cpp
        src/RoomCrypto.h
        src/RoomCrypto.cpp
        src/CoveCrypto.h
        src/CoveCrypto.cpp
        src/CoveStore.h
        src/CoveStore.cpp
        src/MirrorCrypto.h
        src/MirrorCrypto.cpp
        src/PrivateMirrorStore.h
        src/PrivateMirrorStore.cpp
        src/IssueStore.h
        src/IssueStore.cpp
        src/IssueBurnup.h
        src/IssueBurnup.cpp
        src/ProjectStore.h
        src/ProjectStore.cpp
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
        src/LocalBackupStore.h
        src/LocalBackupStore.cpp
        src/McpConnector.h
        src/McpConnector.cpp
        src/ReferenceLinks.h
        src/ChatHistoryLimits.h
        src/ChatVisitorPresence.h
    )
    target_link_libraries(forkmesh-tests PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Focused local control-node contracts: Cloudflare tokens remain out of
    # argv/output, public fields are validated, wallet input is public-address
    # only, and the manifest signer verifies the exact payload it signs.
    qt_add_executable(forkmesh-control-tests
        tests/test_control_node.cpp
        src/ControlNode.h
        src/ControlNode.cpp
        src/MirrorActionsConfiguration.h
        src/MirrorActionsConfiguration.cpp
        src/MirrorActionsSummary.h
        src/MirrorActionsSummary.cpp
        src/ActionStore.h
        src/ActionStore.cpp
    )
    target_include_directories(forkmesh-control-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-control-tests PRIVATE Qt6::Core)
    if(APPLE)
        set_target_properties(forkmesh-control-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Desktop/public-World chat convergence: saved settings migrate only the
    # retired shared-room route, blackholed upgrades have a deadline, and every
    # failure/network transition yields one bounded reconnect.
    qt_add_executable(forkmesh-chat-transport-tests
        tests/test_chat_transport.cpp
        src/MainnodeRoom.h
        src/ChatBackend.h
        src/RoomCrypto.h
        src/RoomCrypto.cpp
        src/SystemStats.h
        src/SystemStats.cpp
        src/ServerNode.h
        src/ServerNode.cpp
        src/OfficeChannelMirror.h
        src/OfficeChannelMirror.cpp
    )
    target_include_directories(forkmesh-chat-transport-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(forkmesh-chat-transport-tests PRIVATE
        FORKMESH_SERVER_NODE_TESTS)
    target_link_libraries(forkmesh-chat-transport-tests PRIVATE
        Qt6::Core Qt6::Network OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-chat-transport-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # ForkMesh Actions trust boundary: repository-state-bound approvals plus
    # real Linux namespace/resource isolation. The integration cases execute
    # hostile workflows and prove host secrets, source writes, egress, runaway
    # files/output/processes, and deadlines fail closed.
    qt_add_executable(forkmesh-action-security-tests
        tests/test_action_security.cpp
        src/CrashHandler.h
        src/CrashHandler.cpp
        src/ActionFile.h
        src/ActionFile.cpp
        src/ActionStore.h
        src/ActionStore.cpp
        src/ActionRunner.h
        src/ActionRunner.cpp
    )
    target_include_directories(forkmesh-action-security-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-action-security-tests PRIVATE Qt6::Core)
    if(APPLE)
        set_target_properties(forkmesh-action-security-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # Private control-node runtime: device-bound encrypted identity vault,
    # opaque whole-repository replicas, per-sync key rotation, recipient
    # revocation, owner-only temporary materialization, and legacy plaintext
    # cleanup/WebSocket fail-closed behavior.
    qt_add_executable(forkmesh-private-mirror-tests
        tests/test_private_mirror_runtime.cpp
        src/CoveCrypto.h
        src/CoveCrypto.cpp
        src/MirrorCrypto.h
        src/MirrorCrypto.cpp
        src/PrivateMirrorStore.h
        src/PrivateMirrorStore.cpp
        src/PrivateMirrorRuntime.h
        src/PrivateMirrorRuntime.cpp
    )
    target_include_directories(forkmesh-private-mirror-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-private-mirror-tests PRIVATE
        Qt6::Core OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-private-mirror-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # Public mirrors are durable only as official-age encrypted tar archives.
    # The focused runtime suite uses deterministic stand-in age executables to
    # exercise vault binding, atomic sealing, safe temporary materialization,
    # gateway request validation, tamper rejection, and legacy cleanup without
    # requiring a system age installation in CI.
    qt_add_executable(forkmesh-public-mirror-tests
        tests/test_public_mirror_runtime.cpp
        src/CoveCrypto.h
        src/CoveCrypto.cpp
        src/PublicMirrorRuntime.h
        src/PublicMirrorRuntime.cpp
    )
    target_include_directories(forkmesh-public-mirror-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-public-mirror-tests PRIVATE
        Qt6::Core OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-public-mirror-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # Headless fleet nodes seal their public mirror from the installer's agent
    # checkout; this suite proves that checkout keeps tracking the upstream
    # relay clone (fast-forwards, forced pulls-metadata tracking, safe pruning,
    # divergence preservation) against real local git repositories.
    qt_add_executable(forkmesh-upstream-sync-tests
        tests/test_upstream_checkout_sync.cpp
        src/UpstreamCheckoutSync.h
        src/UpstreamCheckoutSync.cpp
    )
    target_include_directories(forkmesh-upstream-sync-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-upstream-sync-tests PRIVATE Qt6::Core)
    if(APPLE)
        set_target_properties(forkmesh-upstream-sync-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # The first-instance owner's community reward-pool key remains inside a
    # passphrase-encrypted, owner-only desktop vault. These tests exercise key
    # import, strict transaction inspection, local signing, canonical owner
    # authorization, RPC validation, and redaction without a Worker or network.
    qt_add_executable(forkmesh-reward-pool-tests EXCLUDE_FROM_ALL
        tests/test_reward_pool_signer.cpp
        src/RewardPoolSigner.h
        src/RewardPoolSigner.cpp
    )
    target_include_directories(forkmesh-reward-pool-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-reward-pool-tests PRIVATE
        Qt6::Core OpenSSL::Crypto)
    if(APPLE)
        set_target_properties(forkmesh-reward-pool-tests PROPERTIES MACOSX_BUNDLE FALSE)
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

    # Origin-bound, loopback-only browser -> local speech-to-text control. The
    # focused suite drives its real HTTP/CORS capability protocol and confirms
    # that there is no audio transport endpoint.
    qt_add_executable(forkmesh-world-speech-tests
        tests/test_world_speech_bridge.cpp
        src/WorldSpeechBridge.h
        src/WorldSpeechBridge.cpp
    )
    target_include_directories(forkmesh-world-speech-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(forkmesh-world-speech-tests PRIVATE
        Qt6::Core Qt6::Network)
    if(APPLE)
        set_target_properties(forkmesh-world-speech-tests PROPERTIES
            MACOSX_BUNDLE FALSE)
    endif()

    # Focused Codex app-server JSONL transport and normalization tests.
    qt_add_executable(forkmesh-codex-tests
        tests/test_codex_app_server.cpp
        src/CodexAppServerSession.h
        src/CodexAppServerSession.cpp
    )
    target_link_libraries(forkmesh-codex-tests PRIVATE Qt6::Core)
    if(APPLE)
        set_target_properties(forkmesh-codex-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Nightly end-to-end mesh-loop test (issue #352): drives the real client
    # control/update channel, direct HTTP repository boundary, and signed-issue
    # code through publish -> browse -> clone -> issue -> agent PR -> merge.
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
        src/BackgroundActivity.h
        src/ActionTelemetry.h
        src/ActionTelemetry.cpp
        src/BackoffNetworkAccessManager.h
        src/BackoffNetworkAccessManager.cpp
        src/ActionFile.h
        src/ActionFile.cpp
        src/ForkMeshIdentity.h
        src/ForkMeshIdentity.cpp
        src/StrictGitReader.h
        src/StrictGitReader.cpp
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
    target_link_libraries(forkmesh-window-tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent OpenSSL::Crypto)
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
    target_link_libraries(forkmesh-perf-tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent OpenSSL::Crypto)
    if(UNIX AND NOT APPLE)
        target_link_libraries(forkmesh-perf-tests PRIVATE util Qt6::DBus)
        target_compile_definitions(forkmesh-perf-tests PRIVATE FORKMESH_HAVE_PORTAL)
    endif()
    target_compile_definitions(forkmesh-perf-tests PRIVATE FORKMESH_WINDOW_TESTS)
    if(APPLE)
        set_target_properties(forkmesh-perf-tests PROPERTIES MACOSX_BUNDLE FALSE)
    endif()

    # Keep the test binaries out of the default `all` target: window-tests and
    # perf-tests each recompile every app source, so building them implicitly
    # tripled the cost of a plain `cmake --build build` (what run.sh and the
    # in-app quick-update do). They still build on demand — `--target check`
    # below depends on all of them, run.sh test and ci.yml name their targets
    # explicitly, and the release/deploy builds configure with
    # FORKMESH_BUILD_TESTS=OFF anyway.
    set_target_properties(
        forkmesh-tests forkmesh-ide-tests forkmesh-world-speech-tests
        forkmesh-codex-tests
        forkmesh-control-tests forkmesh-chat-transport-tests
        forkmesh-action-security-tests
        forkmesh-private-mirror-tests
        forkmesh-public-mirror-tests
        forkmesh-upstream-sync-tests
        forkmesh-e2e-stub-agent forkmesh-e2e
        forkmesh-window-tests forkmesh-perf-tests
        PROPERTIES EXCLUDE_FROM_ALL TRUE)

    # Register all suites with CTest so they run independently of the app:
    #   ctest --test-dir build            (runs the already-built binaries)
    #   cmake --build build --target check (builds the tests, then runs them)
    # The headless smoke tests need an offscreen Qt platform.
    enable_testing()
    add_test(NAME forkmesh-tests COMMAND forkmesh-tests)
    find_program(FORKMESH_PYTHON_EXECUTABLE NAMES python3 python)
    if(FORKMESH_PYTHON_EXECUTABLE)
        add_test(
            NAME forkmesh-agent-e2ee-worker-contract
            COMMAND "${FORKMESH_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_owner_envelope_contract.py"
                    "$<TARGET_FILE:forkmesh-tests>")
    endif()
    add_test(NAME forkmesh-ide-tests COMMAND forkmesh-ide-tests)
    add_test(NAME forkmesh-world-speech-tests
             COMMAND forkmesh-world-speech-tests)
    add_test(NAME forkmesh-codex-tests COMMAND forkmesh-codex-tests)
    add_test(NAME forkmesh-control-tests COMMAND forkmesh-control-tests)
    add_test(NAME forkmesh-chat-transport-tests
             COMMAND forkmesh-chat-transport-tests)
    add_test(NAME forkmesh-action-security-tests
             COMMAND forkmesh-action-security-tests)
    add_test(NAME forkmesh-private-mirror-tests
             COMMAND forkmesh-private-mirror-tests)
    add_test(NAME forkmesh-public-mirror-tests
             COMMAND forkmesh-public-mirror-tests)
    add_test(NAME forkmesh-upstream-sync-tests
             COMMAND forkmesh-upstream-sync-tests)
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
    add_dependencies(check forkmesh-tests forkmesh-ide-tests
        forkmesh-world-speech-tests forkmesh-codex-tests
        forkmesh-control-tests forkmesh-chat-transport-tests
        forkmesh-action-security-tests
        forkmesh-private-mirror-tests
        forkmesh-public-mirror-tests
        forkmesh-upstream-sync-tests
        forkmesh-window-tests forkmesh-perf-tests forkmesh-e2e
        forkmesh-e2e-stub-agent)

    # Generate provenance before any test target can compile a source that
    # includes ForkMeshVersion.h. Applying this uniformly also makes a
    # dirty-after-configure regression observable in focused test builds.
    foreach(_forkmesh_test_target
            forkmesh-tests forkmesh-ide-tests forkmesh-world-speech-tests
            forkmesh-codex-tests forkmesh-control-tests
            forkmesh-chat-transport-tests
            forkmesh-action-security-tests
            forkmesh-private-mirror-tests forkmesh-public-mirror-tests
            forkmesh-upstream-sync-tests
            forkmesh-reward-pool-tests forkmesh-e2e-stub-agent forkmesh-e2e
            forkmesh-window-tests forkmesh-perf-tests)
        add_dependencies(${_forkmesh_test_target} forkmesh-version-header)
    endforeach()
endif()
