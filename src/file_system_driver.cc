#include "src/file_system_driver.h"

#include "src/wstring.h"

namespace afc::editor {
namespace {
PossibleError SyscallReturnValue(std::wstring description, int return_value) {
  LOG(INFO) << "Syscall return value: " << description << ": " << return_value;
  return return_value == -1 ? Error(description + L" failed: " +
                                    FromByteString(strerror(errno)))
                            : Success();
}
}  // namespace

FileSystemDriver::FileSystemDriver(WorkQueue* work_queue)
    : evaluator_(L"FilesystemDriver", work_queue) {}

futures::Value<ValueOrError<int>> FileSystemDriver::Open(std::wstring path,
                                                         int flags,
                                                         mode_t mode) {
  return evaluator_.Run([path = std::move(path), flags, mode]() {
    LOG(INFO) << "Opening file:" << path;
    int fd = open(ToByteString(path).c_str(), flags, mode);
    PossibleError output = SyscallReturnValue(L"Open: " + path, fd);
    return output.IsError() ? Error(output.error.value()) : Success(fd);
  });
}

futures::Value<PossibleError> FileSystemDriver::Close(int fd) {
  return evaluator_.Run(
      [fd] { return SyscallReturnValue(L"Close", close(fd)); });
}

futures::ValueOrError<struct stat> FileSystemDriver::Stat(std::wstring path) {
  return evaluator_.Run([path =
                             std::move(path)]() -> ValueOrError<struct stat> {
    struct stat output;
    if (path.empty()) {
      return Error(L"Stat failed: Empty path.");
    } else if (stat(ToByteString(path).c_str(), &output) == -1) {
      return Error(L"Stat failed: `" + path + L"`: " +
                   FromByteString(strerror(errno)));
    }
    return Success(output);
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
