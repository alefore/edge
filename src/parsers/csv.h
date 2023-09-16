#ifndef __AFC_EDITOR_PARSERS_CSV_H__
#define __AFC_EDITOR_PARSERS_CSV_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/parse_tree.h"

namespace afc::editor::parsers {
language::NonNull<std::unique_ptr<TreeParser>> NewCsvTreeParser();
}  // namespace afc::editor::parsers

#endif  // __AFC_EDITOR_PARSERS_CSV_H__
