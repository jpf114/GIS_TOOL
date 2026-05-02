function(gis_gui_prepare_output_paths output_path screenshot_path)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)

    file(REMOVE "${output_path}" "${screenshot_path}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
endfunction()

function(gis_gui_prepare_artifact_paths output_path screenshot_path status_path)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)
    get_filename_component(status_dir "${status_path}" DIRECTORY)

    file(REMOVE "${output_path}" "${screenshot_path}" "${status_path}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
    file(MAKE_DIRECTORY "${status_dir}")
endfunction()

function(gis_gui_prepare_aux_paths screenshot_path status_path)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)
    get_filename_component(status_dir "${status_path}" DIRECTORY)

    file(REMOVE "${screenshot_path}" "${status_path}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
    file(MAKE_DIRECTORY "${status_dir}")
endfunction()

function(gis_gui_read_status_file test_name status_path out_success out_cancelled out_message out_raw_message)
    if(NOT EXISTS "${status_path}")
        message(FATAL_ERROR "${test_name} did not produce status file: ${status_path}")
    endif()

    file(READ "${status_path}" status_json)
    string(JSON status_success GET "${status_json}" success)
    string(JSON status_cancelled GET "${status_json}" cancelled)
    string(JSON status_message GET "${status_json}" message)
    string(JSON status_raw_message GET "${status_json}" raw_message)

    set(${out_success} "${status_success}" PARENT_SCOPE)
    set(${out_cancelled} "${status_cancelled}" PARENT_SCOPE)
    set(${out_message} "${status_message}" PARENT_SCOPE)
    set(${out_raw_message} "${status_raw_message}" PARENT_SCOPE)
endfunction()

function(gis_gui_assert_status_bool actual_value expected_value description)
    string(TOLOWER "${actual_value}" actual_normalized)
    string(TOLOWER "${expected_value}" expected_normalized)

    if(expected_normalized STREQUAL "true")
        if(NOT actual_normalized STREQUAL "true" AND NOT actual_normalized STREQUAL "on" AND NOT actual_normalized STREQUAL "1")
            message(FATAL_ERROR "${description}: expected true, got ${actual_value}")
        endif()
    else()
        if(NOT actual_normalized STREQUAL "false" AND NOT actual_normalized STREQUAL "off" AND NOT actual_normalized STREQUAL "0")
            message(FATAL_ERROR "${description}: expected false, got ${actual_value}")
        endif()
    endif()
endfunction()

function(gis_gui_assert_regression_result test_name exit_code stdout_text stderr_text output_path screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT EXISTS "${output_path}")
        message(FATAL_ERROR "${test_name} did not produce output: ${output_path}")
    endif()

    file(SIZE "${output_path}" output_size)
    if(output_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty output file: ${output_path}")
    endif()

    if(NOT EXISTS "${screenshot_path}")
        message(FATAL_ERROR "${test_name} did not produce screenshot: ${screenshot_path}")
    endif()

    file(SIZE "${screenshot_path}" screenshot_size)
    if(screenshot_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty screenshot: ${screenshot_path}")
    endif()

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_failure_result test_name exit_code stdout_text stderr_text output_path screenshot_path status_path expected_raw_message)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(EXISTS "${output_path}")
        file(SIZE "${output_path}" output_size)
        if(NOT output_size EQUAL 0)
            message(FATAL_ERROR
                "${test_name} unexpectedly produced output: ${output_path}")
        endif()
    endif()

    if(NOT EXISTS "${screenshot_path}")
        message(FATAL_ERROR "${test_name} did not produce screenshot: ${screenshot_path}")
    endif()

    file(SIZE "${screenshot_path}" screenshot_size)
    if(screenshot_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty screenshot: ${screenshot_path}")
    endif()

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "false"
        "${test_name} unexpectedly reported success")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")

    string(FIND "${status_raw_message}" "${expected_raw_message}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR
            "${test_name} reported unexpected raw failure message.\n"
            "expected substring: ${expected_raw_message}\n"
            "actual raw_message: ${status_raw_message}\n"
            "localized message: ${status_message}")
    endif()
endfunction()

function(gis_gui_assert_status_only_success test_name exit_code stdout_text stderr_text screenshot_path status_path subject_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT EXISTS "${subject_path}")
        message(FATAL_ERROR "${test_name} did not preserve expected file: ${subject_path}")
    endif()

    file(SIZE "${subject_path}" subject_size)
    if(subject_size EQUAL 0)
        message(FATAL_ERROR "${test_name} preserved an empty file: ${subject_path}")
    endif()

    if(NOT EXISTS "${screenshot_path}")
        message(FATAL_ERROR "${test_name} did not produce screenshot: ${screenshot_path}")
    endif()

    file(SIZE "${screenshot_path}" screenshot_size)
    if(screenshot_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty screenshot: ${screenshot_path}")
    endif()

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_status_success_no_output test_name exit_code stdout_text stderr_text screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT EXISTS "${screenshot_path}")
        message(FATAL_ERROR "${test_name} did not produce screenshot: ${screenshot_path}")
    endif()

    file(SIZE "${screenshot_path}" screenshot_size)
    if(screenshot_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty screenshot: ${screenshot_path}")
    endif()

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_directory_success test_name exit_code stdout_text stderr_text output_dir screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT IS_DIRECTORY "${output_dir}")
        message(FATAL_ERROR "${test_name} did not produce output directory: ${output_dir}")
    endif()

    file(GLOB output_entries "${output_dir}/*")
    list(LENGTH output_entries output_count)
    if(output_count EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty output directory: ${output_dir}")
    endif()

    if(NOT EXISTS "${screenshot_path}")
        message(FATAL_ERROR "${test_name} did not produce screenshot: ${screenshot_path}")
    endif()

    file(SIZE "${screenshot_path}" screenshot_size)
    if(screenshot_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty screenshot: ${screenshot_path}")
    endif()

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_generate_test_bmp image_path)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap 32, 32;"
        "for ($y = 0; $y -lt 32; $y++) {"
        "  for ($x = 0; $x -lt 32; $x++) {"
        "    $v = ($x * 8 + $y * 3) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($v, $v, $v);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_test_tiff image_path)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap 32, 32;"
        "for ($y = 0; $y -lt 32; $y++) {"
        "  for ($x = 0; $x -lt 32; $x++) {"
        "    $r = ($x * 7 + $y * 3) % 256;"
        "    $g = ($x * 5 + $y * 11) % 256;"
        "    $b = ($x * 13 + $y * 2) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($r, $g, $b);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Tiff);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_test_bmp_with_size image_path width height)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap ${width}, ${height};"
        "for ($y = 0; $y -lt ${height}; $y++) {"
        "  for ($x = 0; $x -lt ${width}; $x++) {"
        "    $v = ($x * 9 + $y * 5) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($v, $v, $v);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_feature_bmp image_path width height)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap ${width}, ${height};"
        "$g = [System.Drawing.Graphics]::FromImage($bmp);"
        "$g.Clear([System.Drawing.Color]::Black);"
        "$brushW = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White);"
        "$brushG = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::Gray);"
        "for ($y = 0; $y -lt ${height}; $y += 16) {"
        "  for ($x = 0; $x -lt ${width}; $x += 16) {"
        "    if ((($x + $y) / 16) % 2 -eq 0) { $g.FillRectangle($brushW, $x, $y, 8, 8) }"
        "    else { $g.FillRectangle($brushG, $x, $y, 8, 8) }"
        "  }"
        "}"
        "$pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 2);"
        "$g.DrawLine($pen, 0, 0, ${width}, ${height});"
        "$g.DrawLine($pen, 0, ${height}, ${width}, 0);"
        "$g.DrawEllipse($pen, 20, 20, 24, 24);"
        "$g.DrawRectangle($pen, 52, 18, 28, 20);"
        "$g.FillEllipse($brushW, 90, 90, 18, 18);"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$g.Dispose();"
        "$pen.Dispose();"
        "$brushW.Dispose();"
        "$brushG.Dispose();"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_write_geojson_dataset output_path dataset_kind)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")

    if(dataset_kind STREQUAL "projected_touching_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_touching_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "poly_a" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[100,0],[100,100],[0,100],[0,0]]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[100,0],[200,0],[200,100],[100,100],[100,0]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_overlapping_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_overlapping_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "poly_a" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[120,0],[120,120],[0,120],[0,0]]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[80,40],[200,40],[200,160],[80,160],[80,40]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_invalid_polygon")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_invalid_polygon",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "bowtie" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[120,120],[120,0],[0,120],[0,0]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_hole_polygon")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_hole_polygon",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "with_hole" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [
          [[0,0],[200,0],[200,200],[0,200],[0,0]],
          [[60,60],[140,60],[140,140],[60,140],[60,60]]
        ]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_multipart_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_multipart_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "multi_a" },
      "geometry": {
        "type": "MultiPolygon",
        "coordinates": [
          [[[0,0],[60,0],[60,60],[0,60],[0,0]]],
          [[[100,0],[160,0],[160,60],[100,60],[100,0]]]
        ]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,100],[80,100],[80,180],[0,180],[0,100]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_lines")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_lines",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "line_a" },
      "geometry": {
        "type": "LineString",
        "coordinates": [[0,0],[100,50],[200,50]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "line_b" },
      "geometry": {
        "type": "LineString",
        "coordinates": [[40,120],[160,120],[220,180]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_points_duplicate")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_points_duplicate",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "p1" },
      "geometry": { "type": "Point", "coordinates": [10, 10] }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "p2" },
      "geometry": { "type": "Point", "coordinates": [10, 10] }
    },
    {
      "type": "Feature",
      "properties": { "id": 3, "name": "p3" },
      "geometry": { "type": "Point", "coordinates": [50, 20] }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_nearest_source_points")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_nearest_source_points",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "src_a" },
      "geometry": { "type": "Point", "coordinates": [20, 20] }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "src_b" },
      "geometry": { "type": "Point", "coordinates": [180, 180] }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_nearest_target_points")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_nearest_target_points",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 101, "name": "target_a" },
      "geometry": { "type": "Point", "coordinates": [40, 40] }
    },
    {
      "type": "Feature",
      "properties": { "id": 102, "name": "target_b" },
      "geometry": { "type": "Point", "coordinates": [210, 200] }
    }
  ]
}
]=])
    else()
        message(FATAL_ERROR "Unsupported GeoJSON dataset kind: ${dataset_kind}")
    endif()

    file(WRITE "${output_path}" "${content}")
endfunction()
