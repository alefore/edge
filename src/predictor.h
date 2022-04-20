#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/buffer_name.h"
#include "src/concurrent/work_queue.h"
#include "src/futures/futures.h"
#include "src/status.h"
#include "src/structure.h"

namespace afc {
namespace editor {

using std::function;
using std::shared_ptr;
using std::vector;
using std::wstring;

class EditorState;
class OpenBuffer;
class Notification;

struct ProgressInformation {
  std::map<StatusPromptExtraInformationKey, std::wstring> values = {};

  // Similar to `values` but will be added in the case where multiple values are
  // reported from predictions that run independently (whereas for values the
  // last value reported triumps all others).
  //
  // This is useful, in particular, when searching with `multiple_buffers`
  // enabled; the search will be performed in each buffer and the results will
  // be aggregated.
  std::map<StatusPromptExtraInformationKey, size_t> counters = {};
};

using ProgressChannel = WorkQueueChannel<ProgressInformation>;

// A Predictor is a function that generates predictions (autocompletions) for a
// given prompt input and writes them to a buffer.
struct PredictorInput {
  EditorState& editor;
  std::wstring input;

  // The output buffer to write predictions to.
  //
  // It begins with nothing but an empty line. As such, you probably don't want
  // to use OpenBuffer::AppendLine (because that would leave an empty line at
  // the beggining) but, instead, OpenBuffer::AppendToLastLine, followed by
  // OpenBuffer::AppendRawLine(..., empty_line). In other words, once the
  // predictor is done running, the buffer must have an empty line at the end
  // (and not at the beginning).
  OpenBuffer* predictions = nullptr;

  // If the completion is specific to a given buffer (as opposed to in a global
  // status, with no corresponding buffer), this will be pointing to the buffer.
  // That allows the predictor to inspect the buffer contents (e.g., searching
  // in the buffer) or variables (e.g., honoring variables in the buffer
  // affecting the prediction).
  //
  // TODO: Mark the buffers as const. Unfortunately, the search handler wants to
  // modify them.
  std::vector<std::shared_ptr<OpenBuffer>> source_buffers;

  ProgressChannel& progress_channel;

  // Will never be nullptr: Predict ensures that.
  std::shared_ptr<Notification> abort_notification;
};

struct PredictorOutput {};

using Predictor =
    std::function<futures::Value<PredictorOutput>(PredictorInput)>;

const BufferName& PredictionsBufferName();

struct PredictResults {
  // If the input matched at least one item, this will be the longest common
  // prefix of all the items that the input matched.
  std::optional<wstring> common_prefix;

  // The buffer holding all the predictions.
  std::shared_ptr<OpenBuffer> predictions_buffer;

  int matches = 0;

  // The size of the longest prefix in the input that matched at least one item.
  // Typically, when the input matches at least one item, this will be the size
  // of the input.
  ColumnNumberDelta longest_prefix;
  // The size of the longest prefix in the input that matched a directory and
  // that is shorter than the entire input (i.e., if the input is `foo/bar` and
  // that directory exists, the longest directory will be `foo`).
  ColumnNumberDelta longest_directory_match;

  // Did the input match a file exactly?
  bool found_exact_match = false;
};

std::ostream& operator<<(std::ostream& os, const PredictResults& lc);

struct PredictOptions {
  EditorState& editor_state;
  Predictor predictor;

  // The text to use for the prediction. If not set, it'll be extracted from
  // `input_buffer` based on `input_selection_structure`.
  std::optional<std::wstring> text = {};

  // The buffer that contains the input to use for the prediction. Only read if
  // `text` is absent.
  std::shared_ptr<OpenBuffer> input_buffer = nullptr;
  Structure* input_selection_structure = StructureLine();

  // Given to the predictor (see `PredictorInput::source_buffers`). The caller
  // must ensure it doesn't get deallocated until the future returned by the
  // predictor is done running.
  //
  // TODO: Mark the buffers as const. See comments in `PredictorInput`.
  std::vector<std::shared_ptr<OpenBuffer>> source_buffers;

  // Can be null, in which case Predict will use a dummy no-op channel.
  std::unique_ptr<WorkQueueChannel<ProgressInformation>> progress_channel =
      nullptr;

  // Notification that the caller can use to signal that it wants to stop the
  // prediction (without waiting for it to complete).
  //
  // Can be null, in which case Predict will create a notification (that never
  // gets notified).
  std::shared_ptr<Notification> abort_notification = nullptr;
};

// Create a new buffer running a given predictor on the input in a given status
// prompt. When that's done, notifies the returned future.
//
// The vaue will be absent if the prediction finished when it was too late (for
// example, because the query has changed in the meantime).
futures::Value<std::optional<PredictResults>> Predict(
    PredictOptions predict_options);

futures::Value<PredictorOutput> FilePredictor(PredictorInput input);

futures::Value<PredictorOutput> EmptyPredictor(PredictorInput input);

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator);

Predictor DictionaryPredictor(std::shared_ptr<const OpenBuffer> dictionary);

// Based on the parse tree of the source_buffer.
futures::Value<PredictorOutput> SyntaxBasedPredictor(PredictorInput input);

}  // namespace editor
}  // namespace afc

#endif
