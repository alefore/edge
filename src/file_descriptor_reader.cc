#include "src/file_descriptor_reader.h"

#include <cctype>
#include <ostream>

#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/text/line.h"
#include "src/language/wstring.h"

using afc::infrastructure::FileDescriptor;
using afc::infrastructure::Tracker;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::EmptyString;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumberDelta;

namespace afc::editor {
using ::operator<<;

FileDescriptorReader::FileDescriptorReader(Options options)
    : options_(MakeNonNullShared<Options>(std::move(options))) {
  CHECK(fd() != FileDescriptor(-1));
}

FileDescriptorReader::~FileDescriptorReader() { close(fd().read()); }

FileDescriptor FileDescriptorReader::fd() const { return options_->fd; }

struct timespec FileDescriptorReader::last_input_received() const {
  return last_input_received_;
}

std::optional<struct pollfd> FileDescriptorReader::GetPollFd() const {
  if (state_ == State::kProcessing) return std::nullopt;
  struct pollfd output;
  output.fd = fd().read();
  output.events = POLLIN | POLLPRI;
  output.revents = 0;
  return output;
}

std::variant<FileDescriptorReader::EndOfFile,
             FileDescriptorReader::ReadDataInput>
FileDescriptorReader::ReadData() {
  LOG(INFO) << "Reading input from " << options_->fd << " for buffer "
            << options_->name;
  static const size_t kLowBufferSize = 1024 * 60;
  if (low_buffer_ == nullptr) {
    CHECK_EQ(low_buffer_length_, 0ul);
    low_buffer_.reset(new char[kLowBufferSize]);
  }
  ssize_t characters_read =
      read(fd().read(), low_buffer_.get() + low_buffer_length_,
           kLowBufferSize - low_buffer_length_);
  LOG(INFO) << "Read returns: " << characters_read;
  if (characters_read == -1) {
    if (errno == EAGAIN) return ReadDataInput{.input = EmptyString()};
    return EndOfFile{};
  }
  CHECK_GE(characters_read, 0);
  CHECK_LE(characters_read, ssize_t(kLowBufferSize - low_buffer_length_));
  if (characters_read == 0) return EndOfFile{};
  low_buffer_length_ += characters_read;

  static Tracker chars_tracker(
      L"FileDescriptorReader::ReadData::UnicodeConversion");
  auto chars_tracker_call = chars_tracker.Call();

  const char* low_buffer_tmp = low_buffer_.get();
  int output_characters =
      mbsnrtowcs(nullptr, &low_buffer_tmp, low_buffer_length_, 0, nullptr);
  std::vector<wchar_t> buffer(output_characters == -1 ? low_buffer_length_
                                                      : output_characters);

  low_buffer_tmp = low_buffer_.get();
  if (output_characters == -1) {
    low_buffer_tmp = nullptr;
    for (size_t i = 0; i < low_buffer_length_; i++) {
      buffer[i] = static_cast<wchar_t>(*(low_buffer_.get() + i));
    }
  } else {
    mbsnrtowcs(&buffer[0], &low_buffer_tmp, low_buffer_length_, buffer.size(),
               nullptr);
  }

  chars_tracker_call = nullptr;

  NonNull<std::shared_ptr<LazyString>> buffer_wrapper =
      NewLazyString(std::move(buffer));
  VLOG(5) << "Input: [" << buffer_wrapper->ToString() << "]";

  size_t processed = low_buffer_tmp == nullptr
                         ? low_buffer_length_
                         : low_buffer_tmp - low_buffer_.get();
  VLOG(5) << options_->name << ": Characters consumed: " << processed
          << ", produced: " << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length_);
  memmove(low_buffer_.get(), low_buffer_tmp, low_buffer_length_ - processed);
  low_buffer_length_ -= processed;
  if (low_buffer_length_ == 0) {
    LOG(INFO) << "Consumed all input.";
    low_buffer_ = nullptr;
  }

  clock_gettime(0, &last_input_received_);
  state_ = State::kProcessing;
  return ReadDataInput{.input = std::move(buffer_wrapper)};
}

void FileDescriptorReader::ResumeReading() {
  CHECK(state_ == State::kProcessing);
  state_ = State::kReading;
}

}  // namespace afc::editor
