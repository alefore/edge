#ifndef __AFC_EDITOR_SRC_PARSERS_LOG_H__
#define __AFC_EDITOR_SRC_PARSERS_LOG_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/log_model.h"
#include "src/parse_tree.h"

namespace afc::editor::parsers {
language::NonNull<std::unique_ptr<TreeParser>> NewLogTreeParser(
    LogType log_type, LogView log_view);
}

#endif  // __AFC_EDITOR_SRC_PARSERS_LOG_H__
