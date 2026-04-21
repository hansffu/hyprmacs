#pragma once

#include <functional>
#include <string>

namespace hyprmacs {

class FocusController {
  public:
    using DispatchExecutor = std::function<int(const std::string&)>;

    explicit FocusController(DispatchExecutor dispatch_executor);

    bool focus_client(const std::string& client_id);

  private:
    DispatchExecutor dispatch_executor_;
};

}  // namespace hyprmacs
