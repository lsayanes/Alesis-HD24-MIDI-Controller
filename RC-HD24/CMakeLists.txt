cmake_minimum_required(VERSION 3.16)
project(RC-HD24)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure Qt automoc / autorcc / autouic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Selección de versión Qt y find_package
#   - Android: forzamos Qt6 (Qt5 no tiene soporte moderno de cross-build, y Qt6
#              trae qt_add_executable + qt_finalize_target con androiddeployqt).
#   - Desktop: respeta QT_MAJOR=6|5, hints Qt6_DIR/Qt5_DIR, o auto-detecta.
# ============================================================================
if(ANDROID)
  set(QT_VERSION_MAJOR 6)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
else()
  set(QT_REQUESTED_MAJOR "" CACHE STRING "Force Qt major version (5 or 6). Empty = auto")
  if(DEFINED QT_MAJOR AND NOT "${QT_MAJOR}" STREQUAL "")
    set(QT_REQUESTED_MAJOR "${QT_MAJOR}")
  elseif(DEFINED ENV{QT_MAJOR} AND NOT "$ENV{QT_MAJOR}" STREQUAL "")
    set(QT_REQUESTED_MAJOR "$ENV{QT_MAJOR}")
  endif()

  if(QT_REQUESTED_MAJOR STREQUAL "6")
    set(QT_VERSION_MAJOR 6)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
  elseif(QT_REQUESTED_MAJOR STREQUAL "5")
    set(QT_VERSION_MAJOR 5)
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
  elseif(DEFINED Qt6_DIR)
    set(QT_VERSION_MAJOR 6)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
  elseif(DEFINED Qt5_DIR)
    set(QT_VERSION_MAJOR 5)
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
  else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network Bluetooth)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Bluetooth)
  endif()
endif()

set(SOURCES
    main.cpp
    RCHD24.cpp
    Tcp.cpp
    Bluetooth.cpp
)

set(HEADERS
  RCHD24.h
  Tcp.h
  Bluetooth.h
)

set(RESOURCES
)

# ============================================================================
# Target ejecutable
#   - En Android usamos qt_add_executable (Qt6) para que la build genere una
#     librería compartida y todo el bundle Android (manifest + APK + JNI).
#   - En desktop seguimos con add_executable.
# ============================================================================
if(ANDROID)
  qt_add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})
elseif(APPLE)
  # En macOS DEBE ser un bundle .app con Info.plist: sin el bundle y sin
  # NSBluetoothAlwaysUsageDescription, macOS (TCC) deniega el Bluetooth en
  # silencio y las conexiones SPP expiran por timeout.
  add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${RESOURCES})
else()
  add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})
endif()

# ============================================================================
# Empaquetado macOS: bundle .app + Info.plist con permiso de Bluetooth
# ============================================================================
if(APPLE AND NOT ANDROID)
  set(MACOSX_BUNDLE_BUNDLE_NAME           "${PROJECT_NAME}")
  set(MACOSX_BUNDLE_GUI_IDENTIFIER        "com.lsayanes.rchd24")
  set(MACOSX_BUNDLE_BUNDLE_VERSION        "1.2")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING  "1.2")
  set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
  )
endif()

# Windows: build como GUI app (sin consola al lado).
if(WIN32)
  set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ============================================================================
# Empaquetado Android
# ============================================================================
if(ANDROID)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
    QT_ANDROID_TARGET_SDK_VERSION  35
    QT_ANDROID_MIN_SDK_VERSION     28
    QT_ANDROID_VERSION_NAME        "1.2"
    QT_ANDROID_VERSION_CODE        12000
  )
endif()

# Keyword signature (PRIVATE) obligatorio cuando se usa qt_add_executable Android:
# qt_finalize_target enlaza plugins con sintaxis keywords; CMake exige coherentes.
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Bluetooth
)

# ============================================================================
# Recursos en disco (solo desktop: en Android todo va por el qrc).
# ============================================================================
if(NOT ANDROID)
  if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
      add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  ${CMAKE_SOURCE_DIR}/resources
                  $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
      )
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/img")
      add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  ${CMAKE_SOURCE_DIR}/img
                  $<TARGET_FILE_DIR:${PROJECT_NAME}>/img
      )
  endif()

  install(TARGETS ${PROJECT_NAME}
      RUNTIME DESTINATION bin
      BUNDLE  DESTINATION .
  )

  if(EXISTS "${CMAKE_SOURCE_DIR}/img")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/img
            DESTINATION share/${PROJECT_NAME})
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources
            DESTINATION share/${PROJECT_NAME})
  endif()
endif()




find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(GIT_BRANCH "unknown")
  set(GIT_DESCRIBE "unknown")
  set(GIT_SHA "unknown")
endif()
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S")

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
# En lugar de generar header, definir macros de compilación (el header git_info.h provee defaults)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  RCHD24_GIT_BRANCH="${GIT_BRANCH}"
  RCHD24_GIT_DESC="${GIT_DESCRIBE}"
  RCHD24_GIT_SHA="${GIT_SHA}"
  RCHD24_GIT_BUILD_TIME="${BUILD_TIME}"
)