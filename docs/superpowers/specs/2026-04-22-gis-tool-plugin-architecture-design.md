# GIS Tool — 插件式架构设计文档

> 日期: 2026-04-22
> 技术栈: C++17 / GDAL 3.x / OpenCV 4.x / Qt 6 / CMake / vcpkg
> 目标用户: 通用（专业GIS从业者 + 入门用户 + 开发者）
> 数据规模: 中等（500MB - 5GB）

---

## 1. 架构选型：插件式架构（方案B）

### 核心思路

统一入口 + 算法插件DLL动态加载。CLI和GUI共享同一个插件框架，通过插件声明的 `ParamSpec` 元数据自动生成CLI帮助和GUI参数面板。

### 为什么不选三层独立架构（方案A）

- GIS影像数据量大（500MB-5GB），进程间传递需要序列化，IO开销大
- 进程间通信只能通过文本，进度回报和取消机制受限
- 一个算法一个EXE，数量膨胀维护困难

### 为什么不选混合架构（方案C）

- 两种GUI调用模式（进程内/进程外）增加维护复杂度
- 缺少插件机制，扩展性差

---

## 2. 项目目录结构

```
gis-tool/
├── CMakeLists.txt
├── cmake/
│   └── FindGDAL.cmake
├── src/
│   ├── core/                       # gis_core 共享基础库
│   │   ├── CMakeLists.txt
│   │   ├── include/gis/core/
│   │   │   ├── gdal_wrapper.h      # GDAL C++封装
│   │   │   ├── opencv_wrapper.h    # OpenCV C++封装（Mat与GDAL互转）
│   │   │   ├── progress.h          # ProgressReporter
│   │   │   ├── error.h             # 统一错误码和异常
│   │   │   └── types.h             # 通用类型（Extent, CRS等）
│   │   └── src/
│   │       ├── gdal_wrapper.cpp
│   │       ├── opencv_wrapper.cpp
│   │       ├── progress.cpp
│   │       └── error.cpp
│   │
│   ├── framework/                  # gis_framework 插件框架库
│   │   ├── CMakeLists.txt
│   │   ├── include/gis/framework/
│   │   │   ├── plugin.h            # IGisPlugin 接口
│   │   │   ├── param_spec.h        # ParamSpec 参数元数据
│   │   │   ├── plugin_manager.h    # PluginManager
│   │   │   └── result.h            # Result
│   │   └── src/
│   │       ├── plugin_manager.cpp
│   │       └── param_spec.cpp
│   │
│   ├── plugins/                    # 算法插件
│   │   ├── projection/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── projection_plugin.h
│   │   │   └── projection_plugin.cpp
│   │   ├── cutting/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── cutting_plugin.h
│   │   │   └── cutting_plugin.cpp
│   │   ├── matching/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── matching_plugin.h
│   │   │   └── matching_plugin.cpp
│   │   └── processing/
│   │       ├── CMakeLists.txt
│   │       ├── processing_plugin.h
│   │       └── processing_plugin.cpp
│   │
│   ├── cli/                        # gis-cli.exe
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   └── cli_parser.h
│   │
│   └── gui/                        # gis-gui.exe
│       ├── CMakeLists.txt
│       ├── main.cpp
│       ├── mainwindow.h/cpp
│       ├── param_widget.h/cpp
│       └── progress_dialog.h/cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_core/
│   ├── test_framework/
│   └── test_plugins/
│
├── docs/
└── third_party/
```

---

## 3. 核心接口设计

### 3.1 IGisPlugin

```cpp
class IGisPlugin {
public:
    virtual ~IGisPlugin() = default;

    virtual std::string name() const = 0;          // "projection"
    virtual std::string displayName() const = 0;    // "投影转换"
    virtual std::string version() const = 0;        // "1.0.0"
    virtual std::string description() const = 0;    // "坐标系投影转换与重投影"

    virtual std::vector<ParamSpec> paramSpecs() const = 0;

    virtual Result execute(
        const std::map<std::string, ParamValue>& params,
        ProgressReporter& progress) = 0;
};

// 跨平台导出宏
#ifdef _WIN32
    #define GIS_EXPORT __declspec(dllexport)
#else
    #define GIS_EXPORT __attribute__((visibility("default")))
#endif

#define GIS_PLUGIN_EXPORT(PluginClass) \
    extern "C" GIS_EXPORT IGisPlugin* createPlugin() { \
        return new PluginClass(); \
    } \
    extern "C" GIS_EXPORT void destroyPlugin(IGisPlugin* p) { \
        delete p; \
    }
```

### 3.2 ParamSpec

```cpp
enum class ParamType {
    String,        // 通用文本
    Int,           // 整数
    Double,        // 浮点数
    Bool,          // 开关
    FilePath,      // 文件路径（GUI: 文件选择器）
    DirPath,       // 目录路径
    Enum,          // 枚举选择（GUI: 下拉框）
    Extent,        // 矩形范围 (xmin,ymin,xmax,ymax)
    CRS,           // 坐标参考系（支持WKT字符串或EPSG代号，GUI: EPSG搜索+手动输入）
};

struct ParamSpec {
    std::string key;           // "input_file" / "src_srs" / "resample"
    std::string displayName;   // "输入文件" / "源坐标系" / "重采样方法"
    std::string description;   // 详细说明
    ParamType   type;
    bool        required;
    ParamValue  defaultValue;
    ParamValue  minValue;
    ParamValue  maxValue;
    std::vector<std::string> enumValues; // 枚举选项
};

using ParamValue = std::variant<
    std::string, int, double, bool,
    std::vector<std::string>,  // 多文件
    std::array<double,4>       // Extent
>;
```

**CRS参数说明**: ParamType::CRS 对应的值类型为 `std::string`，可接受：
- EPSG代号: `"EPSG:4326"`, `"EPSG:32650"`
- WKT字符串: `"GEOGCS[\"WGS 84\", ...]"`

在core库中提供CRS解析工具函数，统一将两种格式转换为 `OGRSpatialReference`。

### 3.3 ProgressReporter

```cpp
class ProgressReporter {
public:
    virtual ~ProgressReporter() = default;
    virtual void onProgress(double percent) = 0;       // 0.0 ~ 1.0
    virtual void onMessage(const std::string& msg) = 0;
    virtual bool isCancelled() const = 0;
};

class CliProgress : public ProgressReporter { /* 输出到stderr */ };
class GuiProgress : public ProgressReporter { /* emit Qt信号 */ };
```

### 3.4 Result

```cpp
struct Result {
    bool        success;
    std::string message;
    std::string outputPath;
    std::map<std::string, std::string> metadata;
};
```

---

## 4. 四个算法插件功能

### 4.1 plugin_projection — 投影转换

| 子功能 | 说明 | 核心API |
|--------|------|---------|
| reproject | 影像重投影 | GDALWarp / GDALReprojectImage |
| info | 查询影像坐标系信息 | GetProjectionRef |
| transform | 单点/批量坐标转换 | OGRCoordinateTransformation |
| assign_srs | 为影像指定SRS | SetProjection |

CRS输入统一支持EPSG代号和WKT字符串。

**子功能（action）约定**: 每个插件可包含多个子功能（如projection的reproject/info/transform/assign_srs）。约定使用保留key `"action"`（ParamType::Enum）作为第一个ParamSpec，CLI中表现为子命令（`gis-tool.exe projection reproject ...`），GUI中表现为下拉框选择。

### 4.2 plugin_cutting — 影像裁切与镶嵌

| 子功能 | 说明 | 核心API |
|--------|------|---------|
| clip | 按矢量/矩形裁切 | GDALTranslate / GDALWarp(-cutline) |
| mosaic | 多影像镶嵌 | GDALWarp(多输入) |
| split | 大影像分块 | GDALTranslate(-srcwin) |
| merge_bands | 波段合并 | GDALBuildVRT + GDALTranslate |

### 4.3 plugin_matching — 特征匹配与配准

| 子功能 | 说明 | 核心API |
|--------|------|---------|
| detect | 特征点检测 | SIFT/ORB/AKAZE::detectAndCompute |
| match | 特征匹配 | BFMatcher / FlannBasedMatcher |
| register | 影像配准 | findHomography + warpPerspective/affine |
| change | 变化检测 | absdiff + threshold + morphologyEx |

### 4.4 plugin_processing — 影像处理与分析

| 子功能 | 说明 | 核心API |
|--------|------|---------|
| threshold | 阈值分割 | threshold / adaptiveThreshold |
| filter | 空间滤波 | GaussianBlur / medianBlur / morphologyEx |
| enhance | 影像增强 | equalizeHist / CLAHE / normalize |
| band_math | 波段运算 | 表达式解析 + Mat运算 |
| pansharpen | 全色锐化 | OpenCV融合算法 |
| stats | 统计信息 | ComputeStatistics + GetHistogram |

---

## 5. 插件发现机制

PluginManager 在运行时扫描 `plugins/` 子目录：
1. 查找匹配 `plugin_*.dll`（Windows）或 `libplugin_*.so`（Linux）的文件
2. `LoadLibrary` / `dlopen` 加载
3. 通过 `createPlugin` 导出符号实例化 `IGisPlugin`
4. 读取 `paramSpecs()` 获取参数元数据
5. 调用方通过 `execute()` 执行算法

---

## 6. CLI 使用方式

```
# 列出所有可用插件
gis-tool.exe --list

# 查看插件帮助（自动从ParamSpec生成）
gis-tool.exe projection --help

# 执行投影转换
gis-tool.exe projection reproject \
    --input dem.tif --output dem_4326.tif \
    --dst_srs EPSG:4326 --resample bilinear

# 使用WKT指定坐标系
gis-tool.exe projection reproject \
    --input dem.tif --output dem_custom.tif \
    --dst_srs "GEOGCS[\"WGS 84\", ...]"

# 影像裁切
gis-tool.exe cutting clip \
    --input image.tif --output clip.tif \
    --extent 116.3,39.8,116.5,40.0

# 特征匹配
gis-tool.exe matching register \
    --reference base.tif --input unreg.tif --output reg.tif \
    --transform affine

# 阈值分割
gis-tool.exe processing threshold \
    --input gray.tif --output binary.tif \
    --method otsu
```

---

## 7. 构建与依赖

- **CMake**: 统一构建，`GIS_BUILD_GUI=ON/OFF` 控制Qt编译
- **vcpkg**: 统一管理 GDAL、OpenCV、Qt、PROJ 等所有依赖
- **C++17**: required
- **Qt 6**: 仅GUI模块依赖，通过vcpkg安装

---

## 8. 开发优先级

1. **Phase 1**: core库 + framework库 + CLI入口 + projection插件
2. **Phase 2**: cutting插件 + processing插件
3. **Phase 3**: matching插件
4. **Phase 4**: GUI（基于插件ParamSpec自动生成参数面板）

---

## 9. 部署

GitHub Release 发布便携式压缩包，包含：
- gis-cli.exe / gis-gui.exe
- gis_core.dll / gis_framework.dll
- plugin_*.dll
- 第三方DLL（GDAL, OpenCV, Qt等）
