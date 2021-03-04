#include "src/log.h"

#include "src/time.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {
AsyncEvaluator* LoggingEvaluator() {
  static AsyncEvaluator* output = new AsyncEvaluator(L"LogEvaluator", nullptr);
  return output;
}

class NullLog : public Log {
 public:
  static std::unique_ptr<Log> New() { return std::make_unique<NullLog>(); }

  void Append(std::wstring) override {}
  std::unique_ptr<Log> NewChild(std::wstring) override { return New(); }
};

struct FileLogData {
  const int fd;
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
    LoggingEvaluator()->RunIgnoringResults(
        [data = std::move(data),
         statement = ToByteString(
             (time.IsError() ? L"[error:" + time.error().description + L"]"
                             : time.value()) +
             L" " + std::to_wstring(id) + L": " + statement + L"\n")] {
          return write(data->fd, statement.c_str(), statement.size());
        });
  }

  const std::shared_ptr<FileLogData> data_;
  int id_;
};
}  // namespace
futures::ValueOrError<std::unique_ptr<Log>> NewFileLog(
    FileSystemDriver* file_system, Path path) {
  LOG(INFO) << "Opening log: " << path;
  return futures::Transform(
      file_system->Open(
          path.ToString(), O_WRONLY | O_CREAT | O_APPEND,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
      [](int fd) {
        // TODO(easy): Make `Success` able to convert the unique_ptr to its
        // parent class? So that we can use make_unique<FileLog> here.
        return Success(std::unique_ptr<Log>(
            new FileLog(std::make_shared<FileLogData>(FileLogData{.fd = fd}))));
      });
}

std::unique_ptr<Log> NewNullLog() { return NullLog::New(); }

}  // namespace afc::editor
