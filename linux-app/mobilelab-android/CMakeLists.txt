cmake_minimum_required(VERSION 3.20)
project(MobileLabAndroid VERSION 0.7.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Test)

# --- resources: Inter + JetBrains Mono from shared/fonts, SVG icon set -------------------------
set(MOBILELAB_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/fonts" CACHE PATH "Directory with Inter and JetBrains Mono TTF files")
get_filename_component(MOBILELAB_FONT_DIR "${MOBILELAB_FONT_DIR}" ABSOLUTE)
file(GLOB MOBILELAB_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
set(MOBILELAB_ICON_ENTRIES "")
foreach(f ${MOBILELAB_ICON_FILES})
  get_filename_component(n "${f}" NAME)
  string(APPEND MOBILELAB_ICON_ENTRIES "        <file alias=\"${n}\">${f}</file>\n")
endforeach()
configure_file(resources/resources.qrc.in "${CMAKE_CURRENT_BINARY_DIR}/mobilelab-resources.qrc" @ONLY)

# --- core (no widgets): Android SDK access, scheduler, matrix execution, REST, Waydroid, ARM64 ----
add_library(mobilelab_core STATIC
  src/core/AsyncProcess.cpp src/core/AndroidRuntime.cpp src/core/AndroidEmulator.cpp
  src/core/AndroidPackageCatalog.cpp src/core/ArtifactCollector.cpp src/core/MatrixExecutor.cpp
  src/core/MatrixRun.cpp src/core/AndroidDevice.cpp src/core/AndroidTestRunner.cpp
  src/core/ResourceScheduler.cpp src/core/ApiServer.cpp src/core/ArmVirtualMachine.cpp
  src/waydroid/WaydroidBackend.cpp src/waydroid/WaydroidImageCatalog.cpp
  src/arm64/Arm64CapabilityProbe.cpp src/runtime/AndroidContainerRuntime.cpp
)
target_link_libraries(mobilelab_core PUBLIC Qt6::Core Qt6::Network)
target_include_directories(mobilelab_core PUBLIC src src/core src/waydroid src/arm64 src/runtime)
target_compile_definitions(mobilelab_core PUBLIC MOBILELAB_ANDROID_UI=1 MOBILELAB_ARM_RUNTIME=1
  MOBILELAB_WAYDROID_BACKEND=1 MOBILELAB_GOOGLE_ANDROID_EMULATOR=1 MOBILELAB_MATRIX_EXECUTION=1
  MOBILELAB_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")

# --- UI library (theme, icons, liquid glass, panes, navigators, editors) -------------------------
file(GLOB_RECURSE MOBILELAB_UI_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h")
list(FILTER MOBILELAB_UI_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(mobilelab_ui STATIC ${MOBILELAB_UI_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/mobilelab-resources.qrc")
target_link_libraries(mobilelab_ui PUBLIC mobilelab_core Qt6::Widgets Qt6::Svg)
target_include_directories(mobilelab_ui PUBLIC src src/ui src/ui/glass src/ui/editors src/ui/dialogs src/app)
target_compile_definitions(mobilelab_ui PUBLIC MOBILELAB_VERSION="${PROJECT_VERSION}")
# The resource object must be linked into every consumer, or Q_INIT_RESOURCE-style registration is dropped.
set_target_properties(mobilelab_ui PROPERTIES AUTORCC ON)

qt_add_executable(mobilelab-android src/app/main.cpp)
target_link_libraries(mobilelab-android PRIVATE mobilelab_ui)
target_compile_definitions(mobilelab-android PRIVATE MOBILELAB_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

# --- tests ---------------------------------------------------------------------------------------
enable_testing()
add_test(NAME mobilelab-arm64-smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/arm64-smoke.sh)

function(mobilelab_unit_test name)
  add_executable(${name} tests/unit/${name}.cpp)
  target_link_libraries(${name} PRIVATE mobilelab_ui Qt6::Test)
  target_compile_definitions(${name} PRIVATE MOBILELAB_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/tst_glassmath.cpp)
  mobilelab_unit_test(tst_glassmath)
  mobilelab_unit_test(tst_logclassifier)
  mobilelab_unit_test(tst_theme)
  mobilelab_unit_test(tst_core)
  mobilelab_unit_test(tst_window)
  mobilelab_unit_test(tst_run_fixture)
endif()

add_executable(mobilelab-glass-proof tests/tools/glass_proof.cpp)
target_link_libraries(mobilelab-glass-proof PRIVATE mobilelab_ui)
add_executable(mobilelab-imagecheck tests/tools/imagecheck.cpp)
target_link_libraries(mobilelab-imagecheck PRIVATE Qt6::Gui)

# Screenshot run against the fake Android SDK fixture (needs no real SDK, KVM or display).
add_test(NAME mobilelab-screenshots
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/screenshot-fixture.sh $<TARGET_FILE:mobilelab-android> $<TARGET_FILE:mobilelab-imagecheck> $<TARGET_FILE:mobilelab-glass-proof>)
set_tests_properties(mobilelab-screenshots PROPERTIES TIMEOUT 900)

# --- install -------------------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS mobilelab-android RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY config/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mobilelab-android/config)
install(DIRECTORY tools/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mobilelab-android/tools USE_SOURCE_PERMISSIONS)
install(FILES ${MOBILELAB_FONT_DIR}/LICENSE-Inter.txt ${MOBILELAB_FONT_DIR}/OFL-JetBrainsMono.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES packaging/mobilelab-android.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/mobilelab.mark.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME mobilelab-android.svg)

# --- .deb package (cpack -G DEB) -----------------------------------------------------------------
# Dependencies are read from the binary (dpkg-shlibdeps), so the package stays correct across Qt versions.
set(MOBILELAB_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version written into the .deb")
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME mobilelab-android)
set(CPACK_PACKAGE_VERSION "${MOBILELAB_PACKAGE_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_PACKAGE_CONTACT "MobileLab <noreply@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Android device lab: AVDs, Waydroid and hybrid test matrices")
set(CPACK_DEBIAN_PACKAGE_SECTION devel)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "adb, qt6-qpa-plugins")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
