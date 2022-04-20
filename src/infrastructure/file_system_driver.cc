#include "src/infrastructure/file_system_driver.h"

#include "src/language/wstring.h"

namespace afc::editor {
namespace {
PossibleError SyscallReturnValue(std::wstring description, int return_value) {
  LOG(INFO) << "Syscall return value: " << description << ": " << return_value;
  return return_value == -1 ? Error(description + L" failed: " +
                                    FromByteString(strerror(errno)))
                            : Success();
}
}  // namespace

FileSystemDriver::FileSystemDriver(ThreadPool& thread_pool)
    : thread_pool_(thread_pool) {}

futures::Value<ValueOrError<FileDescriptor>> FileSystemDriver::Open(
    Path path, int flags, mode_t mode) const {
  return thread_pool_.Run([path = std::move(path), flags, mode]() {
    LOG(INFO) << "Opening file:" << path;
    int fd = open(ToByteString(path.read()).c_str(), flags, mode);
    PossibleError output = SyscallReturnValue(L"Open: " + path.read(), fd);
    return output.IsError() ? output.error() : Success(FileDescriptor(fd));
  });
}

futures::Value<PossibleError> FileSystemDriver::Close(FileDescriptor fd) const {
  return thread_pool_.Run(
      [fd] { return SyscallReturnValue(L"Close", close(fd.read())); });
}

futures::ValueOrError<struct stat> FileSystemDriver::Stat(Path path) const {
  return thread_pool_.Run([path =
                               std::move(path)]() -> ValueOrError<struct stat> {
    struct stat output;
    if (stat(ToByteString(path.read()).c_str(), &output) == -1) {
      return Error(L"Stat failed: `" + path.read() + L"`: " +
                   FromByteString(strerror(errno)));
    }
    return Success(output);
  });
}

futures::Value<PossibleError> FileSystemDriver::Rename(Path oldpath,
                                                       Path newpath) const {
  return thread_pool_.Run([oldpath, newpath] {
    return SyscallReturnValue(L"Rename",
                              rename(ToByteString(oldpath.read()).c_str(),
                                     ToByteString(newpath.read()).c_str()));
  });
}

futures::Value<PossibleError> FileSystemDriver::Mkdir(Path path,
                                                      mode_t mode) const {
  return thread_pool_.Run([path, mode] {
    return AugmentErrors(
        path.read(),
        SyscallReturnValue(L"Mkdir",
                           mkdir(ToByteString(path.read()).c_str(), mode)));
  });
}

}  // namespace afc::editor
