#pragma once

#include <filesystem>
#include <string>

namespace gis::core {

// N162/#156：全仓唯一一个"会建目录"的出口。会抛的 std::filesystem::create_directories
// 重载从这一刻起只允许出现在 src/core/src/fs_support.cpp 这一个文件里；其余调用点一律
// 收敛到这里，失败时抛 GisError，文案为本仓可读中文并**带动作名**（actionLabel 由调用点
// 传入，如 "vector convert"），绝不外吐 "create_directories:" 这类 std 原文。
// 语义与原会抛重载一致：directory 自身与缺失的父链逐级创建；已存在且是目录 = 幂等成功。
void ensureOutputDirectory(const std::filesystem::path& directory,
                           const std::string& actionLabel);

// 同一出口的便捷形态：为"要写的文件"确保其父目录存在（文件路径本身不会被建成目录）。
// 文件没有父目录（相对单名）时无事发生。语义与各插件原有的同名本地 helper 一致，
// 只是失败文案从 std 原文换成带动作名的本仓中文。
void ensureParentDirectoryForFile(const std::filesystem::path& file,
                                  const std::string& actionLabel);

} // namespace gis::core
