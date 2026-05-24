# v1.0.0 发布检查清单

## 发布前验证（本地）

```powershell
vcpkg install --triplet x64-windows --x-manifest-root=$PWD
cmake --preset release --fresh
cmake --build build/release --config Release
cmake --build build/debug --config Debug

ctest --test-dir build/release -C Release --output-on-failure
ctest --test-dir build/debug -C Debug --output-on-failure

cmake --build build/release --config Release --target real_raster_regression real_matching_regression real_vector_regression
cmake --install build/release --config Release --prefix install
install/bin/gis-cli.exe --list
install/bin/gis-gui.exe -platform offscreen --self-test
```

## NSIS 安装包

前置：已安装 [NSIS](https://nsis.sourceforge.io/)，且 `makensis` 在 PATH 中。

```powershell
cmake --install build/release --config Release --prefix install
makensis installer\installer.nsi
```

产物：`installer/gis-toolkit-1.0.0-win64-setup.exe`（约 64MB，zlib 压缩）。

CI 参考：[.github/workflows/windows-build.yml](../.github/workflows/windows-build.yml) 中 `Build installer` 步骤。

## GitHub Release

1. 推送 `master` 并打标签：`git tag v1.0.0`
2. 在 GitHub 创建 Release，关联标签 `v1.0.0`
3. 附资产：
   - `gis-toolkit-1.0.0-win64-setup.exe`（NSIS）
   - `gis-tool-win64.zip`（CI artifact，含 `install/` 树）
4. 发布说明正文可复用 [v1.0.0发布说明.md](v1.0.0发布说明.md)、[CHANGELOG.md](../CHANGELOG.md) 或 [v1.0.0_GITHUB_RELEASE.md](v1.0.0_GITHUB_RELEASE.md)

## 已知限制

- Windows 代码签名未配置（SmartScreen 可能提示）
- `classification` / `georef` 插件版本 0.1.0
- Logger / PerformanceMonitor 为基础设施，未接入主执行链
