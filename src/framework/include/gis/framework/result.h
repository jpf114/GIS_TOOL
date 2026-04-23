#pragma once
#include <string>
#include <map>

namespace gis::framework {

struct Result {
    bool success = false;
    std::string message;
    std::string outputPath;
    std::map<std::string, std::string> metadata;

    static Result ok(const std::string& msg = "", const std::string& output = "") {
        return {true, msg, output, {}};
    }
    static Result fail(const std::string& msg) {
        return {false, msg, "", {}};
    }
};

} // namespace gis::framework
