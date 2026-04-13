// Receives a specification of a path to open and turns it into a set of
// possible paths and post-open jumps.
//
// Does not handle search-paths nor resolving.
//
// For example, "foo/bar:40:2" can be (among others): jump to Line 39, Column 1.
//
// See tests in implementation for more details.

#ifndef __AFC_EDITOR_OPEN_FILE_POSITION_H__
#define __AFC_EDITOR_OPEN_FILE_POSITION_H__

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"

namespace afc::editor::open_file_position {
struct Default {};

bool operator==(const Default& a, const Default& b);

std::ostream& operator<<(std::ostream& os, const Default&);

class Search
    : public language::GhostType<Search,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;
};

using Spec = std::variant<Default, Search, language::text::LineColumn>;

std::ostream& operator<<(std::ostream& os, const Spec& spec);

// `path_suffix` will be a string like `:93`.
enum class SuffixMode { Allow, Disallow };
std::optional<Spec> Parse(language::lazy_string::LazyString path_suffix,
                          SuffixMode suffix_mode);

language::text::LineMetadataMap GetLineMetadata(Spec spec);
Spec SpecFromLineMetadata(const language::text::LineMetadataMap& values);

}  // namespace afc::editor::open_file_position

#endif  // __AFC_EDITOR_OPEN_FILE_POSITION_H__