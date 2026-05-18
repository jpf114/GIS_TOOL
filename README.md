# GIS Tool

鍩轰簬 `C++17 + GDAL + OpenCV + PROJ + Qt` 鐨勬彃浠跺紡 GIS / 閬ユ劅绠楁硶宸ヤ綔鍙帮紝鎻愪緵 CLI 鍜?GUI 涓ょ鍏ュ彛锛屽綋鍓嶄粎鏀寔 Windows銆?
## 褰撳墠鐘舵€?
- 褰撳墠鐗堟湰锛歚v1.0.0`
- 鏍囧噯鏋勫缓鐩綍锛?
  - `build/debug`
  - `build/release`
- 榛樿瀹夎涓庝氦浠橈細`Release`
- 渚濊禆缁熶竴澶嶇敤鍏ㄥ眬 `vcpkg`
  - 褰撳墠椤圭洰绾﹀畾璺緞锛歚D:\Develop\vcpkg`
- 褰撳墠浜у搧杈圭晫锛氫粎绠楁硶宸ヤ綔鍙帮紝涓嶅寘鍚湴鍥惧睍绀哄钩鍙拌兘鍔?
### 鏈€鏂伴獙鏀剁姸鎬侊紙2026-05-15锛?
- `cmake --build build/debug --config Debug --target gis_tests` 閫氳繃
- `ctest --test-dir build/debug -C Debug -N`锛氬彂鐜?`405` 涓祴璇?- `ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest" --output-on-failure`锛歚45/45` 閫氳繃
- `ctest --test-dir build/debug -C Debug -R "gui_smoke_startup" --output-on-failure`锛歚1/1` 閫氳繃
- `build/debug/src/cli/Debug/gis-cli.exe --list` 姝ｅ父锛屽彲鍒楀嚭 13 涓彃浠?- `build/debug/src/gui/Debug/gis-gui.exe -platform offscreen --self-test` 姝ｅ父
- Debug 鍏ㄩ噺 `ctest --test-dir build/debug -C Debug --output-on-failure` 鏈湴鎵ц瓒呰繃 5 鍒嗛挓锛屾湭鍦ㄦ湰杞畬鎴愶紱鍙戝竷鍓嶄粛闇€瀹屾暣璺戝畬
- GUI 浠诲姟闃熷垪銆佹壒閲忓鐞嗐€佽€楁椂缁熻銆侀敊璇汉鎬у寲鍔熻兘姝ｅ父
- `cmake --build build/release --config Release --target real_matching_regression` 閫氳繃
- `cmake --build build/release --config Release --target real_matching_regression_full` 閫氳繃
- `cmake --build build/release --config Release --target real_raster_regression` 閫氳繃
- `cmake --build build/release --config Release --target real_raster_regression_full` 閫氳繃
- `cmake --build build/release --config Release --target real_vector_regression` 閫氳繃
- `cmake --build build/release --config Release --target real_vector_regression_full` 閫氳繃
- `tmp/` 褰撳墠涓虹┖

## 涓诲姛鑳?

- 鎶曞奖杞崲
- 褰卞儚瑁佸垏涓庨暥宓?
- 鐗瑰緛鍖归厤涓庨厤鍑?
- 褰卞儚澶勭悊涓庡垎鏋?
- 鍒嗙被缁熻
- 鍑犱綍鏍℃涓庤緪灏勫鐞?
- 鍦板舰鍒嗘瀽
- 鍏夎氨鎸囨暟
- 鏍呮牸宸ュ叿
- 鐭㈤噺鏁版嵁澶勭悊

## 鎻掍欢涓庝富鍔熻兘鏄犲皠

- `projection` -> 鎶曞奖杞崲
- `cutting` -> 褰卞儚瑁佸垏涓庨暥宓?
- `matching` -> 鐗瑰緛鍖归厤涓庨厤鍑?
- `processing` -> 褰卞儚澶勭悊涓庡垎鏋?
- `classification` -> 鍒嗙被缁熻
- `georef` -> 鍑犱綍鏍℃涓庤緪灏勫鐞?
- `terrain` -> 鍦板舰鍒嗘瀽
- `spindex` -> 鍏夎氨鎸囨暟
- `raster_manage / raster_inspect / raster_render / raster_math` -> 鏍呮牸宸ュ叿
- `vector` -> 鐭㈤噺鏁版嵁澶勭悊

## GUI 涓诲姛鑳藉綊骞?

- GUI 宸︿晶瀵艰埅宸插皢 `raster_manage / raster_inspect / raster_render / raster_math` 鍚堝苟涓哄崟涓€鈥滄爡鏍煎伐鍏封€濅富椤?
- `classification` 涓婚」缁熶竴浣跨敤鈥滃垎绫荤粺璁♀€濓紝鐩戠潱鍒嗙被鍔ㄤ綔涓哄叾瀛愬姛鑳?
- GUI 涓?CLI 鏈€缁堥兘钀藉埌鍚屼竴濂楁彃浠舵墽琛岄摼

## 褰撳墠宸茶ˉ榻愮殑閲嶇偣鑳藉姏

### processing

- `gabor_filter`
- `glcm_texture`
- `mean_shift_filter`
- `skeleton`
- `connected_components`
- `pansharpen`

### classification

- `feature_stats`
- `svm_classify`
- `random_forest_classify`
- `max_likelihood_classify`

### georef

- `dos_correction`
- `radiometric_calibration`
- `gcp_register`
- `cosine_correction`
- `minnaert_correction`
- `c_correction`
- `percentile_stretch`
- `rpc_orthorectify`

### projection

- `info`
- `transform`
- `assign_srs`
- `reproject`

### cutting

- `clip`
- `mosaic`
- `split`
- `merge_bands`

### vector

- `intersect`
- `simplify`
- `repair`
- `geom_metrics`
- `nearest`
- `spatial_join`
- `adjacency`
- `overlap_check`
- `topology_check`
- `convex_hull`
- `centroid`
- `envelope`
- `boundary`
- `multipart_check`
- `singlepart`
- `vertices_extract`
- `endpoints_extract`
- `midpoints_extract`
- `interior_point`
- `duplicate_point_check`
- `hole_check`
- `dangling_endpoint_check`
- `sliver_remove`

## 瀹屾暣鎬ц鏄?

浠ヤ笂閲嶇偣鑳藉姏褰撳墠閮藉凡缁忔寜鏃㈡湁椤圭洰鏍囧噯琛ュ埌浠ヤ笅灞傜骇锛?

- 搴曞眰绠楁硶 / 鎻掍欢瀹炵幇
- CLI 璋冪敤閾捐矾
- GUI 鎺ュ叆
- GUI 鍙傛暟鏍￠獙
- 鎻掍欢娴嬭瘯
- GUI support 娴嬭瘯
- GUI 绂诲睆鍥炲綊
- 鐪熷疄鏁版嵁涓撻」鍥炲綊
- Release 瀹夎涓庡惎鍔ㄩ獙璇?

## 鏋勫缓

### Debug

```powershell
cmake -S . -B build/debug -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

### Release

```powershell
cmake -S . -B build/release -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/release --config Release
cmake --install build/release --config Release
```

绾﹀畾锛?
- 鏃ュ父寮€鍙戦獙璇佷娇鐢?`build/debug`
- 榛樿瀹夎浣跨敤 `build/release`
- 鍙戝竷鍓嶈嚦灏戞墽琛屼竴娆?`Debug 鍏ㄩ噺娴嬭瘯 + Release 缂栬瘧瀹夎`

## 鍙戝竷鍓嶆爣鍑嗛獙璇佸熀绾?
鍙戝竷鍓嶈嚦灏戝畬鏁存墽琛屼竴娆′互涓嬪懡浠わ細

```powershell
ctest --test-dir build/debug -C Debug --output-on-failure
ctest --test-dir build/release -C Release --output-on-failure
cmake --build build/release --config Release --target real_raster_regression
cmake --build build/release --config Release --target real_matching_regression
cmake --build build/release --config Release --target real_vector_regression
```

## 鐪熷疄鏁版嵁涓撻」鍥炲綊

### Debug

```powershell
cmake --build build/debug --config Debug --target real_matching_regression
cmake --build build/debug --config Debug --target real_matching_regression_full
cmake --build build/debug --config Debug --target real_raster_regression
cmake --build build/debug --config Debug --target real_raster_regression_full
cmake --build build/debug --config Debug --target real_vector_regression
cmake --build build/debug --config Debug --target real_vector_regression_full
```

### Release

```powershell
cmake --build build/release --config Release --target real_matching_regression
cmake --build build/release --config Release --target real_matching_regression_full
cmake --build build/release --config Release --target real_raster_regression
cmake --build build/release --config Release --target real_raster_regression_full
cmake --build build/release --config Release --target real_vector_regression
cmake --build build/release --config Release --target real_vector_regression_full
```

褰撳墠宸茬粡纭閫氳繃鐨勪笓椤瑰寘鎷細

- `matching`
  - `detect / corner / match / register / change`
  - Release 杩藉姞锛歚ecc_register / stitch`
- `projection`
  - `info / transform / assign_srs / reproject`
- `cutting`
  - `clip / mosaic / split / merge_bands`
- `processing`
  - `pansharpen / gabor_filter / glcm_texture / mean_shift_filter / skeleton / connected_components`
- `classification`
  - `feature_stats / svm_classify / random_forest_classify / max_likelihood_classify`
  - `full` 杩藉姞锛歚feature_stats_csv`
- `georef`
  - `dos_correction / radiometric_calibration / gcp_register / cosine_correction / minnaert_correction / c_correction / percentile_stretch / rpc_orthorectify`
- `spindex`
  - `ndvi / ndmi / evi / evi2 / savi / osavi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index`
- `terrain`
  - 褰撳墠宸插疄鐜板姩浣滅殑鐪熷疄鏁版嵁鍥炲綊閾捐矾
- `vector`
  - 褰撳墠涓婚摼鍔ㄤ綔鐨勭湡瀹炴暟鎹洖褰掗摼璺?

褰撳墠 `real_raster_regression` 鐨勯噸鐐归獙鏀跺彛寰勪负锛?

- `classification.feature_stats`
  - `quick`锛氶獙璇?`json / vector_output / raster_output`
  - `full`锛氳拷鍔?`csv`
  - 棰濆鏍￠獙 `actual_srs` 涓?`__summary__`
- `classification.svm_classify / random_forest_classify / max_likelihood_classify`
  - 楠岃瘉杈撳嚭灏哄 `24 x 12 x 1`
  - 楠岃瘉杈撳嚭绫诲瀷 `Float32`
  - 楠岃瘉绫诲埆鑼冨洿 `1~2`
- `projection.info / transform / assign_srs / reproject`
  - 楠岃瘉灏哄銆丒PSG 缂栫爜銆佸潗鏍囪浆鎹㈢粨鏋滀笌閲嶆姇褰辫緭鍑?
- `cutting.clip / mosaic / split / merge_bands`
  - 楠岃瘉杈撳嚭灏哄銆佺摝鐗囨暟閲忋€佹尝娈垫暟閲忎笌鍏抽敭缁熻鍊?
- `processing.pansharpen`
  - 鍥哄畾楠岃瘉 `pan_method=simple_mean`
- `processing.gabor_filter / glcm_texture / mean_shift_filter`
  - 楠岃瘉杈撳嚭灏哄 `32 x 32 x 1`
  - 楠岃瘉杈撳嚭绫诲瀷 `Float32`
- `processing.skeleton / connected_components`
  - 楠岃瘉杈撳嚭灏哄 `64 x 64 x 1`
  - `skeleton` 鏍￠獙鏈€澶у€?`1.0`
  - `connected_components` 鏍￠獙鏈€澶ф爣绛惧€?`4`
- `georef`
  - 8 涓姩浣滃浐瀹氭牎楠岃緭鍑哄昂瀵搞€佽緭鍑虹被鍨嬨€丆RS 鎴栧叧閿粺璁″€?
- `spindex`
  - 鍥哄畾楠岃瘉涓绘祦鎸囨暟杈撳嚭
  - `custom_index` 浣跨敤 `preset=ndvi_alias / ndmi_alias` 浣滀负绋冲畾楠屾敹鍏ュ彛
- `terrain`
  - 褰撳墠宸查澶栨牎楠?`slope / profile_extract / viewshed_multi` 绛夊叧閿粨鏋?

## 浣跨敤

### 鍒楀嚭鎻掍欢

```powershell
.\install\bin\gis-cli.exe --list
```

### 杩愯绠楁硶

```powershell
.\install\bin\gis-cli.exe <plugin> <action> --input <path> --output <path>
```

### 鍚姩 GUI

```powershell
.\install\bin\gis-gui.exe
```

## 鏂囨。

- 绠楁硶鎬昏锛歔docs/绠楁硶璇存槑/鎬昏.md](/D:/Develop/GIS/GIS_TOOL/docs/绠楁硶璇存槑/鎬昏.md)
- 瀵归綈楠岃瘉娓呭崟锛歔docs/GUI_CLI_搴曞眰瀵归綈楠岃瘉娓呭崟.md](/D:/Develop/GIS/GIS_TOOL/docs/GUI_CLI_搴曞眰瀵归綈楠岃瘉娓呭崟.md)
- 鍚庣画宸ヤ綔璁″垝锛歔docs/鍚庣画宸ヤ綔璁″垝.md](/D:/Develop/GIS/GIS_TOOL/docs/鍚庣画宸ヤ綔璁″垝.md)

## 璇存槑

- 鏂囨。缁熶竴浣跨敤涓枃
- 鎻愪氦淇℃伅缁熶竴浣跨敤涓枃
- 褰撳墠闃舵浼樺厛淇濊瘉绠€鍗曘€佸彲缁存姢銆佸彲鍥炲綊
- `pointcloud` 褰撳墠涓嶈鍏ュ凡瀹屾垚涓绘ā鍧楋紝鐩稿叧渚濊禆鏉′欢灏氭湭婊¤冻
