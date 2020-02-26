#include "src/file_system_driver.h"

#include "src/wstring.h"

namespace afc::editor {
namespace {
PossibleError SyscallReturnValue(std::wstring name, int return_value) {
  LOG(INFO) << "Syscall return value: " << name << ": " << return_value;
  return return_value == 0
             ? Success()
             : Error(name + L" failed: " + FromByteString(strerror(errno)));
}
}  // namespace

FileSystemDriver::FileSystemDriver(WorkQueue* work_queue)
    : evaluator_(L"FilesystemDriver", work_queue) {}

futures::Value<int> FileSystemDriver::Open(std::wstring path, int flags,
                                           mode_t mode) {
  return evaluator_.Run([path = std::move(path), flags, mode]() {
    return open(ToByteString(path).c_str(), flags, mode);
  });
}

futures::Value<PossibleError> FileSystemDriver::Close(int fd) {
  return evaluator_.Run(
      [fd] { return SyscallReturnValue(L"Close", close(fd)); });
}

futures::Value<std::optional<struct stat>> FileSystemDriver::Stat(
    std::wstring path) {
  return evaluator_.Run([path =
                             std::move(path)]() -> std::optional<struct stat> {
    struct stat output;
    if (path.empty() || stat(ToByteString(path).c_str(), &output) == -1) {
      return std::nullopt;
    }
    return output;
  });
}

futures::Value<PossibleError> FileSystemDriver::Rename(std::wstring oldpath,
                                                       std::wstring newpath) {
  return evaluator_.Run([oldpath, newpath] {
    return SyscallReturnValue(L"Rename", rename(ToByteString(oldpath).c_str(),
                                                ToByteString(newpath).c_str()));
  });
}

}  // namespace afc::editor
