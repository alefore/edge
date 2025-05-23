#include "src/infrastructure/file_system_driver.h"

#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

extern "C" {
#include <glob.h>
#include <sys/wait.h>
}

#include "src/language/container.h"
#include "src/language/error/view.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"

namespace container = afc::language::container;

using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::view::ExtractErrors;

namespace afc::infrastructure {

using ::operator<<;

PossibleError FileDescriptorValidator::Validate(const int& fd) {
  if (fd < 0)
    return Error{LazyString{L"Invalid file descriptor: negative value."}};
  return Success();
}

namespace {
PossibleError SyscallReturnValue(LazyString description, int return_value) {
  LOG(INFO) << "Syscall return value: " << description << ": " << return_value;
  return return_value == -1 ? Error{description + LazyString{L": Failure: "} +
                                    LazyString{FromByteString(strerror(errno))}}
                            : Success();
}

PossibleError SyscallReturnValue(const Path& path, LazyString description,
                                 int return_value) {
  return SyscallReturnValue(path.read() + LazyString{L": "} + description,
                            return_value);
}
}  // namespace

FileSystemDriver::FileSystemDriver(
    concurrent::ThreadPoolWithWorkQueue& thread_pool)
    : thread_pool_(thread_pool) {}

futures::ValueOrError<std::vector<Path>> FileSystemDriver::Glob(
    LazyString pattern) {
  return thread_pool_.Run([pattern]() -> ValueOrError<std::vector<Path>> {
    glob_t output_glob;
    switch (glob(pattern.ToBytes().c_str(), 0, nullptr, &output_glob)) {
      case GLOB_NOSPACE:
        return Error{LazyString{L"Out of memory"}};
      case GLOB_ABORTED:
        return Error{LazyString{L"Aborted"}};
      case GLOB_NOMATCH:
        return Error{LazyString{L"No match"}};
    }
    return ExtractErrors(container::MaterializeVector(
        std::views::counted(output_glob.gl_pathv, output_glob.gl_pathc) |
        std::views::transform([](char* input) {
          return Path::New(LazyString{FromByteString(input)});
        })));
  });
}

futures::ValueOrError<FileDescriptor> FileSystemDriver::Open(
    Path path, int flags, mode_t mode) const {
  return thread_pool_.Run(
      [path = std::move(path), flags, mode]() -> ValueOrError<FileDescriptor> {
        LOG(INFO) << "Opening file:" << path;
        int fd = open(path.ToBytes().c_str(), flags, mode);
        ASSIGN_OR_RETURN(EmptyValue value,
                         SyscallReturnValue(path, LazyString{L"Open"}, fd));
        (void)value;
        return FileDescriptor::New(fd);
      });
}

futures::Value<ssize_t> FileSystemDriver::Read(FileDescriptor fd, void* buf,
                                               size_t count) {
  return thread_pool_.Run(
      [fd, buf, count] { return read(fd.read(), buf, count); });
}

futures::Value<PossibleError> FileSystemDriver::Close(FileDescriptor fd) const {
  return thread_pool_.Run([fd] {
    return SyscallReturnValue(LazyString{L"Close"}, close(fd.read()));
  });
}

futures::Value<PossibleError> FileSystemDriver::Unlink(Path path) const {
  return thread_pool_.Run([path = std::move(path)]() {
    return SyscallReturnValue(path, LazyString{L"Unlink"},
                              unlink(path.ToBytes().c_str()));
  });
}

futures::ValueOrError<struct stat> FileSystemDriver::Stat(Path path) const {
  return thread_pool_.Run(
      [path = std::move(path)]() -> language::ValueOrError<struct stat> {
        struct stat output;
        if (stat(path.ToBytes().c_str(), &output) == -1) {
          Error error{LazyString{L"Stat failed: `"} + path.read() +
                      LazyString{L"`: "} +
                      LazyString{FromByteString(strerror(errno))}};
          LOG(INFO) << error;
          return error;
        }
        return Success(output);
      });
}

futures::Value<PossibleError> FileSystemDriver::Rename(Path oldpath,
                                                       Path newpath) const {
  return thread_pool_.Run([oldpath, newpath] {
    return SyscallReturnValue(
        LazyString{L"Rename"},
        rename(oldpath.ToBytes().c_str(), newpath.ToBytes().c_str()));
  });
}

futures::Value<PossibleError> FileSystemDriver::Mkdir(Path path,
                                                      mode_t mode) const {
  return thread_pool_.Run([path, mode] {
    return AugmentError(
        path.read(), SyscallReturnValue(path, LazyString{L"Mkdir"},
                                        mkdir(path.ToBytes().c_str(), mode)));
  });
}

PossibleError FileSystemDriver::Kill(ProcessId pid, UnixSignal sig) {
  if (kill(pid.read(), sig.read()) == -1)
    Error(LazyString{L"Kill: "} + LazyString{FromByteString(strerror(errno))});
  return Success();
}

futures::ValueOrError<FileSystemDriver::WaitPidOutput>
FileSystemDriver::WaitPid(ProcessId pid, int options) {
  return thread_pool_.Run([pid, options]() -> ValueOrError<WaitPidOutput> {
    int wstatus;
    if (waitpid(pid.read(), &wstatus, options) == -1)
      return Error(LazyString{L"Waitpid: "} +
                   LazyString{FromByteString(strerror(errno))});
    return WaitPidOutput{.pid = pid, .wstatus = wstatus};
  });
}

namespace {
ValueOrError<std::vector<ProcessId>> ReadChildrenBlocking(ProcessId pid) {
  std::vector<ProcessId> output;
  try {
    std::ifstream infile("/proc/" + std::to_string(pid.read()) + "/task/" +
                         std::to_string(pid.read()) + "/children");
    int child_pid_int;
    while (infile >> child_pid_int) output.push_back(ProcessId{child_pid_int});
  } catch (const std::exception& e) {
    return Error(LazyString{FromByteString(e.what())});
  }
  return output;
}
}  // namespace

futures::ValueOrError<std::vector<ProcessId>> FileSystemDriver::GetChildren(
    ProcessId pid) {
  return thread_pool_.Run(std::bind_front(ReadChildrenBlocking, pid));
}

futures::ValueOrError<std::map<ProcessId, std::vector<ProcessId>>>
FileSystemDriver::GetAncestors(ProcessId pid,
                               std::optional<size_t> ancestors_limit) {
  using Output = std::map<ProcessId, std::vector<ProcessId>>;
  return thread_pool_.Run(
      [pid, ancestors_limit]() -> language::ValueOrError<Output> {
        Output output;
        std::vector<ProcessId> inputs = {pid};
        while (!inputs.empty() && (!ancestors_limit.has_value() ||
                                   output.size() < ancestors_limit.value())) {
          ProcessId entry = inputs.back();
          inputs.pop_back();
          std::vector<ProcessId>& entry_output = output[entry];
          CHECK(entry_output.empty());
          ASSIGN_OR_RETURN(entry_output, ReadChildrenBlocking(entry));
          for (ProcessId& child : entry_output)
            // The following check seems pointless but ... there could be race
            // conditions between our attempt to read the processes table and
            // ... the processes executing. If it ever happens that we re-visit
            // a process, we just skip it.
            if (!output.contains(child)) inputs.push_back(child);
        }
        return output;
      });
}

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
FileSystemDriver::Expand() const {
  return {};
}
}  // namespace afc::infrastructure
