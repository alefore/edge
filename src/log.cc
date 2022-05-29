#include "src/log.h"

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/time.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"

namespace afc::editor {
namespace {
using concurrent::ThreadPool;
using infrastructure::FileDescriptor;
using infrastructure::FileSystemDriver;
using infrastructure::HumanReadableTime;
using infrastructure::Now;
using infrastructure::Path;
using language::Error;
using language::MakeNonNullShared;
using language::NonNull;
using language::overload;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

ThreadPool& LoggingThreadPool() {
  static ThreadPool* output = new ThreadPool(1, nullptr);
  return *output;
}

class NullLog : public Log {
 public:
  static NonNull<std::unique_ptr<Log>> New() {
    return NonNull<std::unique_ptr<NullLog>>();
  }

  void Append(std::wstring) override {}
  NonNull<std::unique_ptr<Log>> NewChild(std::wstring) override {
    return New();
  }
};

struct FileLogData {
  const FileDescriptor fd;
  int next_id = 0;
};

class FileLog : public Log {
 public:
  FileLog(NonNull<std::shared_ptr<FileLogData>> data)
      : data_(std::move(data)), id_(data_->next_id++) {
    Write(data_, id_, L"Start");
  }

  ~FileLog() override { Write(data_, id_, L"End"); }

  void Append(std::wstring statement) {
    Write(data_, id_, L"Info: " + std::move(statement));
  }

  NonNull<std::unique_ptr<Log>> NewChild(std::wstring name) {
    Write(data_, id_,
          L"New Child: id:" + std::to_wstring(data_->next_id) + L": " + name);
    return NonNull<std::unique_ptr<FileLog>>(data_);
  }

 private:
  static void Write(NonNull<std::shared_ptr<FileLogData>> data, int id,
                    std::wstring statement) {
    ValueOrError<std::wstring> time = HumanReadableTime(Now());
    LoggingThreadPool().RunIgnoringResult(
        [data = std::move(data),
         statement = ToByteString(
             std::visit(overload{[](Error error) {
                                   return L"[error:" + error.description + L"]";
                                 },
                                 [](std::wstring value) { return value; }},
                        time) +
             L" " + std::to_wstring(id) + L": " + statement + L"\n")] {
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
