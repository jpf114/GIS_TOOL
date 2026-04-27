# 构建与验证基线

## 1. 这份文档的用途

这份文档只回答一个问题：

- 当前仓库在本机环境下，怎样算“已经验证通过”

它不负责描述所有功能细节，功能范围请看：

- [当前功能与真实数据验证指南](D:/Code/MyProject/GIS_TOOL/docs/当前功能与真实数据验证指南.md)

## 2. 基线环境

当前验证基线基于以下环境：

- Windows
- Visual Studio 2022
- C++17
- 全局 `vcpkg`
- `GDAL / OpenCV / PROJ / GTest / Qt6`

项目默认复用：

- `VCPKG_ROOT/installed/x64-windows`

## 3. 2026-04-27 状态快照

以下结果基于 `build-verify` 目录重新执行得到：

- `cmake configure`：通过
- `cmake --build --config Debug`：通过
- `ctest -C Debug -N`：通过，识别 `106` 个测试
- `ctest -C Debug --output-on-failure`：通过，`106/106`
- `gis-cli.exe --list`：通过，可正常列出 6 类插件
- `gui_smoke_startup`：通过

补充说明：

- 本轮实际阻塞点不是框架层或插件层，而是 `src/gui/preview_panel.*` 中有一组未完成改动，导致 `gis-gui` 链接失败
- 修复后，GUI、CLI、测试链路均恢复正常

## 4. 推荐验证命令

### 4.1 配置

```powershell
cmake -S . -B build-verify -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
```

### 4.2 构建

```powershell
cmake --build build-verify --config Debug
```

### 4.3 枚举测试

```powershell
ctest --test-dir build-verify -C Debug -N
```

### 4.4 执行测试

```powershell
ctest --test-dir build-verify -C Debug --output-on-failure
```

### 4.5 CLI 快速验证

```powershell
.\build-verify\src\cli\Debug\gis-cli.exe --list
```

## 5. 如何理解“当前状态”

建议把项目状态分成两层理解：

### 5.1 功能覆盖状态

它回答：

- 这个工具现在能做什么
- 哪些插件和子功能已经有实现

### 5.2 工程基线状态

它回答：

- 当前代码是否可重新配置
- 是否能重新构建
- 自动化测试是否稳定
- CLI / GUI 是否还能正常启动

不要用“功能很多”替代“工程稳定”，也不要用“测试能跑”替代“所有业务场景都已完成真实数据回归”。

## 6. 当前已确认结论

- `core / framework / plugins / cli / gui / tests` 主链路可用
- 当前自动化测试规模已经不是早期原型水平，而是有较完整覆盖
- 目前更需要持续维护的是“可复现构建 + 文档同步”，而不是盲目继续堆新功能
