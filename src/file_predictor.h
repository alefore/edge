#ifndef __AFC_EDITOR_FILE_PREDICTOR_H__
#define __AFC_EDITOR_FILE_PREDICTOR_H__

#include <functional>

#include "src/futures/futures.h"
#include "src/open_file_position.h"
#include "src/predictor.h"

namespace afc::editor {
enum class FilePredictorMatchBehavior { kOnlyExactMatch, kIncludePartialMatch };

enum class FilePredictorOutputFormat {
  SearchPathAndInput,
  Input,
};

struct FilePredictorOptions {
  FilePredictorMatchBehavior match_behavior =
      FilePredictorMatchBehavior::kIncludePartialMatch;

  open_file_position::SuffixMode open_file_position_suffix_mode =
      open_file_position::SuffixMode::Disallow;

  enum class Filter { Include, Exclude };
  Filter directory_filter = Filter::Include;
  Filter special_file_filter = Filter::Include;

  FilePredictorOutputFormat output_format = FilePredictorOutputFormat::Input;
};

std::function<futures::Value<PredictorOutput>(PredictorInput input)>
GetFilePredictor(FilePredictorOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FILE_PREDICTOR_H__
