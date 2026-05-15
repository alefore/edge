#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_builder.h"
#include "src/operation_bisect.h"
#include "src/transformation_paste.h"

using namespace afc::language::lazy_string;
using namespace afc::language::text;
using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor::operation::commands {
const Description& PasteDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"📎");
  return output;
}

namespace {
class Impl : public MoveOperationCommand {
  commands::Repetitions repetitions_ = {1};
  std::vector<language::lazy_string::LazyString> queries_;
  std::optional<language::lazy_string::SingleLine> query_input_;

 public:
  LineBuilder status() const override {
    LineBuilder output;
    commands::SerializeCall(
        PasteDescription().read(),
        std::vector<SingleLine>{repetitions_.ToString(),
                                query_input_.has_value()
                                    ? SINGLE_LINE_CONSTANT(L"\"") +
                                          query_input_.value() +
                                          SINGLE_LINE_CONSTANT(L"\"")
                                    : SingleLine{}},
        output);
    return output;
  }

  transformation::Variant GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>&,
      transformation::Stack&) const override {
    return repetitions_.Apply(
        std::nullopt,
        MakeNonNullShared<transformation::Paste>(transformation::Paste{
            FindFragmentQuery{.filter = query_input_.value_or(SingleLine{})}}));
  }

  KeyCommandsMap key_commands_map(Receiver) override {
    KeyCommandsMap cmap;
    if (query_input_.has_value()) {
      cmap.SetFallback(
          {'\n', ControlChar::Escape}, [this](ExtendedChar extended_c) {
            std::visit(
                overload{
                    [this](ControlChar c) {
                      switch (c) {
                        case ControlChar::Backspace:
                          if (query_input_->empty())
                            query_input_ = std::nullopt;
                          else
                            query_input_ = query_input_->Substring(
                                ColumnNumber{},
                                query_input_->size() - ColumnNumberDelta{1});
                          // TODO(trivial, 2024-09-16): Handle more
                          // control characters.
                        default:
                          break;
                      }
                    },
                    [&](wchar_t c) {
                      CHECK(c !=
                            L'\n');  // Exempted above (in cmap.SetFallback).
                      query_input_ =
                          query_input_.value() +
                          SingleLine{LazyString{ColumnNumberDelta{1}, c}};
                    }},
                extended_c);
          });
      return cmap;
    }
    repetitions_.ExtendKeyCommandsMap(cmap);
    cmap.Insert(L'p',
                {.category = KeyCommandsMap::Category::Repetitions,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Paste")},
                 .handler = [this](ExtendedChar) { repetitions_.sum(1); }})
        .Insert(L'f',
                {.category = KeyCommandsMap::Category::StringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Filter")},
                 .handler = [this](ExtendedChar) {
                   CHECK(!query_input_.has_value());
                   query_input_ = SingleLine{};
                 }});
    return cmap;
  }

  Repetitions* repetitions() override { return &repetitions_; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> Paste() {
  return MakeNonNullShared<Impl>();
}
}  // namespace afc::editor::operation::commands
