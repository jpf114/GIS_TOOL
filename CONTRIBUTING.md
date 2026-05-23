# 贡献指南

感谢关注 GIS TOOL。本项目当前以 Windows 桌面算法工作台为主，欢迎提交缺陷修复、文档改进与测试补强。

## 开发环境

- Windows 10+ x64
- Visual Studio 2022
- CMake 3.21+
- 全局 [vcpkg](https://github.com/microsoft/vcpkg)，`VCPKG_ROOT` 已配置

```powershell
vcpkg install --triplet x64-windows --x-manifest-root=$PWD
cmake --preset debug --fresh
cmake --build build/debug --config Debug
```

依赖说明见 [README.md](README.md#构建)。

## 提交规范

提交信息使用中文 Conventional Commits：

```
<type>(<scope>): <中文简述>
```

常用 type：`feat`、`fix`、`docs`、`test`、`refactor`、`ci`、`chore`。

原则：

- 一次提交一个清晰主题
- 不混入无关格式化或无关文件
- 不提交 `build/`、`install/`、`*.db`、根目录 `/data/` 本地验证数据

## 测试

提交前至少执行与改动相关的测试。发布相关改动应跑通 README 中的发布前验收基线。

```powershell
ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest" --output-on-failure
```

## Pull Request

1. 从 `master` 拉取最新代码
2. 在独立分支上完成改动
3. 确保 CI（windows-build）可通过
4. PR 描述中说明动机、影响范围与验证命令

## 报告问题

安全问题请见 [SECURITY.md](SECURITY.md)。一般缺陷请在 Issue 中提供复现步骤、期望与实际结果、环境信息（Windows 版本、构建配置）。
