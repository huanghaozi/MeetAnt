cmake_minimum_required(VERSION 3.15)
project(MeetAnt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加选项：是否使用最新的PortAudio（从GitHub获取）
option(USE_LATEST_PORTAUDIO "Use latest PortAudio from GitHub master branch" ON)

if(USE_LATEST_PORTAUDIO)
    # 使用FetchContent获取最新的PortAudio
    include(FetchContent)
    FetchContent_Declare(
        portaudio
        GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
        GIT_TAG        master
    )

    # 配置PortAudio选项
    set(PA_BUILD_SHARED OFF CACHE BOOL "Build PortAudio shared library" FORCE)
    set(PA_BUILD_STATIC ON CACHE BOOL "Build PortAudio static library" FORCE)
    set(PA_ENABLE_DEBUG_OUTPUT OFF CACHE BOOL "Enable debug output" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(PA_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
    
    # Windows特定选项
    if(WIN32)
        set(PA_USE_WASAPI ON CACHE BOOL "Enable WASAPI" FORCE)
        set(PA_USE_WDMKS ON CACHE BOOL "Enable WDM/KS" FORCE)
        set(PA_USE_WMME ON CACHE BOOL "Enable WMME" FORCE)
        set(PA_USE_DS ON CACHE BOOL "Enable DirectSound" FORCE)
    endif()

    FetchContent_MakeAvailable(portaudio)
    
    message(STATUS "Using latest PortAudio from GitHub master branch")
    message(STATUS "PortAudio build type: Static")
else()
    # 使用vcpkg提供的PortAudio
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
    find_library(PORTAUDIO_LIBRARY portaudio)
    
    if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
        message(FATAL_ERROR "PortAudio not found! Please install portaudio via vcpkg.")
    else()
        message(STATUS "Found PortAudio from vcpkg!")
        message(STATUS "  PORTAUDIO_INCLUDE_DIR: ${PORTAUDIO_INCLUDE_DIR}")
        message(STATUS "  PORTAUDIO_LIBRARY: ${PORTAUDIO_LIBRARY}")
    endif()
endif()

# 找到 wxWidgets
find_package(wxWidgets COMPONENTS core base richtext adv net REQUIRED)

if(NOT wxWidgets_FOUND)
    message(FATAL_ERROR "wxWidgets not found! Please install wxWidgets via vcpkg.")
else()
    message(STATUS "Found wxWidgets!")
    message(STATUS "  wxWidgets_LIBRARIES: ${wxWidgets_LIBRARIES}")
    message(STATUS "  wxWidgets_INCLUDE_DIRS: ${wxWidgets_INCLUDE_DIRS}")
    message(STATUS "  wxWidgets_DEFINITIONS: ${wxWidgets_DEFINITIONS}")
    message(STATUS "  wxWidgets_CXX_FLAGS: ${wxWidgets_CXX_FLAGS}")
    include(${wxWidgets_USE_FILE})
endif()

# 找到 CURL
find_package(CURL REQUIRED)
if(NOT CURL_FOUND)
    message(FATAL_ERROR "CURL not found! Please install curl via vcpkg.")
else()
    message(STATUS "Found CURL!")
    message(STATUS "  CURL_LIBRARIES: ${CURL_LIBRARIES}")
    message(STATUS "  CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
endif()

# 找到 nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR "nlohmann_json not found! Please install nlohmann-json via vcpkg.")
else()
    message(STATUS "Found nlohmann_json!")
endif()

# 添加可执行文件
if(WIN32)
    # 如果是 Windows 平台，添加资源文件
    add_executable(MeetAnt WIN32
        src/main.cpp
        src/App.cpp
        src/App.h
        src/MainFrame.cpp
        src/MainFrame.h
        src/ConfigDialog.cpp
        src/ConfigDialog.h
        src/Annotation.cpp
        src/Annotation.h
        src/NoteDialog.cpp
        src/NoteDialog.h
        src/BookmarkDialog.cpp
        src/BookmarkDialog.h
        src/NotePopup.cpp
        src/SSEClient.cpp
        src/meetant.rc
        src/TranscriptionBubbleCtrl.cpp
        src/TranscriptionBubbleCtrl.h
        src/PlaybackControlBar.cpp
        src/PlaybackControlBar.h
    )
else()
    # 非 Windows 平台，不使用 WIN32 属性，也不编译 .rc
    add_executable(MeetAnt
        src/main.cpp
        src/App.cpp
        src/App.h
        src/MainFrame.cpp
        src/MainFrame.h
        src/ConfigDialog.cpp
        src/ConfigDialog.h
        src/Annotation.cpp
        src/Annotation.h
        src/NoteDialog.cpp
        src/NoteDialog.h
        src/BookmarkDialog.cpp
        src/BookmarkDialog.h
        src/NotePopup.cpp
        src/SSEClient.cpp
        src/TranscriptionBubbleCtrl.cpp
        src/TranscriptionBubbleCtrl.h
        src/PlaybackControlBar.cpp
        src/PlaybackControlBar.h
    )
endif()

# 为 MeetAnt 目标添加 /utf-8 编译选项 (MSVC特定)
if(MSVC)
    target_compile_options(MeetAnt PRIVATE "/utf-8")
endif()

# 包含项目本身的头文件目录
target_include_directories(MeetAnt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(MeetAnt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 包含 wxWidgets 的头文件目录
target_include_directories(MeetAnt PRIVATE ${wxWidgets_INCLUDE_DIRS})

# 包含 CURL 的头文件目录
target_include_directories(MeetAnt PRIVATE ${CURL_INCLUDE_DIRS})

# 如果使用vcpkg的PortAudio，需要包含其头文件目录
if(NOT USE_LATEST_PORTAUDIO)
    target_include_directories(MeetAnt PRIVATE ${PORTAUDIO_INCLUDE_DIR})
endif()

# 链接库
if(USE_LATEST_PORTAUDIO)
    target_link_libraries(MeetAnt PRIVATE 
        ${wxWidgets_LIBRARIES}
        portaudio  # 使用FetchContent提供的portaudio目标（不是portaudio_static）
        ${CURL_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
else()
    target_link_libraries(MeetAnt PRIVATE 
        ${wxWidgets_LIBRARIES}
        ${PORTAUDIO_LIBRARY}  # 使用vcpkg提供的portaudio库
        ${CURL_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
endif()

# 为 Windows 应用程序设置入口点
if(WIN32)
    set_target_properties(MeetAnt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# 创建文件夹，方便在 IDE 中查看
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${SOURCES})

# 复制必要的 DLL 到输出目录（如果使用动态库）
if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET MeetAnt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:MeetAnt>
        $<TARGET_FILE_DIR:MeetAnt>
        COMMAND_EXPAND_LISTS
    )
endif() 