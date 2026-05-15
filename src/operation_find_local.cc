#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_builder.h"
#include "src/operation_bisect.h"
#include "src/transformation_reach_query.h"

using namespace afc::language::lazy_string;
using namespace afc::language::text;
using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor::operation::commands {
const Description& FindLocalDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🔮");
  return output;
}
namespace {

class Impl : public MoveOperationCommand {
  language::lazy_string::SingleLine query_;

 public:
  LineBuilder status() const override {
    LineBuilder output;
    commands::SerializeCall(
        FindLocalDescription().read(),
        {query_ + SingleLine::Padding<L'_'>(
                      ColumnNumberDelta(3) -
                      std::min(ColumnNumberDelta(3), query_.size()))},
        output);
    return output;
  }

  transformation::Variant GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>&,
      transformation::Stack&) const override {
    if (query_.empty()) return transformation::Stack{};
    return transformation::Stack{
        .stack = {MakeNonNullUnique<transformation::ReachQueryTransformation>(
            query_)}};
  }

  KeyCommandsMap key_commands_map(Receiver) override {
    KeyCommandsMap cmap;
    // TODO(2026-05-14, P2): Maybe exclude `?`?. Let that generate help.
    if (query_.size() < ColumnNumberDelta{3})
      cmap.SetFallback({L'\n', ControlChar::Escape, ControlChar::Backspace},
                       [this](ExtendedChar extended_c) {
                         std::visit(overload{[](ControlChar) {},
                                             [&](wchar_t c) {
                                               query_ += SingleLine{LazyString{
                                                   ColumnNumberDelta{1}, c}};
                                             }},
                                    extended_c);
                       });
    cmap.Insert(ControlChar::Backspace,
                {.category = KeyCommandsMap::Category::StringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Backspace")},
                 .active = !query_.empty(),
                 .handler = [this](ExtendedChar) {
                   query_ = query_.Substring(
                       ColumnNumber{}, query_.size() - ColumnNumberDelta{1});
                 }});
    return cmap;
  }

  Repetitions* repetitions() override { return nullptr; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> FindLocal() {
  return MakeNonNullShared<Impl>();
}
}  // namespace afc::editor::operation::commands
