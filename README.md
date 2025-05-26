# MeetAnt 会议助手（施工中）

MeetAnt 是一款基于 wxWidgets 构建的桌面会议助手应用，旨在提供录音、实时转录、AI 交互等功能。

## 功能特性 (施工中)

- **核心功能**:
    - （已完成）录音控制 (系统音频捕获)
    - （施工中）实时语音转录 (本地 FunASR / 云端 API)
    - （施工中）AI 对话交互
- **新增功能模块**:
    - 详细的配置系统 (音频, FunASR, 大模型)
    - 辅助功能 (批注, 搜索)
    - 维护功能 (自动更新, 诊断工具)

## 技术栈

- **UI 框架**: wxWidgets 3.2+ (确保体积足够轻量、跨平台)
- **构建系统**: CMake 3.15+
- **音频处理**: portaudio
- **语音识别**: FunASR (本地/云端)

## 构建说明 (占位)

详细构建步骤将在后续补充。
需要确保已安装 CMake, C++ 编译器, 以及 wxWidgets 开发库。

## Screenshots
<div style="display: flex; justify-content: space-between; align-items: center;">
  <img src="https://github.com/user-attachments/assets/e53b719c-42a6-4d5e-b655-490723307b17" width="19%" />
  <img src="https://github.com/user-attachments/assets/d374bc5c-6acc-4f2a-b4b6-ba29fce85327" width="19%" />
  <img src="https://github.com/user-attachments/assets/7675fe17-5455-496a-b183-3379114217cc" width="19%" />
  <img src="https://github.com/user-attachments/assets/7596f7fe-31c6-4f92-8929-ca345058b3e9" width="19%" />
  <img src="https://github.com/user-attachments/assets/0b833e45-571f-4cab-b5ae-8ed0e12567c2" width="19%" />
</div>






## 快速开始

本项目现在使用 vcpkg 进行依赖管理。

### Windows

1. 运行设置脚本：
   ```bash
   setup-vcpkg.bat
   ```

2. 构建项目：
   ```bash
   mkdir build
   cd build
   cmake .. --preset windows-x64-release
   cmake --build . --config Release
   ```

### Linux/macOS

1. 运行设置脚本：
   ```bash
   chmod +x setup-vcpkg.sh
   ./setup-vcpkg.sh
   ```

2. 构建项目：
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   cmake --build . --config Release
   ```

## 依赖项

- wxWidgets：跨平台 GUI 库
- PortAudio：音频处理库
- CURL：网络通信库

所有依赖项都通过 vcpkg 自动管理。

## 详细说明

更多详细的构建说明，请参阅 [README_vcpkg.md](README_vcpkg.md)。

## 旧版构建方式

如果您仍需要使用 3rdparty 目录中的预编译依赖，请使用旧版的 CMakeLists.txt。 
