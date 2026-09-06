# =============================================================================
# 图像查看器(ImageViewer) - CMake 配置
# 目标: Windows / Qt 5.14 / OpenCV 4.5.5 / C++17 / VS2019
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(ImageViewer LANGUAGES CXX)

# ---------- 全局编译选项 ----------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------- Windows 平台设置 ----------
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zi /FS
        /source-charset:utf-8 /execution-charset:utf-8)
    add_link_options(/DEBUG /ignore:4099)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

# ---------- 依赖查找 ----------
find_package(Qt5 5.14 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Concurrent
    Svg
    LinguistTools
)

# OpenCV 4.5.5：优先使用本工程的平铺 include/lib/bin 结构。
set(LOCAL_OPENCV_ROOT "${CMAKE_SOURCE_DIR}/3rdpart/opencv")
if(EXISTS "${LOCAL_OPENCV_ROOT}/lib/opencv_world455.lib")
    set(OpenCV_INCLUDE_DIRS "${LOCAL_OPENCV_ROOT}/include")
    add_library(opencv_world SHARED IMPORTED)
    set_target_properties(opencv_world PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${LOCAL_OPENCV_ROOT}/lib/opencv_world455d.lib"
        IMPORTED_IMPLIB_RELEASE "${LOCAL_OPENCV_ROOT}/lib/opencv_world455.lib"
        IMPORTED_IMPLIB_RELWITHDEBINFO "${LOCAL_OPENCV_ROOT}/lib/opencv_world455.lib"
        IMPORTED_IMPLIB_MINSIZEREL "${LOCAL_OPENCV_ROOT}/lib/opencv_world455.lib"
        IMPORTED_LOCATION_DEBUG "${LOCAL_OPENCV_ROOT}/bin/opencv_world455d.dll"
        IMPORTED_LOCATION_RELEASE "${LOCAL_OPENCV_ROOT}/bin/opencv_world455.dll"
        IMPORTED_LOCATION_RELWITHDEBINFO "${LOCAL_OPENCV_ROOT}/bin/opencv_world455.dll"
        IMPORTED_LOCATION_MINSIZEREL "${LOCAL_OPENCV_ROOT}/bin/opencv_world455.dll"
    )
else()
    find_package(OpenCV 4.5.5 REQUIRED)
endif()

# ---------- 可执行目标 ----------
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/MainWindow.cpp
    src/ui/ImageView.cpp
    src/ui/ThumbnailBar.cpp
    src/ui/TitleBar.cpp
    src/ui/PreprocessPanel.cpp
    src/ui/AnalysisPanel.cpp
    src/core/loader/ImageLoader.cpp
    src/core/navigation/DirectoryModel.cpp
    src/core/processing/ImageProcessor.cpp
    src/core/processing/ProcessingPreset.cpp
    src/core/analysis/ImageAnalysis.cpp
    src/core/cache/ImageCache.cpp
    src/core/cache/ThumbnailCache.cpp
    src/core/cache/ImagePyramid.cpp
)

set(APP_HEADERS
    src/app/Application.h
    src/app/MainWindow.h
    src/ui/ImageView.h
    src/ui/ThumbnailBar.h
    src/ui/TitleBar.h
    src/ui/PreprocessPanel.h
    src/ui/AnalysisPanel.h
    src/core/loader/ImageLoader.h
    src/core/navigation/DirectoryModel.h
    src/core/processing/ImageProcessor.h
    src/core/processing/ProcessingPreset.h
    src/core/analysis/ImageAnalysis.h
    src/core/cache/ImageCache.h
    src/core/cache/ThumbnailCache.h
    src/core/cache/ImagePyramid.h
    src/util/ElapsedLog.h
)

set(APP_UI
    src/app/MainWindow.ui
)

add_executable(ImageViewer WIN32
    ${APP_SOURCES}
    ${APP_HEADERS}
    ${APP_UI}
)

qt5_add_translation(APP_TRANSLATIONS
    translations/ImageViewer_en_US.ts
)
add_custom_target(ImageViewerTranslations ALL DEPENDS ${APP_TRANSLATIONS})
add_dependencies(ImageViewer ImageViewerTranslations)

# ---------- 链接 ----------
target_include_directories(ImageViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(ImageViewer PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Svg
    opencv_world
)

add_custom_command(TARGET ImageViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ImageViewer>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${APP_TRANSLATIONS} "$<TARGET_FILE_DIR:ImageViewer>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt5::Core> $<TARGET_FILE:Qt5::Gui>
        $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE:Qt5::Concurrent>
        $<TARGET_FILE:Qt5::Svg> $<TARGET_FILE:opencv_world>
        "$<TARGET_FILE_DIR:ImageViewer>"
)

# ---------- 部署 ----------
if(WIN32)
    # windeployqt 在 install 时执行
    install(TARGETS ImageViewer RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ImageViewerCoreTests
        tests/ImageViewerCoreTests.cpp
        src/core/navigation/DirectoryModel.cpp
        src/core/processing/ImageProcessor.cpp
        src/core/processing/ProcessingPreset.cpp
        src/core/analysis/ImageAnalysis.cpp
        src/core/cache/ImageCache.cpp
        src/core/cache/ThumbnailCache.cpp
        src/core/cache/ImagePyramid.cpp
        src/core/navigation/DirectoryModel.h
        src/core/processing/ImageProcessor.h
        src/core/processing/ProcessingPreset.h
        src/core/analysis/ImageAnalysis.h
        src/core/cache/ImageCache.h
        src/core/cache/ThumbnailCache.h
        src/core/cache/ImagePyramid.h
    )
    target_include_directories(ImageViewerCoreTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(ImageViewerCoreTests PRIVATE Qt5::Core Qt5::Gui opencv_world)
    add_custom_command(TARGET ImageViewerCoreTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt5::Core> $<TARGET_FILE:Qt5::Gui>
            $<TARGET_FILE:opencv_world>
            "$<TARGET_FILE_DIR:ImageViewerCoreTests>"
    )
    add_test(NAME ImageViewerCoreTests COMMAND ImageViewerCoreTests)
endif()
