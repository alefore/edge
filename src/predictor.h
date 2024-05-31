#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/buffer_name.h"
#include "src/concurrent/version_property_receiver.h"
#include "src/concurrent/work_queue.h"
#include "src/futures/delete_notification.h"
#include "src/futures/futures.h"
#include "src/language/safe_types.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/status.h"

namespace afc {
namespace editor {

class EditorState;
class OpenBuffer;

struct ProgressInformation {
  std::map<concurrent::VersionPropertyKey, std::wstring> values = {};

  // Similar to `values` but will be added in the case where multiple values are
  // reported from predictions that run independently (whereas for values the
  // last value reported triumps all others).
  //
  // This is useful, in particular, when searching with `multiple_buffers`
  // enabled; the search will be performed in each buffer and the results will
  // be aggregated.
  std::map<concurrent::VersionPropertyKey, size_t> counters = {};
};

using ProgressChannel = concurrent::Channel<ProgressInformation>;

// A Predictor is a function that generates predictions (autocompletions) for a
// given prompt input and writes them to a buffer.
struct PredictorInput {
  EditorState& editor;

  // Input for the prediction.
  language::lazy_string::LazyString input;

  // If the input comes from a prompt, position of the current cursor. This will
  // be used in the future for predictors that expand only a single token.
  language::lazy_string::ColumnNumber input_column;

  // If the completion is specific to a given buffer (as opposed to in a global
  // status, with no corresponding buffer), this will be pointing to the buffer.
  // That allows the predictor to inspect the buffer contents (e.g., searching
  // in the buffer) or variables (e.g., honoring variables in the buffer
  // affecting the prediction).
  //
  // TODO: Mark the buffers as const. Unfortunately, the search handler wants to
  // modify them.
  std::vector<language::gc::Root<OpenBuffer>> source_buffers;

  // Can be null, in which case Predict will use a dummy no-op channel.
  language::NonNull<std::shared_ptr<ProgressChannel>> progress_channel =
      language::MakeNonNullShared<
          afc::concurrent::ChannelAll<ProgressInformation>>(
          [](ProgressInformation) {});

  // Notification that the caller can use to signal that it wants to stop the
  // prediction (without waiting for it to complete).
  futures::DeleteNotification::Value abort_value =
      futures::DeleteNotification::Never();
};

struct PredictorOutput {
  // The size of the longest prefix in the input that matched at least one item.
  // Typically, when the input matches at least one item, this will be the size
  // of the input.
  language::lazy_string::ColumnNumberDelta longest_prefix =
      language::lazy_string::ColumnNumberDelta();

  // The size of the longest prefix in the input that matched a directory and
  // that is shorter than the entire input (i.e., if the input is `foo/bar` and
  // that directory exists, the longest directory will be `foo`).
  language::lazy_string::ColumnNumberDelta longest_directory_match =
      language::lazy_string::ColumnNumberDelta();

  // Did the input match a file exactly?
  bool found_exact_match = false;

  language::text::SortedLineSequenceUniqueLines contents =
      language::text::SortedLineSequenceUniqueLines(
          language::text::SortedLineSequence(language::text::LineSequence()));
};

std::ostream& operator<<(std::ostream& os, const PredictorOutput& lc);

using Predictor =
    std::function<futures::Value<PredictorOutput>(PredictorInput)>;

struct PredictResults {
  // If the input matched at least one item, this will be the longest common
  // prefix of all the items that the input matched.
  std::optional<std::wstring> common_prefix;

  // The buffer holding all the predictions.
  language::gc::Root<OpenBuffer> predictions_buffer;

  int matches = 0;

  PredictorOutput predictor_output;
};

std::ostream& operator<<(std::ostream& os, const PredictResults& lc);

// Create a new buffer running a given predictor on the input in a given status
// prompt. When that's done, notifies the returned future.
//
// The vaue will be absent if the prediction finished when it was too late (for
// example, because the query has changed in the meantime).
futures::Value<std::optional<PredictResults>> Predict(
    const Predictor& predictor, PredictorInput predict_options);

futures::Value<PredictorOutput> FilePredictor(PredictorInput input);

futures::Value<PredictorOutput> EmptyPredictor(PredictorInput input);

Predictor PrecomputedPredictor(const std::vector<std::wstring>& predictions,
                               wchar_t separator);

Predictor DictionaryPredictor(language::gc::Root<const OpenBuffer> dictionary);

// Based on the parse tree of the source_buffer.
futures::Value<PredictorOutput> SyntaxBasedPredictor(PredictorInput input);

Predictor ComposePredictors(Predictor, Predictor);

// Buffer must be a buffer given to a predictor by `Predict`. Registers a new
// size of a prefix that has a match.
void RegisterPredictorPrefixMatch(size_t new_value, OpenBuffer& buffer);

}  // namespace editor
}  // namespace afc

#endif
