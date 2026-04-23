# GDAL + OpenCV 功能详细清单（C++ 版）

> 适用版本：GDAL 3.x | OpenCV 4.x | C++17  
> 所有示例均为 C++ 语法，配合实际开发使用。

---

## 一、GDAL C++ 功能详解

### 1. 环境初始化（必须）

```cpp
#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "cpl_conv.h"

// 程序启动时必须调用，注册所有驱动
GDALAllRegister();
```

---

### 2. 栅格数据读写

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 打开栅格文件 | 只读方式打开 GeoTIFF 等栅格文件 | `GDALDataset *ds = (GDALDataset*)GDALOpen("file.tif", GA_ReadOnly);` |
| 创建栅格文件 | 通过驱动创建新栅格文件 | `GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");` |
| 读取波段数据 | 读取指定波段为数组 | `band->RasterIO(GF_Read, ...)` |
| 写入波段数据 | 将数组写入波段 | `band->RasterIO(GF_Write, ...)` |
| 获取波段对象 | 获取第 N 个波段 | `GDALRasterBand *band = ds->GetRasterBand(1);` |
| 关闭数据集 | 释放资源 | `GDALClose(ds);` |

**完整读取示例：**
```cpp
GDALDataset *ds = (GDALDataset*)GDALOpen("input.tif", GA_ReadOnly);
if (ds == nullptr) { /* 错误处理 */ }

int width  = ds->GetRasterXSize();
int height = ds->GetRasterYSize();
int bands  = ds->GetRasterCount();

GDALRasterBand *band = ds->GetRasterBand(1); // 第1波段
std::vector<float> buffer(width * height);
band->RasterIO(GF_Read, 0, 0, width, height,
               buffer.data(), width, height, GDT_Float32, 0, 0);

GDALClose(ds);
```

---

### 3. 地理参考与坐标系

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 获取仿射变换参数 | 获取像素坐标到地理坐标的映射 | `ds->GetGeoTransform(adfGT)` |
| 设置仿射变换参数 | 给影像赋予地理参考 | `ds->SetGeoTransform(adfGT)` |
| 获取投影信息 | 获取 WKT 格式坐标系字符串 | `ds->GetProjectionRef()` |
| 设置投影信息 | 设置坐标系 | `ds->SetProjection(pszWKT)` |
| 坐标系转换 | 两个坐标系之间转换坐标点 | `OGRCoordinateTransformation::CreateCoordinateTransformation()` |
| 像素坐标到地理坐标 | 根据仿射参数手动换算 | 见下方示例 |

**像素坐标转地理坐标示例：**
```cpp
double adfGT[6];
ds->GetGeoTransform(adfGT);

// 像素(col, row) -> 地理坐标(X, Y)
double geoX = adfGT[0] + col * adfGT[1] + row * adfGT[2];
double geoY = adfGT[3] + col * adfGT[4] + row * adfGT[5];
```

**坐标系转换示例：**
```cpp
OGRSpatialReference srcSRS, dstSRS;
srcSRS.ImportFromEPSG(4326);  // WGS84
dstSRS.ImportFromEPSG(32650); // UTM Zone 50N

OGRCoordinateTransformation *ct =
    OGRCoordinateTransformation::CreateCoordinateTransformation(&srcSRS, &dstSRS);

double x = 120.0, y = 30.0;
ct->Transform(1, &x, &y);
OGRCoordinateTransformation::DestroyCT(ct);
```

---

### 4. 栅格处理

| 功能 | 说明 | 关键 API |
|------|------|----------|
| 重投影/重采样 | 将栅格变换到新坐标系或分辨率 | `GDALReprojectImage()` / `GDALWarp()` |
| 按范围裁剪 | 通过 GDALTranslate 按坐标裁剪 | `GDALTranslate()` + `GDALTranslateOptions` |
| 按矢量掩膜裁剪 | 使用矢量面文件裁剪栅格 | `GDALWarp()` + `-cutline` 选项 |
| 波段合并 | 多个单波段文件合并为多波段 | `GDALBuildVRT()` |
| 统计信息 | 计算波段最小值、最大值、均值、标准差 | `band->ComputeStatistics()` |
| 直方图 | 获取波段像素值分布 | `band->GetHistogram()` |
| NoData 设置 | 设置无效值 | `band->SetNoDataValue()` |
| 构建金字塔 | 加速大图显示 | `ds->BuildOverviews()` |

**重投影示例：**
```cpp
GDALDataset *srcDS = (GDALDataset*)GDALOpen("src.tif", GA_ReadOnly);
const char *args[] = {"-t_srs", "EPSG:32650", nullptr};
GDALWarpAppOptions *opt = GDALWarpAppOptionsNew((char**)args, nullptr);

GDALDatasetH srcHandle = (GDALDatasetH)srcDS;
GDALDataset *dstDS = (GDALDataset*)GDALWarp(
    "dst.tif", nullptr, 1, &srcHandle, opt, nullptr);

GDALWarpAppOptionsFree(opt);
GDALClose(srcDS);
GDALClose(dstDS);
```

---

### 5. 矢量数据读写（OGR）

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 打开矢量文件 | 读取 Shapefile / GeoJSON 等 | `GDALDataset *ds = (GDALDataset*)GDALOpenEx("file.shp", GDAL_OF_VECTOR, ...)` |
| 获取图层 | 获取第 0 个图层 | `OGRLayer *layer = ds->GetLayer(0)` |
| 遍历要素 | 循环读取所有要素 | `while((feat = layer->GetNextFeature()) != nullptr)` |
| 读取属性 | 读取字段值 | `feat->GetFieldAsString("name")` |
| 读取几何 | 获取要素几何体 | `OGRGeometry *geom = feat->GetGeometryRef()` |
| 创建要素 | 新建并写入要素 | `OGRFeature::CreateFeature(layer->GetLayerDefn())` |
| 空间过滤 | 按空间范围筛选要素 | `layer->SetSpatialFilterRect(minX, minY, maxX, maxY)` |
| 属性过滤 | 按 SQL 条件筛选 | `layer->SetAttributeFilter("population > 10000")` |

**遍历矢量要素示例：**
```cpp
GDALDataset *ds = (GDALDataset*)GDALOpenEx(
    "buildings.shp", GDAL_OF_VECTOR, nullptr, nullptr, nullptr);

OGRLayer *layer = ds->GetLayer(0);
layer->ResetReading();

OGRFeature *feat;
while ((feat = layer->GetNextFeature()) != nullptr) {
    const char *name = feat->GetFieldAsString("name");
    double area      = feat->GetFieldAsDouble("area");

    OGRGeometry *geom = feat->GetGeometryRef();
    if (geom && wkbFlatten(geom->getGeometryType()) == wkbPolygon) {
        OGRPolygon *poly = geom->toPolygon();
        // 处理多边形...
    }
    OGRFeature::DestroyFeature(feat);
}
GDALClose(ds);
```

---

### 6. 矢量空间分析（OGR 几何运算）

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 缓冲区 | 生成几何体的缓冲面 | `geom->Buffer(100.0)` |
| 相交 | 两个几何体取交集 | `geomA->Intersection(geomB)` |
| 并集 | 两个几何体取并集 | `geomA->Union(geomB)` |
| 差集 | 几何体相减 | `geomA->Difference(geomB)` |
| 包含判断 | 判断是否包含 | `geomA->Contains(geomB)` |
| 相交判断 | 判断是否相交 | `geomA->Intersects(geomB)` |
| 质心计算 | 获取多边形质心 | `poly->Centroid(&point)` |
| 面积计算 | 计算多边形面积 | `poly->get_Area()` |
| 矢量转栅格 | 将矢量烧录到栅格 | `GDALRasterizeLayers()` |
| 栅格转矢量 | 将栅格结果转为多边形 | `GDALPolygonize()` |

---

## 二、OpenCV C++ 功能详解

### 1. 图像基础操作

```cpp
#include <opencv2/opencv.hpp>
using namespace cv;
```

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 读取图像 | 默认读取为 BGR 格式 | `Mat img = imread("img.jpg", IMREAD_COLOR);` |
| 写入图像 | 保存为指定格式 | `imwrite("out.jpg", img);` |
| 显示图像 | 弹出预览窗口 | `imshow("window", img); waitKey(0);` |
| 颜色空间转换 | BGR <-> HSV / LAB / 灰度 | `cvtColor(img, gray, COLOR_BGR2GRAY);` |
| 图像缩放 | 按尺寸或比例缩放 | `resize(img, dst, Size(640, 480));` |
| 图像裁剪 | 截取感兴趣区域 | `Mat roi = img(Rect(x, y, w, h));` |
| 图像旋转 | 绕中心旋转任意角度 | `warpAffine(img, dst, getRotationMatrix2D(center, angle, 1.0), img.size());` |
| 图像翻转 | 水平/垂直翻转 | `flip(img, dst, 1);` |
| 获取图像信息 | 宽、高、通道数 | `img.cols`, `img.rows`, `img.channels()` |
| 图像类型转换 | uint8 <-> float32 | `img.convertTo(dst, CV_32F, 1.0/255.0);` |

---

### 2. 滤波与图像增强

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 高斯滤波 | 平滑去噪 | `GaussianBlur(src, dst, Size(5,5), 1.5);` |
| 均值滤波 | 简单均值平滑 | `blur(src, dst, Size(5,5));` |
| 中值滤波 | 去椒盐噪声 | `medianBlur(src, dst, 5);` |
| 双边滤波 | 保边去噪 | `bilateralFilter(src, dst, 9, 75, 75);` |
| 自定义卷积 | 使用自定义核 | `filter2D(src, dst, -1, kernel);` |
| 直方图均衡化 | 增强对比度（灰度图） | `equalizeHist(gray, dst);` |
| CLAHE 均衡化 | 限制对比度自适应均衡 | `createCLAHE(2.0, Size(8,8))->apply(gray, dst);` |
| 形态学操作 | 腐蚀/膨胀/开/闭运算 | `morphologyEx(src, dst, MORPH_OPEN, kernel);` |
| 图像加减 | 两图相加/相减 | `add(a, b, dst);` / `absdiff(a, b, dst);` |
| 归一化 | 将像素值映射到指定范围 | `normalize(src, dst, 0, 255, NORM_MINMAX, CV_8U);` |

---

### 3. 特征提取

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| Canny 边缘检测 | 经典边缘算法 | `Canny(gray, edges, 50, 150);` |
| Sobel 算子 | X/Y 方向梯度 | `Sobel(gray, dst, CV_32F, 1, 0, 3);` |
| Laplacian 算子 | 全方向边缘 | `Laplacian(gray, dst, CV_32F);` |
| Harris 角点 | 检测角点 | `cornerHarris(gray, dst, 2, 3, 0.04);` |
| Shi-Tomasi 角点 | 稳定角点检测 | `goodFeaturesToTrack(gray, corners, 200, 0.01, 10);` |
| SIFT 特征点 | 尺度不变特征 | `auto sift = SIFT::create(); sift->detectAndCompute(img, noArray(), kps, desc);` |
| ORB 特征点 | 快速开源特征 | `auto orb = ORB::create(); orb->detectAndCompute(img, noArray(), kps, desc);` |
| 特征点匹配 | BF 暴力匹配 | `BFMatcher matcher(NORM_HAMMING); matcher.match(desc1, desc2, matches);` |
| FLANN 匹配 | 快速近似匹配 | `FlannBasedMatcher matcher; matcher.knnMatch(desc1, desc2, matches, 2);` |
| 霍夫直线检测 | 检测图中直线 | `HoughLinesP(edges, lines, 1, CV_PI/180, 50, 50, 10);` |
| 霍夫圆检测 | 检测图中圆形 | `HoughCircles(gray, circles, HOUGH_GRADIENT, 1, 20, 100, 30, 1, 30);` |

---

### 4. 目标检测与分割

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 全局阈值 | 固定阈值二值化 | `threshold(gray, bin, 128, 255, THRESH_BINARY);` |
| Otsu 阈值 | 自动最优阈值 | `threshold(gray, bin, 0, 255, THRESH_BINARY \| THRESH_OTSU);` |
| 自适应阈值 | 局部动态阈值 | `adaptiveThreshold(gray, bin, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 11, 2);` |
| 轮廓检测 | 检测连通区域边界 | `findContours(bin, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);` |
| 轮廓面积 | 计算轮廓围成面积 | `contourArea(contours[i])` |
| 外接矩形 | 获取轮廓外接矩形 | `boundingRect(contours[i])` |
| 最小外接矩形 | 带旋转角度的外接矩形 | `minAreaRect(contours[i])` |
| 模板匹配 | 在大图中定位模板 | `matchTemplate(img, tmpl, result, TM_CCOEFF_NORMED);` |
| 分水岭分割 | 基于梯度分割 | `watershed(img, markers);` |
| DNN 推理 | 加载深度学习模型 | `dnn::Net net = dnn::readNet("yolo.weights", "yolo.cfg");` |

**轮廓检测完整示例：**
```cpp
Mat gray, bin;
cvtColor(img, gray, COLOR_BGR2GRAY);
threshold(gray, bin, 0, 255, THRESH_BINARY | THRESH_OTSU);

std::vector<std::vector<Point>> contours;
std::vector<Vec4i> hierarchy;
findContours(bin, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

for (size_t i = 0; i < contours.size(); i++) {
    double area = contourArea(contours[i]);
    if (area < 100) continue; // 过滤小轮廓

    Rect bbox = boundingRect(contours[i]);
    rectangle(img, bbox, Scalar(0, 255, 0), 2);
    drawContours(img, contours, (int)i, Scalar(0, 0, 255), 1);
}
```

---

### 5. 几何变换与图像配准

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 仿射变换 | 平移/旋转/缩放组合 | `warpAffine(src, dst, M, dst.size());` |
| 透视变换 | 四点视角矫正 | `warpPerspective(src, dst, H, dst.size());` |
| 求仿射矩阵 | 三点对应求变换矩阵 | `getAffineTransform(srcPts, dstPts)` |
| 求单应矩阵 | 特征点对应求单应矩阵 | `findHomography(srcPts, dstPts, RANSAC);` |
| 估计仿射变换 | 鲁棒仿射估计 | `estimateAffine2D(srcPts, dstPts);` |
| ECC 配准 | 最优变换矩阵估计 | `findTransformECC(src, dst, warpMat, MOTION_AFFINE);` |
| 图像拼接 | 全景图拼接 | `Stitcher::create()->stitch(images, result);` |

**透视矫正示例：**
```cpp
// 四个角点：左上、右上、右下、左下
std::vector<Point2f> srcPts = {{50,50},{400,30},{420,380},{20,400}};
std::vector<Point2f> dstPts = {{0,0},{400,0},{400,400},{0,400}};

Mat H = getPerspectiveTransform(srcPts, dstPts);
Mat dst;
warpPerspective(src, dst, H, Size(400, 400));
```

---

### 6. 视频处理

| 功能 | 说明 | C++ 示例 |
|------|------|----------|
| 读取视频 | 打开视频文件 | `VideoCapture cap("video.mp4");` |
| 读取摄像头 | 打开摄像头 | `VideoCapture cap(0);` |
| 逐帧读取 | 获取下一帧 | `cap >> frame;` |
| 写入视频 | 保存为视频文件 | `VideoWriter writer("out.mp4", VideoWriter::fourcc('m','p','4','v'), 30, Size(w,h));` |
| 写入帧 | 向视频写入一帧 | `writer << frame;` |
| 背景建模 | MOG2 背景去除 | `createBackgroundSubtractorMOG2()->apply(frame, fgMask);` |
| 目标追踪 | KCF 追踪器 | `TrackerKCF::create()->init(frame, bbox);` |

---

## 三、GDAL + OpenCV 联合使用（C++ 核心桥接）

### 关键：GDAL 数组 <-> OpenCV Mat 转换

```cpp
#include "gdal_priv.h"
#include <opencv2/opencv.hpp>

// 读取 GeoTIFF 单波段 -> cv::Mat
cv::Mat GdalBandToMat(const std::string &path, int bandIndex = 1) {
    GDALDataset *ds = (GDALDataset*)GDALOpen(path.c_str(), GA_ReadOnly);
    if (!ds) throw std::runtime_error("Cannot open file");

    GDALRasterBand *band = ds->GetRasterBand(bandIndex);
    int width  = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();

    cv::Mat mat(height, width, CV_32F);
    band->RasterIO(GF_Read, 0, 0, width, height,
                   mat.ptr<float>(), width, height, GDT_Float32, 0, 0);
    GDALClose(ds);
    return mat;
}

// cv::Mat -> 写回 GeoTIFF（保留地理参考）
void MatToGdalTiff(const cv::Mat &mat, const std::string &srcPath,
                   const std::string &dstPath) {
    GDALDataset *srcDS = (GDALDataset*)GDALOpen(srcPath.c_str(), GA_ReadOnly);
    double adfGT[6];
    srcDS->GetGeoTransform(adfGT);
    const char *pszProj = srcDS->GetProjectionRef();

    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset *dstDS = drv->Create(dstPath.c_str(),
                                      mat.cols, mat.rows, 1, GDT_Float32, nullptr);
    dstDS->SetGeoTransform(adfGT);
    dstDS->SetProjection(pszProj);

    GDALRasterBand *band = dstDS->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                   (void*)mat.ptr<float>(), mat.cols, mat.rows, GDT_Float32, 0, 0);

    GDALClose(srcDS);
    GDALClose(dstDS);
}
```

---

### 联合应用场景

#### 场景 1：遥感影像 NDVI 计算与可视化

```cpp
// 读取红波段和近红外波段
cv::Mat red = GdalBandToMat("sentinel.tif", 3);  // Band 3: Red
cv::Mat nir = GdalBandToMat("sentinel.tif", 4);  // Band 4: NIR

// 计算 NDVI = (NIR - Red) / (NIR + Red)
cv::Mat ndvi, diff, sum;
cv::subtract(nir, red, diff);
cv::add(nir, red, sum);
sum += 1e-10f; // 避免除零
cv::divide(diff, sum, ndvi);

// 归一化到 0~255 并伪彩色显示
cv::Mat ndvi_u8, colored;
cv::normalize(ndvi, ndvi_u8, 0, 255, cv::NORM_MINMAX, CV_8U);
cv::applyColorMap(ndvi_u8, colored, cv::COLORMAP_RdYlGn);

// 写回带地理坐标的 GeoTIFF
MatToGdalTiff(ndvi, "sentinel.tif", "ndvi_output.tif");
cv::imwrite("ndvi_visual.jpg", colored);
```

---

#### 场景 2：变化检测

```cpp
cv::Mat img1 = GdalBandToMat("before.tif");
cv::Mat img2 = GdalBandToMat("after.tif");

// 差值计算
cv::Mat diff;
cv::absdiff(img1, img2, diff);

// 归一化后阈值分割
cv::Mat diff_u8, changeMask;
cv::normalize(diff, diff_u8, 0, 255, cv::NORM_MINMAX, CV_8U);
cv::threshold(diff_u8, changeMask, 30, 255, cv::THRESH_BINARY);

// 形态学去噪
cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
cv::morphologyEx(changeMask, changeMask, cv::MORPH_OPEN, kernel);

// 保存结果（带地理参考）
changeMask.convertTo(changeMask, CV_32F);
MatToGdalTiff(changeMask, "before.tif", "change_result.tif");
```

---

#### 场景 3：地物轮廓提取并保存为矢量（GeoJSON）

```cpp
// 1. GDAL 读取影像和地理参数
GDALDataset *ds = (GDALDataset*)GDALOpen("landcover.tif", GA_ReadOnly);
double adfGT[6];
ds->GetGeoTransform(adfGT);

// 2. OpenCV 阈值分割
cv::Mat img = GdalBandToMat("landcover.tif");
cv::Mat u8, bin;
cv::normalize(img, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
cv::threshold(u8, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

// 3. 提取轮廓
std::vector<std::vector<cv::Point>> contours;
cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

// 4. OGR 创建矢量输出
GDALDriver *ogrDrv = GetGDALDriverManager()->GetDriverByName("GeoJSON");
GDALDataset *ogrDS = ogrDrv->Create("output.geojson", 0, 0, 0, GDT_Unknown, nullptr);
OGRLayer *layer = ogrDS->CreateLayer("polygons", nullptr, wkbPolygon, nullptr);

// 5. 像素坐标转地理坐标并写入矢量
for (auto &contour : contours) {
    if (cv::contourArea(contour) < 50) continue;
    OGRPolygon poly;
    OGRLinearRing ring;
    for (auto &pt : contour) {
        double geoX = adfGT[0] + pt.x * adfGT[1] + pt.y * adfGT[2];
        double geoY = adfGT[3] + pt.x * adfGT[4] + pt.y * adfGT[5];
        ring.addPoint(geoX, geoY);
    }
    ring.closeRings();
    poly.addRing(&ring);
    OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    feat->SetGeometry(&poly);
    layer->CreateFeature(feat);
    OGRFeature::DestroyFeature(feat);
}

GDALClose(ogrDS);
GDALClose(ds);
```

---

## 四、CMakeLists.txt 配置参考

```cmake
cmake_minimum_required(VERSION 3.15)
project(GdalOpenCVProject)

set(CMAKE_CXX_STANDARD 17)

# 查找 GDAL
find_package(GDAL REQUIRED)

# 查找 OpenCV
find_package(OpenCV REQUIRED)

add_executable(main main.cpp)

target_include_directories(main PRIVATE
    ${GDAL_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(main PRIVATE
    ${GDAL_LIBRARIES}
    ${OpenCV_LIBS}
)
```

---

## 五、常用数据类型对照表

| GDAL 枚举类型 | C++ 实际类型 | OpenCV Mat 类型 | 说明 |
|---------------|--------------|-----------------|------|
| GDT_Byte | uint8_t | CV_8U | 常见 8 位影像 |
| GDT_UInt16 | uint16_t | CV_16U | 遥感影像（Sentinel/Landsat）|
| GDT_Int16 | int16_t | CV_16S | DEM 高程 |
| GDT_Int32 | int32_t | CV_32S | 分类结果 |
| GDT_Float32 | float | CV_32F | NDVI、坡度等浮点 |
| GDT_Float64 | double | CV_64F | 高精度计算 |

---

## 六、常见错误处理模式

```cpp
// 始终检查 GDAL 返回值
GDALDataset *ds = (GDALDataset*)GDALOpen("file.tif", GA_ReadOnly);
if (ds == nullptr) {
    std::cerr << "GDAL Error: " << CPLGetLastErrorMsg() << std::endl;
    return -1;
}

// 始终检查 OpenCV Mat 是否为空
cv::Mat img = cv::imread("image.jpg");
if (img.empty()) {
    std::cerr << "OpenCV: Failed to load image" << std::endl;
    return -1;
}

// GDAL 资源释放（RAII 写法，推荐）
struct GDALDatasetDeleter {
    void operator()(GDALDataset *ds) { if (ds) GDALClose(ds); }
};
std::unique_ptr<GDALDataset, GDALDatasetDeleter> safeDS(
    (GDALDataset*)GDALOpen("file.tif", GA_ReadOnly));
```

---

*文档版本：v1.0 | 适用：GDAL 3.x + OpenCV 4.x + C++17*
