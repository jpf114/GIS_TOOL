#pragma once
#include <string>
#include <map>

namespace gis::framework {

/** Result of a plugin execution. */
struct Result {
    bool success = false;              ///< Whether execution succeeded
    bool isCancelled = false;          ///< Whether execution was cancelled by user
    std::string message;               ///< Human-readable result message
    std::string outputPath;            ///< Path to the output file (if any)
    std::map<std::string, std::string> metadata; ///< Additional result metadata

    /** Create a successful result. */
    static Result ok(const std::string& msg = "", const std::string& output = "") {
        return {true, false, msg, output, {}};
    }
    /** Create a failed result. */
    static Result fail(const std::string& msg) {
        return {false, false, msg, "", {}};
    }
    /** Create a cancelled result. */
    static Result cancelled(const std::string& msg = "操作已取消") {
        return {false, true, msg, "", {}};
    }
};

} // namespace gis::framework
