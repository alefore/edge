#ifndef __AFC_EDITOR_CURSORS_TRANSFORMATION_H__
#define __AFC_EDITOR_CURSORS_TRANSFORMATION_H__

#include "src/futures/futures.h"
#include "src/infrastructure/screen/cursors.h"
#include "src/language/text/line_column.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"

namespace afc::editor::transformation {
struct Cursors {
  infrastructure::screen::CursorsSet cursors;
  language::text::LineColumn active;
};

futures::Value<Result> ApplyBase(const Cursors& parameters, Input input);
std::wstring ToStringBase(const Cursors& v);
Cursors OptimizeBase(Cursors cursors);
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_CURSORS_TRANSFORMATION_H__
