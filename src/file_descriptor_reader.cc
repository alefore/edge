#include "src/file_descriptor_reader.h"

#include <cctype>
#include <ostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string.h"
#include "src/time.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

namespace {
vector<unordered_set<LineModifier, hash<int>>> ModifiersVector(
    const unordered_set<LineModifier, hash<int>>& input, size_t size) {
  return vector<unordered_set<LineModifier, hash<int>>>(size, input);
}
}  // namespace

FileDescriptorReader::FileDescriptorReader(Options options)
    : options_(std::move(options)), lines_read_rate_(2.0) {
  CHECK(options_.buffer != nullptr);
  CHECK(options_.fd != -1);
}

FileDescriptorReader::~FileDescriptorReader() { close(options_.fd); }

int FileDescriptorReader::fd() const { return options_.fd; }

struct timespec FileDescriptorReader::last_input_received() const {
  return last_input_received_;
}

double FileDescriptorReader::lines_read_rate() const {
  return lines_read_rate_.GetEventsPerSecond();
}

FileDescriptorReader::ReadResult FileDescriptorReader::ReadData() {
  EditorState* editor_state = options_.buffer->editor();
  LOG(INFO) << "Reading input from " << options_.fd << " for buffer "
            << options_.buffer->Read(buffer_variables::name);
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

  shared_ptr<LazyString> buffer_wrapper(NewLazyString(std::move(buffer)));
  VLOG(5) << "Input: [" << buffer_wrapper->ToString() << "]";

  size_t processed = low_buffer_tmp == nullptr
                         ? low_buffer_length_
                         : low_buffer_tmp - low_buffer_.get();
  VLOG(5) << options_.buffer->Read(buffer_variables::name)
          << ": Characters consumed: " << processed
          << ", produced: " << buffer_wrapper->size();
  CHECK_LE(processed, low_buffer_length_);
  memmove(low_buffer_.get(), low_buffer_tmp, low_buffer_length_ - processed);
  low_buffer_length_ -= processed;
  if (low_buffer_length_ == 0) {
    LOG(INFO) << "Consumed all input.";
    low_buffer_ = nullptr;
  }

  if (options_.buffer->Read(buffer_variables::vm_exec)) {
    LOG(INFO) << options_.buffer->Read(buffer_variables::name)
              << ": Evaluating VM code: " << buffer_wrapper->ToString();
    options_.buffer->EvaluateString(buffer_wrapper->ToString(),
                                    [](std::unique_ptr<Value>) {});
  }

  clock_gettime(0, &last_input_received_);
  options_.buffer->RegisterProgress();
  bool previous_modified = options_.buffer->modified();
  if (options_.terminal != nullptr) {
    options_.terminal->ProcessCommandInput(buffer_wrapper, [this]() {
      lines_read_rate_.IncrementAndGetEventsPerSecond(1.0);
    });
    editor_state->ScheduleRedraw();
  } else {
    auto follower = options_.buffer->GetEndPositionFollower();
    size_t line_start = 0;
    for (size_t i = 0; i < buffer_wrapper->size(); i++) {
      if (buffer_wrapper->get(i) == '\n') {
        VLOG(8) << "Adding line from " << line_start << " to " << i;

        Line::Options options;
        options.contents =
            Substring(buffer_wrapper, line_start, i - line_start);
        options.modifiers[ColumnNumber(0)] = options_.modifiers;
        options_.buffer->AppendToLastLine(Line(std::move(options)));

        lines_read_rate_.IncrementAndGetEventsPerSecond(1.0);
        options_.start_new_line();
        line_start = i + 1;
        auto buffer = editor_state->current_buffer();
        if (buffer.get() == options_.buffer) {
          // TODO: Only do this if the position is in view in any of the
          // buffers.
          editor_state->ScheduleRedraw();
        }
      }
    }
    if (line_start < buffer_wrapper->size()) {
      VLOG(8) << "Adding last line from " << line_start << " to "
              << buffer_wrapper->size();

      Line::Options options;
      options.contents = Substring(buffer_wrapper, line_start);
      options.modifiers[ColumnNumber(0)] = options_.modifiers;
      options_.buffer->AppendToLastLine(Line(std::move(options)));
    }
  }
  if (!previous_modified) {
    options_.buffer->ClearModified();  // These changes don't count.
  }
  auto current_buffer = editor_state->current_buffer();
  if (current_buffer != nullptr &&
      current_buffer->Read(buffer_variables::name) ==
          OpenBuffer::kBuffersName) {
    current_buffer->Reload();
  }
  editor_state->ScheduleRedraw();
  return ReadResult::kContinue;
}

}  // namespace editor
}  // namespace afc
