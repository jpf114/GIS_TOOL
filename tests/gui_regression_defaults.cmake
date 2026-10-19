# Default Qt platform for GUI regression scripts.
# Must match the headless plugin copied beside gis_tests (see tests/CMakeLists.txt).
if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

# 平台名不再构成"跳过截图判定"的理由：批次 B / B7（N98）删掉了这里的
#   gis_gui_platform_skips_screenshot()。**实测过并产出有内容位图的是 offscreen 与 minimal**
#   （windows 那臂从没测过，别说成实测过 —— 它本来也不在旧的跳过臂里，删这半句不影响任何结论）
#   ⇒ 那条按名字一刀切的判据是个恒真的跳过，
#   它把全部 gui_ 用例的截图判定短路成死代码。判据本体见 helpers 的
#   gis_gui_assert_screenshot_rendered()（结构层 + 内容层两层）。
# GUI_PLATFORM 仍只是"用哪个 Qt 平台跑"，与"截图算不算数"解耦。
