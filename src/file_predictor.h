#ifndef __AFC_EDITOR_FILE_PREDICTOR_H__
#define __AFC_EDITOR_FILE_PREDICTOR_H__

#include <functional>

#include "src/futures/futures.h"
#include "src/predictor.h"

namespace afc::editor {
enum class FilePredictorMatchBehavior { kOnlyExactMatch, kIncludePartialMatch };

struct FilePredictorOptions {
  FilePredictorMatchBehavior match_behavior =
      FilePredictorMatchBehavior::kIncludePartialMatch;
};

std::function<futures::Value<PredictorOutput>(PredictorInput input)>
GetFilePredictor(FilePredictorOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FILE_PREDICTOR_H__
