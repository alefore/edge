#ifndef __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__
#define __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__

#include <map>
#include <vector>

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::infrastructure {

struct FileDescriptorValidator {
  static language::PossibleError Validate(const int& fd);
};

class FileDescriptor
    : public language::GhostType<FileDescriptor, int, FileDescriptorValidator> {
  using GhostType::GhostType;
};

class UnixSignal : public language::GhostType<UnixSignal, int> {
  using GhostType::GhostType;
};

// Why define a GhostType<> for `pid_t`, which is already a "specific" type?
// Because pid_t is just a `using` or `typedef` alias, so incorrect usage isn't
// detected by the compiler.
class ProcessId : public language::GhostType<ProcessId, pid_t> {
  using GhostType::GhostType;
};

// Class used to interact with the file system. All operations are performed
// asynchronously in a thread pool.
class FileSystemDriver {
  concurrent::ThreadPoolWithWorkQueue& thread_pool_;

 public:
  FileSystemDriver(concurrent::ThreadPoolWithWorkQueue& thread_pool);

  futures::ValueOrError<std::vector<Path>> Glob(
      language::lazy_string::LazyString pattern);

  futures::ValueOrError<FileDescriptor> Open(Path path, int flags,
                                             mode_t mode) const;
  futures::Value<ssize_t> Read(FileDescriptor, void* buf, size_t count);
  futures::Value<language::PossibleError> Close(FileDescriptor fd) const;
  futures::Value<language::PossibleError> Unlink(Path path) const;
  futures::ValueOrError<struct stat> Stat(Path path) const;
  futures::Value<language::PossibleError> Rename(Path oldpath,
                                                 Path newpath) const;
  futures::Value<language::PossibleError> Mkdir(Path path, mode_t mode) const;
  language::PossibleError Kill(ProcessId, UnixSignal);

  struct WaitPidOutput {
    ProcessId pid;
    int wstatus;
  };
  futures::ValueOrError<WaitPidOutput> WaitPid(ProcessId pid, int options);

  futures::ValueOrError<std::vector<ProcessId>> GetChildren(ProcessId pid);

  // Similar to GetChildren, but recurses on the children to return the
  // transitive set.
  //
  // If `limit` is specified, stops (with success) when this number of ancestors
  // have been read.
  futures::ValueOrError<std::map<ProcessId, std::vector<ProcessId>>>
  GetAncestors(ProcessId pid, std::optional<size_t> ancestors_limit);

  // Allow a FileSystemDriver to be managed by a gc::Pool.
  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};

}  // namespace afc::infrastructure

#endif  // __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__
