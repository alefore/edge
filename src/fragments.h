#ifndef __AFC_EDITOR_FRAGMENTS_H__
#define __AFC_EDITOR_FRAGMENTS_H__

#include "src/buffer_filter.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
class EditorState;
class OpenBuffer;

void AddFragment(EditorState& editor, language::text::LineSequence fragment);

struct FindFragmentQuery {
  language::lazy_string::LazyString filter;
  size_t results_limit = 50;
};

// Returns up to query.results_limit entries. The output is sorted from the most
// to the least relevant entry.
futures::Value<std::vector<FilterSortBufferOutput::Match>> FindFragment(
    EditorState& editor, FindFragmentQuery query);

}  // namespace afc::editor
#endif
