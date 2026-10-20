#include <gis/framework/execute_guarded.h>

#include <exception>

namespace gis::framework {

Result executeGuarded(IGisPlugin& plugin,
                      const std::map<std::string, ParamValue>& params,
                      gis::core::ProgressReporter& reporter,
                      bool* threwException) {
    if (threwException != nullptr) {
        *threwException = false;
    }

    try {
        return plugin.execute(params, reporter);
    } catch (const gis::core::CancelledException&) {
        // 取消是**正常结局**，不是失败：继承 std::runtime_error 只是为了能跨栈抛出。
        // 顺序必须在本 catch 之后才轮到 std::exception（见头文件）。
        return Result::cancelled();
    } catch (const std::exception& e) {
        if (threwException != nullptr) {
            *threwException = true;
        }
        return Result::fail(e.what());
    } catch (...) {
        // 非 std 派生的抛出（实测夹具是 `throw 42;`）。改前 CLI 侧没有任何 catch 接得住它，
        // 进程被 std::terminate 带走、用户拿不到一个字 —— 这个分支就是那条路径的终点。
        if (threwException != nullptr) {
            *threwException = true;
        }
        return Result::fail("Unknown exception (non-std)");
    }
}

} // namespace gis::framework
