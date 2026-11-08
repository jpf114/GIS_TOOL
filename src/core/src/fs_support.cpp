#include <gis/core/fs_support.h>
#include <gis/core/error.h>

#include <system_error>

namespace gis::core {

namespace {

// 从 directory 自身往上找"已存在但不是目录"的最近祖先：那就是 create_directories
// 失败的第一因（N162/#156 的病灶形态——output 的父路径指向一个已存在的文件）。
// 找不到（纯权限/占用等系统层原因）返回空 path。
std::filesystem::path existingNonDirectoryAncestor(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path current = directory;
    while (!current.empty()) {
        if (fs::exists(current, ec) && !fs::is_directory(current, ec)) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

[[noreturn]] void throwDirectoryFailure(const std::filesystem::path& directory,
                                        const std::string& actionLabel) {
    std::string reason;
    const std::filesystem::path blocker = existingNonDirectoryAncestor(directory);
    if (!blocker.empty()) {
        reason = "路径“" + blocker.u8string() + "”是已存在的文件而非目录";
    } else {
        // 建不出来又不撞"已存在的文件"：权限/占用等系统层原因。只留数字错误码，
        // 不引 ec.message() 的英文原文——本刀的 CLI 判据禁 std 原文进用户可见输出。
        std::error_code probeEc;
        std::filesystem::create_directories(directory, probeEc);
        reason = "系统错误码 " + std::to_string(probeEc.value());
    }
    throw GisError("无法创建输出目录“" + directory.u8string() + "”：" + reason +
                   "（动作：" + actionLabel + "）");
}

} // namespace

void ensureOutputDirectory(const std::filesystem::path& directory,
                           const std::string& actionLabel) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        throwDirectoryFailure(directory, actionLabel);
    }
}

void ensureParentDirectoryForFile(const std::filesystem::path& file,
                                  const std::string& actionLabel) {
    if (!file.has_parent_path()) {
        return;
    }
    ensureOutputDirectory(file.parent_path(), actionLabel);
}

} // namespace gis::core
