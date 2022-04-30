#ifndef __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__
#define __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"

namespace afc::infrastructure {

GHOST_TYPE(FileDescriptor, int);

// Class used to interact with the file system. All operations are performed
// asynchronously in a thread pool.
class FileSystemDriver {
 public:
  FileSystemDriver(concurrent::ThreadPool& thread_pool);

  futures::ValueOrError<FileDescriptor> Open(Path path, int flags,
                                             mode_t mode) const;
  futures::Value<language::PossibleError> Close(FileDescriptor fd) const;
  futures::Value<language::PossibleError> Unlink(Path path) const;
  futures::ValueOrError<struct stat> Stat(Path path) const;
  futures::Value<language::PossibleError> Rename(Path oldpath,
                                                 Path newpath) const;
  futures::Value<language::PossibleError> Mkdir(Path path, mode_t mode) const;

 private:
  concurrent::ThreadPool& thread_pool_;
};

}  // namespace afc::infrastructure

GHOST_TYPE_TOP_LEVEL(afc::infrastructure::FileDescriptor);

#endif  // __AFC_INFRASTRUCTURE_FILE_SYSTEM_DRIVER_H__
