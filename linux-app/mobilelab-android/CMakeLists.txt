cmake_minimum_required(VERSION 3.20)
project(MobileLabAndroid VERSION 0.6.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)

set(MOBILELAB_ANDROID_SOURCES
 src/main.cpp src/MainWindow.cpp src/MainWindow.h
 src/core/AndroidRuntime.cpp src/core/AndroidRuntime.h
 src/core/AndroidEmulator.cpp src/core/AndroidEmulator.h
 src/core/AndroidPackageCatalog.cpp src/core/AndroidPackageCatalog.h
 src/core/ArtifactCollector.cpp src/core/ArtifactCollector.h
 src/core/MatrixExecutor.cpp src/core/MatrixExecutor.h
 src/core/AndroidDevice.cpp src/core/AndroidDevice.h
 src/core/AndroidTestRunner.cpp src/core/AndroidTestRunner.h
 src/core/ResourceScheduler.cpp src/core/ResourceScheduler.h
 src/core/ApiServer.cpp src/core/ApiServer.h
 src/core/ArmVirtualMachine.cpp src/core/ArmVirtualMachine.h
 src/waydroid/WaydroidBackend.cpp src/waydroid/WaydroidBackend.h
 src/waydroid/WaydroidImageCatalog.cpp src/waydroid/WaydroidImageCatalog.h
 src/arm64/Arm64CapabilityProbe.cpp src/arm64/Arm64CapabilityProbe.h
 src/arm64/Arm64ContainerProfile.h
 src/runtime/AndroidContainerRuntime.cpp src/runtime/AndroidContainerRuntime.h
)

qt_add_executable(mobilelab-android ${MOBILELAB_ANDROID_SOURCES})
target_link_libraries(mobilelab-android PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(mobilelab-android PRIVATE MOBILELAB_ANDROID_UI=1 MOBILELAB_ARM_RUNTIME=1 MOBILELAB_WAYDROID_BACKEND=1 MOBILELAB_GOOGLE_ANDROID_EMULATOR=1 MOBILELAB_MATRIX_EXECUTION=1)
target_include_directories(mobilelab-android PRIVATE src src/core src/waydroid src/arm64 src/runtime)
install(TARGETS mobilelab-android RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME mobilelab-arm64-smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/arm64-smoke.sh)

qt_add_executable(resource-scheduler-test
 tests/unit/ResourceSchedulerTest.cpp
 src/core/ResourceScheduler.cpp src/core/ResourceScheduler.h
)
target_link_libraries(resource-scheduler-test PRIVATE Qt6::Core Qt6::Test)
target_include_directories(resource-scheduler-test PRIVATE src/core)
add_test(NAME resource-scheduler-test COMMAND resource-scheduler-test)
