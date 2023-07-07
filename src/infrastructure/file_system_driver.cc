#include "src/infrastructure/file_system_driver.h"

#include "src/language/wstring.h"

namespace afc::infrastructure {
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

namespace {
PossibleError SyscallReturnValue(std::wstring description, int return_value) {
  LOG(INFO) << "Syscall return value: " << description << ": " << return_value;
  return return_value == -1 ? Error(description + L" failed: " +
                                    FromByteString(strerror(errno)))
                            : Success();
}
}  // namespace

FileSystemDriver::FileSystemDriver(concurrent::ThreadPool& thread_pool)
    : thread_pool_(thread_pool) {}

futures::ValueOrError<FileDescriptor> FileSystemDriver::Open(
    Path path, int flags, mode_t mode) const {
  return thread_pool_.Run(
      [path = std::move(path), flags, mode]() -> ValueOrError<FileDescriptor> {
        LOG(INFO) << "Opening file:" << path;
        int fd = open(ToByteString(path.read()).c_str(), flags, mode);
        ASSIGN_OR_RETURN(EmptyValue value,
                         SyscallReturnValue(L"Open: " + path.read(), fd));
        (void)value;
        return Success(FileDescriptor(fd));
      });
}

futures::Value<ssize_t> FileSystemDriver::Read(FileDescriptor fd, void* buf,
                                               size_t count) {
  return thread_pool_.Run(
      [fd, buf, count] { return read(fd.read(), buf, count); });
}

futures::Value<PossibleError> FileSystemDriver::Close(FileDescriptor fd) const {
  return thread_pool_.Run(
      [fd] { return SyscallReturnValue(L"Close", close(fd.read())); });
}

futures::Value<PossibleError> FileSystemDriver::Unlink(Path path) const {
  return thread_pool_.Run([path = std::move(path)]() {
    return SyscallReturnValue(L"Unlink",
                              unlink(ToByteString(path.read()).c_str()));
  });
}

futures::ValueOrError<struct stat> FileSystemDriver::Stat(Path path) const {
  return thread_pool_.Run([path = std::move(
                               path)]() -> language::ValueOrError<struct stat> {
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

}  // namespace afc::infrastructure
