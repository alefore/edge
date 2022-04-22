#include "src/log.h"

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/time.h"
#include "src/language/wstring.h"

namespace afc::editor {
namespace {
using concurrent::ThreadPool;
using infrastructure::FileDescriptor;
using infrastructure::FileSystemDriver;
using infrastructure::HumanReadableTime;
using infrastructure::Now;
using infrastructure::Path;
using language::NonNull;
using language::Success;
using language::ToByteString;

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
  FileLog(std::shared_ptr<FileLogData> data)
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
  static void Write(std::shared_ptr<FileLogData> data, int id,
                    std::wstring statement) {
    CHECK(data != nullptr);
    auto time = HumanReadableTime(Now());
    LoggingThreadPool().RunIgnoringResult(
        [data = std::move(data),
         statement = ToByteString(
             (time.IsError() ? L"[error:" + time.error().description + L"]"
                             : time.value()) +
             L" " + std::to_wstring(id) + L": " + statement + L"\n")] {
          return write(data->fd.read(), statement.c_str(), statement.size());
        });
  }

  const std::shared_ptr<FileLogData> data_;
  int id_;
};
}  // namespace
futures::ValueOrError<language::NonNull<std::unique_ptr<Log>>> NewFileLog(
    FileSystemDriver* file_system, Path path) {
  LOG(INFO) << "Opening log: " << path;
  return file_system
      ->Open(path, O_WRONLY | O_CREAT | O_APPEND,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
      .Transform([](FileDescriptor fd) {
        return Success<NonNull<std::unique_ptr<Log>>>(
            NonNull<std::unique_ptr<FileLog>>(
                std::make_shared<FileLogData>(FileLogData{.fd = fd})));
      });
}

NonNull<std::unique_ptr<Log>> NewNullLog() { return NullLog::New(); }

}  // namespace afc::editor
