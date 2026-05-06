#pragma once
#include <string>
#include <map>

namespace gis::framework {

struct Result {
    bool success = false;
    bool isCancelled = false;
    std::string message;
    std::string outputPath;
    std::map<std::string, std::string> metadata;

    static Result ok(const std::string& msg = "", const std::string& output = "") {
        return {true, false, msg, output, {}};
    }
    static Result fail(const std::string& msg) {
        return {false, false, msg, "", {}};
    }
    static Result cancelled(const std::string& msg = "操作已取消") {
        return {false, true, msg, "", {}};
    }
};

} // namespace gis::framework
