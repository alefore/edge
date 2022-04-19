#ifndef __AFC_EDITOR_FILE_SYSTEM_DRIVER_H__
#define __AFC_EDITOR_FILE_SYSTEM_DRIVER_H__

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/dirname.h"
#include "src/futures/futures.h"
#include "src/thread_pool.h"

namespace afc::editor {

GHOST_TYPE(FileDescriptor, int);

// Class used to interact with the file system. All operations are performed
// asynchronously in a background thread; once their results are available, the
// corresponding future is notified through `work_queue` (to switch back to the
// main thread).
class FileSystemDriver {
 public:
  FileSystemDriver(ThreadPool& thread_pool);

  futures::Value<ValueOrError<FileDescriptor>> Open(Path path, int flags,
                                                    mode_t mode) const;
  futures::Value<PossibleError> Close(FileDescriptor fd) const;
  futures::Value<ValueOrError<struct stat>> Stat(Path path) const;
  futures::Value<PossibleError> Rename(Path oldpath, Path newpath) const;
  futures::Value<PossibleError> Mkdir(Path path, mode_t mode) const;

 private:
  ThreadPool& thread_pool_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_FILE_SYSTEM_DRIVER_H__
