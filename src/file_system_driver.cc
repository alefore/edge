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

FileSystemDriver::FileSystemDriver(std::shared_ptr<WorkQueue> work_queue)
    : evaluator_(L"FilesystemDriver", work_queue) {
  CHECK(work_queue != nullptr);
}

futures::Value<ValueOrError<int>> FileSystemDriver::Open(Path path, int flags,
                                                         mode_t mode) const {
  return evaluator_.Run([path = std::move(path), flags, mode]() {
    LOG(INFO) << "Opening file:" << path;
    int fd = open(ToByteString(path.ToString()).c_str(), flags, mode);
    PossibleError output = SyscallReturnValue(L"Open: " + path.ToString(), fd);
    return output.IsError() ? output.error() : Success(fd);
  });
}

futures::Value<PossibleError> FileSystemDriver::Close(int fd) const {
  return evaluator_.Run(
      [fd] { return SyscallReturnValue(L"Close", close(fd)); });
}

futures::ValueOrError<struct stat> FileSystemDriver::Stat(Path path) const {
  return evaluator_.Run([path =
                             std::move(path)]() -> ValueOrError<struct stat> {
    struct stat output;
    if (stat(ToByteString(path.ToString()).c_str(), &output) == -1) {
      return Error(L"Stat failed: `" + path.ToString() + L"`: " +
                   FromByteString(strerror(errno)));
    }
    return Success(output);
  });
}

futures::Value<PossibleError> FileSystemDriver::Rename(Path oldpath,
                                                       Path newpath) const {
  return evaluator_.Run([oldpath, newpath] {
    return SyscallReturnValue(L"Rename",
                              rename(ToByteString(oldpath.ToString()).c_str(),
                                     ToByteString(newpath.ToString()).c_str()));
  });
}

futures::Value<PossibleError> FileSystemDriver::Mkdir(Path path,
                                                      mode_t mode) const {
  return evaluator_.Run([path, mode] {
    return AugmentErrors(
        path.ToString(),
        SyscallReturnValue(L"Mkdir",
                           mkdir(ToByteString(path.ToString()).c_str(), mode)));
  });
}

}  // namespace afc::editor
