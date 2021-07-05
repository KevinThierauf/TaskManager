#include "Task.hpp"

namespace tskm {
    const TypedTaskResult<void> TypedTaskResult<void>::SUCCESS{std::monostate()}; // NOLINT(cert-err58-cpp)

    std::optional<std::string> detail::getExceptionMessage(const std::exception_ptr *exception) {
        try {
            if (exception) {
                std::rethrow_exception(*exception);
            }
            return std::nullopt;
        } catch(const std::exception &ex) {
            return ex.what();
        } catch(...) {
            return "unknown exception";
        }
    }
}