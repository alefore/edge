#include "src/log.h"

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/time_human.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"

using afc::concurrent::ThreadPool;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::HumanReadableTime;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
ThreadPool& LoggingThreadPool() {
  static ThreadPool* output = new ThreadPool(1);
  return *output;
}

class NullLog : public Log {
 public:
  static NonNull<std::unique_ptr<Log>> New() {
    return NonNull<std::unique_ptr<NullLog>>();
  }

  void Append(LazyString) override {}
  NonNull<std::unique_ptr<Log>> NewChild(LazyString) override { return New(); }
};

struct FileLogData {
  const FileDescriptor fd;
  int next_id = 0;
};

class FileLog : public Log {
 public:
  FileLog(NonNull<std::shared_ptr<FileLogData>> data)
      : data_(std::move(data)), id_(data_->next_id++) {
    Write(data_, id_, LazyString{L"Start"});
  }

  ~FileLog() override { Write(data_, id_, LazyString{L"End"}); }

  void Append(LazyString statement) override {
    Write(data_, id_, LazyString{L"Info: "} + statement);
  }

  NonNull<std::unique_ptr<Log>> NewChild(LazyString name) override {
    Write(data_, id_,
          LazyString{L"New Child: id:"} +
              LazyString{std::to_wstring(data_->next_id)} + LazyString{L": "} +
              name);
    return NonNull<std::unique_ptr<FileLog>>(data_);
  }

 private:
  static void Write(NonNull<std::shared_ptr<FileLogData>> data, int id,
                    LazyString statement) {
    ValueOrError<std::wstring> time = HumanReadableTime(Now());
    LazyString full_statement =
        std::visit(overload{[](Error error) {
                              return LazyString{L"[error:"} + error.read() +
                                     LazyString{L"]"};
                            },
                            [](std::wstring value) {
                              // TODO(trivial, 2024-08-28):
                              // Change the parameter to
                              // already be a LazyString and
                              // remove this conversion.
                              return LazyString{value};
                            }},
                   time) +
        LazyString{L" "} + LazyString{std::to_wstring(id)} + LazyString{L": "} +
        statement + LazyString{L"\n"};
    LoggingThreadPool().RunIgnoringResult(
        [data = std::move(data), statement = full_statement.ToBytes()] {
          return write(data->fd.read(), statement.c_str(), statement.size());
        });
  }

  const NonNull<std::shared_ptr<FileLogData>> data_;
  int id_;
};
}  // namespace
futures::ValueOrError<language::NonNull<std::unique_ptr<Log>>> NewFileLog(
    FileSystemDriver& file_system, Path path) {
  LOG(INFO) << "Opening log: " << path;
  return file_system
      .Open(path, O_WRONLY | O_CREAT | O_APPEND,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
      .Transform(
          [](FileDescriptor fd) -> ValueOrError<NonNull<std::unique_ptr<Log>>> {
            return NonNull<std::unique_ptr<FileLog>>(
                MakeNonNullShared<FileLogData>(FileLogData{.fd = fd}));
          });
}

NonNull<std::unique_ptr<Log>> NewNullLog() { return NullLog::New(); }

}  // namespace afc::editor
