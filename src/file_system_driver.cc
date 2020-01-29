#include "src/file_system_driver.h"

#include "src/wstring.h"

namespace afc::editor {
FileSystemDriver::FileSystemDriver(WorkQueue* work_queue)
    : evaluator_(L"FilesystemDriver", work_queue) {}

futures::Value<int> FileSystemDriver::Open(std::wstring path, int mode) {
  return evaluator_.Run(std::function<int()>([path = std::move(path), mode]() {
    return open(ToByteString(path).c_str(), mode);
  }));
}

futures::Value<std::optional<struct stat>> FileSystemDriver::Stat(
    std::wstring path) {
  return evaluator_.Run(std::function<std::optional<struct stat>()>(
      [path = std::move(path)]() -> std::optional<struct stat> {
        struct stat output;
        if (path.empty() || stat(ToByteString(path).c_str(), &output) == -1) {
          return std::nullopt;
        }
        return output;
      }));
}

}  // namespace afc::editor
