#include <glog/logging.h>

#include "src/futures/futures.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);

  std::wstring error;
  auto environment = std::make_shared<afc::vm::Environment>();
  auto expr = afc::vm::CompileFile(
      "/dev/"
      "stdin",
      environment, &error);
  if (expr == nullptr) {
    return 0;
  }

  std::function<void()> resume;
  auto value = afc::vm::Evaluate(expr.get(), environment,
                                 [&resume](std::function<void()> callback) {
                                   resume = std::move(callback);
                                 });
  expr = nullptr;

  for (int i = 0; i < 5 && resume != nullptr; ++i) {
    auto copy = std::move(resume);
    resume = nullptr;
    copy();
  }

  return 0;
}
