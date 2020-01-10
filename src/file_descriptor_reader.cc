#include "src/file_descriptor_reader.h"

#include <cctype>
#include <ostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string.h"
#include "src/time.h"
#include "src/tracker.h"
#include "src/wstring.h"

namespace afc::editor {

FileDescriptorReader::FileDescriptorReader(Options options)
    : options_(std::make_shared<Options>(std::move(options))) {
  CHECK(options_->buffer != nullptr);
  CHECK(options_->fd != -1);
  CHECK(options_->background_callback_runner != nullptr);
}

FileDescriptorReader::~FileDescriptorReader() { close(options_->fd); }

int FileDescriptorReader::fd() const { return options_->fd; }

struct timespec FileDescriptorReader::last_input_received() const {
  return last_input_received_;
}

double FileDescriptorReader::lines_read_rate() const {
  return lines_read_rate_->GetEventsPerSecond();
}

std::optional<struct pollfd> FileDescriptorReader::GetPollFd() const {
  struct pollfd output;
  output.fd = fd();
  output.events = POLLIN | POLLPRI;
  return output;
}

FileDescriptorReader::ReadResult FileDescriptorReader::ReadData() {
  LOG(INFO) << "Reading input from " << options_->fd << " for buffer "
            << options_->buffer->Read(buffer_variables::name);
  static const size_t kLowBufferSize = 1024 * 60;
  if (low_buffer_ == nullptr) {
    CHECK_EQ(low_buffer_length_, 0ul);
    low_buffer_.reset(new char[kLowBufferSize]);
  }
  ssize_t characters_read = read(fd(), low_buffer_.get() + low_buffer_length_,
                                 kLowBufferSize - low_buffer_length_);
  LOG(INFO) << "Read returns: " << characters_read;
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return ReadResult::kContinue;
    }
    return ReadResult::kDone;
  }
  CHECK_GE(characters_read, 0);
  CHECK_LE(characters_read, ssize_t(kLowBufferSize - low_buffer_length_));
  if (characters_read == 0) {
    return ReadResult::kDone;
  }
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

  shared_ptr<LazyString> buffer_wrapper(NewLazyString(std::move(buffer)));
  VLOG(5) << "Input: [" << buffer_wrapper->ToString() << "]";

  size_t processed = low_buffer_tmp == nullptr
                         ? low_buffer_length_
                         : low_buffer_tmp - low_buffer_.get();
  VLOG(5) << options_->buffer->Read(buffer_variables::name)
          << ": Characters consumed: " << processed
          << ", produced: " << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length_);
  memmove(low_buffer_.get(), low_buffer_tmp, low_buffer_length_ - processed);
  low_buffer_length_ -= processed;
  if (low_buffer_length_ == 0) {
    LOG(INFO) << "Consumed all input.";
    low_buffer_ = nullptr;
  }

  if (options_->buffer->Read(buffer_variables::vm_exec)) {
    LOG(INFO) << options_->buffer->Read(buffer_variables::name)
              << ": Evaluating VM code: " << buffer_wrapper->ToString();
    options_->buffer->EvaluateString(buffer_wrapper->ToString());
  }

  clock_gettime(0, &last_input_received_);
  options_->buffer->RegisterProgress();
  if (options_->terminal != nullptr) {
    options_->terminal->ProcessCommandInput(buffer_wrapper, [this]() {
      lines_read_rate_->IncrementAndGetEventsPerSecond(1.0);
    });
  } else {
    ParseAndInsertLines(buffer_wrapper);
  }
  return ReadResult::kContinue;
}

std::vector<std::shared_ptr<Line>> CreateLineInstances(
    std::shared_ptr<LazyString> contents, const LineModifierSet& modifiers) {
  static Tracker tracker(L"FileDescriptorReader::CreateLineInstances");
  auto tracker_call = tracker.Call();

  std::vector<std::shared_ptr<Line>> lines_to_insert;
  ColumnNumber line_start;
  for (ColumnNumber i; i.ToDelta() < ColumnNumberDelta(contents->size()); i++) {
    if (contents->get(i) == '\n') {
      VLOG(8) << "Adding line from " << line_start << " to " << i;

      Line::Options line_options;
      line_options.contents =
          Substring(contents, line_start, ColumnNumber(i) - line_start);
      line_options.modifiers[ColumnNumber(0)] = modifiers;
      lines_to_insert.emplace_back(
          std::make_shared<Line>(std::move(line_options)));

      line_start = ColumnNumber(i) + ColumnNumberDelta(1);
    }
  }

  VLOG(8) << "Adding last line from " << line_start << " to "
          << contents->size();
  Line::Options line_options;
  line_options.contents = Substring(contents, line_start);
  line_options.modifiers[ColumnNumber(0)] = modifiers;
  lines_to_insert.emplace_back(std::make_shared<Line>(std::move(line_options)));
  return lines_to_insert;
}

void InsertLines(const FileDescriptorReader::Options& options,
                 std::vector<std::shared_ptr<Line>> lines_to_insert) {
  static Tracker tracker(L"FileDescriptorReader::InsertLines");
  auto tracker_call = tracker.Call();

  bool previous_modified = options.buffer->modified();
  auto follower = options.buffer->GetEndPositionFollower();

  for (auto it = lines_to_insert.begin(); it != lines_to_insert.end(); ++it) {
    if (it == lines_to_insert.begin()) {
      options.buffer->AppendToLastLine(std::move(**it));
    } else {
      options.buffer->StartNewLine(std::move(*it));
    }
  }

  if (!previous_modified) {
    options.buffer->ClearModified();  // These changes don't count.
  }
}

void FileDescriptorReader::ParseAndInsertLines(
    std::shared_ptr<LazyString> contents) {
  options_->background_callback_runner->Push(
      [options = options_, lines_read_rate = lines_read_rate_,
       contents]() mutable {
        auto lines =
            CreateLineInstances(std::move(contents), options->modifiers);
        options->buffer->work_queue()->Schedule(
            [options, lines_read_rate, lines = std::move(lines)]() mutable {
              lines_read_rate->IncrementAndGetEventsPerSecond(lines.size() - 1);
              InsertLines(*options, std::move(lines));
            });
      });
}
}  // namespace afc::editor
