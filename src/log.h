#ifndef __AFC_EDITOR_LOG_H__
#define __AFC_EDITOR_LOG_H__

#include <memory>
#include <string>

#include "src/async_processor.h"
#include "src/file_system_driver.h"
#include "src/futures/futures.h"
#include "src/value_or_error.h"

namespace afc::editor {
class Log {
 public:
  virtual ~Log() {}
  virtual void Append(std::wstring statement) = 0;
  virtual std::unique_ptr<Log> NewChild(std::wstring name) = 0;
};

// file_system may be deleted as soon as this function returns (i.e., before the
// future has a value).
futures::ValueOrError<std::unique_ptr<Log>> NewFileLog(
    FileSystemDriver* file_system, Path path);

std::unique_ptr<Log> NewNullLog();

template <typename Callable>
auto RunAndLog(Log* log, std::wstring name, Callable callable) {
  auto sub_log = log->NewChild(name);
  return callable();
}

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LOG_H__
