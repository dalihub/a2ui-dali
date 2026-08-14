CMAKE_MINIMUM_REQUIRED(VERSION 3.8.2)

PROJECT(a2ui-dali
  VERSION 0.18.1
  DESCRIPTION "A DALi-based renderer for the A2UI protocol"
  LANGUAGES CXX C
)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_C_STANDARD 99)

# ============================================================
# Dependencies
# ============================================================
FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(DALICORE            REQUIRED dali2-core)
PKG_CHECK_MODULES(DALIADAPTOR         REQUIRED dali2-adaptor)
PKG_CHECK_MODULES(DALI_UI_FOUNDATION  REQUIRED dali2-ui-foundation)
PKG_CHECK_MODULES(DALI_UI_COMPONENTS  REQUIRED dali2-ui-components)
PKG_CHECK_MODULES(CURL                REQUIRED libcurl)

# ============================================================
# Source groups
# ============================================================
# Core: A2UI v0.9 message parsing, data model, expression evaluation,
# action dispatch — no DALi UI dependency, used by both the renderer
# and the conformance test.
SET(A2UI_CORE_SOURCES
  src/core/a2ui-message-processor.cpp
  src/core/a2ui-payload-fixer.cpp
  src/core/a2ui-sanitizer.cpp
  src/core/a2ui-stream-parser.cpp
  src/core/action-dispatcher.cpp
  src/core/data-context.cpp
  src/core/data-model.cpp
  src/core/diff-engine.cpp
  src/core/expression-parser.cpp
  src/core/surface-components-model.cpp
  src/core/surface-group-model.cpp
  src/core/surface-model.cpp
  src/core/template-cache.cpp
)

# Renderer: DALi-side bindings.
# One file per component renderer (renderer/components/*.cpp).
FILE(GLOB A2UI_COMPONENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/components/*.cpp)

SET(A2UI_LIB_SOURCES
  ${A2UI_CORE_SOURCES}
  src/renderer/a11y-bridge.cpp
  src/renderer/a2ui-host.cpp
  src/renderer/a2ui-renderer.cpp
  src/renderer/render-context.cpp
  src/renderer/render-style.cpp
  src/renderer/a2ui-theme.cpp
  src/renderer/a2ui-metrics.cpp
  src/renderer/view-pool.cpp
  ${A2UI_COMPONENT_SOURCES}
)

# ============================================================
# Library: a2ui-dali (static)
# ============================================================
ADD_LIBRARY(a2ui-dali STATIC ${A2UI_LIB_SOURCES})

TARGET_INCLUDE_DIRECTORIES(a2ui-dali PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DALICORE_INCLUDE_DIRS}
  ${DALIADAPTOR_INCLUDE_DIRS}
  ${DALI_UI_FOUNDATION_INCLUDE_DIRS}
  ${DALI_UI_COMPONENTS_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
)

TARGET_COMPILE_OPTIONS(a2ui-dali PRIVATE
  ${DALICORE_CFLAGS_OTHER}
  ${DALIADAPTOR_CFLAGS_OTHER}
  ${DALI_UI_FOUNDATION_CFLAGS_OTHER}
  ${DALI_UI_COMPONENTS_CFLAGS_OTHER}
)

TARGET_LINK_LIBRARIES(a2ui-dali PUBLIC
  ${DALICORE_LIBRARIES}
  ${DALIADAPTOR_LIBRARIES}
  ${DALI_UI_FOUNDATION_LIBRARIES}
  ${DALI_UI_COMPONENTS_LIBRARIES}
  ${CURL_LIBRARIES}
  pthread
)

# Desktop (Ubuntu) build links against DALi installed under $DESKTOP_PREFIX.
# Under gbs/RPM the deps are resolved via pkg-config from the system prefix,
# and DESKTOP_PREFIX is unset, so only add this link path when it exists.
IF(DEFINED ENV{DESKTOP_PREFIX})
  TARGET_LINK_DIRECTORIES(a2ui-dali PUBLIC
    $ENV{DESKTOP_PREFIX}/lib
  )
ENDIF()

# ============================================================
# Install: library + public headers + pkg-config (distributable)
# ============================================================
INCLUDE(GNUInstallDirs)

INSTALL(TARGETS a2ui-dali ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Public headers keep their "core/..." / "renderer/..." include prefixes.
INSTALL(DIRECTORY src/core src/renderer
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/a2ui-dali
  FILES_MATCHING PATTERN "*.h")

CONFIGURE_FILE(a2ui-dali.pc.in ${CMAKE_CURRENT_BINARY_DIR}/a2ui-dali.pc @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/a2ui-dali.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# ============================================================
# Conformance test: core parsing only, no DALi UI deps
# ============================================================
SET(TEST_NAME a2ui-conformance-test)
ADD_EXECUTABLE(${TEST_NAME}
  test/conformance-test.cpp
  ${A2UI_CORE_SOURCES}
)

TARGET_INCLUDE_DIRECTORIES(${TEST_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DALICORE_INCLUDE_DIRS}
  ${DALIADAPTOR_INCLUDE_DIRS}
  ${DALI_UI_FOUNDATION_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
)

TARGET_LINK_LIBRARIES(${TEST_NAME}
  ${DALICORE_LIBRARIES}
  ${DALIADAPTOR_LIBRARIES}
  ${DALI_UI_FOUNDATION_LIBRARIES}
  ${CURL_LIBRARIES}
  pthread
)

IF(DEFINED ENV{DESKTOP_PREFIX})
  TARGET_LINK_DIRECTORIES(${TEST_NAME} PRIVATE
    $ENV{DESKTOP_PREFIX}/lib
  )
ENDIF()

SET_TARGET_PROPERTIES(${TEST_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
# Streaming render test: drives the real renderer, needs a display
# (run via tools/run-tests.sh, which wraps it in Xvfb)
# ============================================================
SET(RENDER_TEST_NAME a2ui-streaming-render-test)
ADD_EXECUTABLE(${RENDER_TEST_NAME} test/streaming-render-test.cpp)

TARGET_LINK_LIBRARIES(${RENDER_TEST_NAME} a2ui-dali)

SET_TARGET_PROPERTIES(${RENDER_TEST_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
# Example: examples/basic-renderer
# ============================================================
SET(EXAMPLE_NAME a2ui-basic-renderer)
ADD_EXECUTABLE(${EXAMPLE_NAME} examples/basic-renderer/main.cpp)

TARGET_LINK_LIBRARIES(${EXAMPLE_NAME} a2ui-dali)

SET_TARGET_PROPERTIES(${EXAMPLE_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
# Example: examples/gallery-demo — runnable catalog browser
# ============================================================
ADD_EXECUTABLE(a2ui-gallery-demo examples/gallery-demo/main.cpp)
TARGET_LINK_LIBRARIES(a2ui-gallery-demo a2ui-dali)
SET_TARGET_PROPERTIES(a2ui-gallery-demo PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
# Example: examples/custom-component — custom-catalog component via RegisterComponent
# ============================================================
ADD_EXECUTABLE(a2ui-custom-component examples/custom-component/main.cpp)
TARGET_LINK_LIBRARIES(a2ui-custom-component a2ui-dali)
SET_TARGET_PROPERTIES(a2ui-custom-component PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
# Install: example binaries + runtime data (Tizen/gbs packaging)
# ============================================================
# The renderer resolves images via a relative "res/" path
# (A2uiHost::GetRenderer().SetImageDir("res/")), so on target the examples must
# be launched with the installed data dir as the working directory, e.g.:
#   cd ${CMAKE_INSTALL_DATADIR}/a2ui-dali && a2ui-gallery-demo screens
OPTION(A2UI_INSTALL_EXAMPLES "Install example binaries and runtime resources" ON)
IF(A2UI_INSTALL_EXAMPLES)
  INSTALL(TARGETS a2ui-basic-renderer a2ui-gallery-demo a2ui-custom-component
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  INSTALL(DIRECTORY res/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/a2ui-dali/res)
  INSTALL(DIRECTORY examples/gallery-demo/screens/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/a2ui-dali/screens)
  INSTALL(DIRECTORY examples/samples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/a2ui-dali/samples)
  INSTALL(FILES examples/basic-renderer/sample.jsonl
    DESTINATION ${CMAKE_INSTALL_DATADIR}/a2ui-dali/samples)
ENDIF()
