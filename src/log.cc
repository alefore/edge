#include "src/log.h"

#include "src/time.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {
class NullLog : public Log {
 public:
  static std::unique_ptr<Log> New() { return std::make_unique<NullLog>(); }

  void Append(std::wstring) override {}
  std::unique_ptr<Log> NewChild(std::wstring) override { return New(); }
};

struct FileLogData {
  const int fd;
  int next_id = 0;
  AsyncEvaluator async_evaluator;
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

  std::unique_ptr<Log> NewChild(std::wstring name) {
    Write(data_, id_,
          L"New Child: id:" + std::to_wstring(data_->next_id) + L": " + name);
    return std::make_unique<FileLog>(data_);
  }

 private:
  static void Write(std::shared_ptr<FileLogData> data, int id,
                    std::wstring statement) {
    CHECK(data != nullptr);
    auto time = HumanReadableTime(Now());
    auto data_raw = data.get();
    futures::Transform(
        data_raw->async_evaluator.Run(
            [data = data_raw,
             statement = ToByteString(
                 (time.IsError() ? L"[error:" + time.error.value() + L"]"
                                 : time.value.value()) +
                 L" " + std::to_wstring(id) + L": " + statement + L"\n")] {
              CHECK(data != nullptr);
              return write(data->fd, statement.c_str(), statement.size());
            }),
        [data = std::move(data)](int) { return Success(); });
  }

  const std::shared_ptr<FileLogData> data_;
  int id_;
};
}  // namespace
futures::ValueOrError<std::unique_ptr<Log>> NewFileLog(
    WorkQueue* work_queue, FileSystemDriver* file_system, std::wstring path) {
  LOG(INFO) << "Opening log: " << path;
  return futures::Transform(
      file_system->Open(
          path, O_WRONLY | O_CREAT | O_APPEND,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
      [work_queue](int fd) {
        // TODO(easy): Make `Success` able to convert the unique_ptr to its
        // parent class? So that we can use make_unique<FileLog> here.
        return Success(std::unique_ptr<Log>(
            new FileLog(std::make_shared<FileLogData>(FileLogData{
                .fd = fd,
                .async_evaluator = AsyncEvaluator(L"Log", work_queue)}))));
      });
}

std::unique_ptr<Log> NewNullLog() { return NullLog::New(); }

}  // namespace afc::editor
