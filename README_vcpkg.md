# MeetAnt - 使用 vcpkg 构建指南

## 前提条件

1. 安装 CMake (版本 >= 3.15)
2. 安装 Git
3. 安装适合您平台的编译器（Windows上使用MSVC，Linux上使用GCC/Clang）

## 安装 vcpkg

### Windows

```bash
# 克隆 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# 运行引导脚本
.\bootstrap-vcpkg.bat

# （可选）集成到系统
.\vcpkg integrate install
```

### Linux/macOS

```bash
# 克隆 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# 运行引导脚本
./bootstrap-vcpkg.sh

# （可选）集成到系统
./vcpkg integrate install
```

## 安装依赖

在 vcpkg 目录中运行以下命令安装所需的依赖：

### Windows (x64)

```bash
.\vcpkg install wxwidgets:x64-windows
.\vcpkg install portaudio:x64-windows
.\vcpkg install curl:x64-windows
```

### Linux

```bash
./vcpkg install wxwidgets:x64-linux
./vcpkg install portaudio:x64-linux
./vcpkg install curl:x64-linux
```

### macOS

```bash
./vcpkg install wxwidgets:x64-osx
./vcpkg install portaudio:x64-osx
./vcpkg install curl:x64-osx
```

## 构建项目

### 方法一：使用 vcpkg 工具链文件

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目（将 [vcpkg root] 替换为您的 vcpkg 安装路径）
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build . --config Release
```

### 方法二：使用 vcpkg manifest 模式（推荐）

项目根目录已包含 `vcpkg.json` 文件，定义了所需的依赖。

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目（vcpkg 将自动安装 vcpkg.json 中定义的依赖）
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build . --config Release
```

## 运行程序

构建完成后，可执行文件将位于：
- Windows: `build/Release/MeetAnt.exe`
- Linux/macOS: `build/MeetAnt`

## 故障排除

### Windows 上的 DLL 问题

如果遇到缺少 DLL 的错误，确保：
1. 使用相同的架构（x86 或 x64）
2. 将 vcpkg 的 bin 目录添加到 PATH，或者
3. 将所需的 DLL 复制到可执行文件所在目录

### Linux 上的依赖问题

如果 wxWidgets 编译失败，可能需要安装系统依赖：

```bash
# Ubuntu/Debian
sudo apt-get install libgtk-3-dev libwebkitgtk-4.0-dev

# Fedora
sudo dnf install gtk3-devel webkit2gtk3-devel
```

### 清理和重新构建

如果遇到问题，尝试清理并重新构建：

```bash
# 在项目根目录
rm -rf build
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
``` 