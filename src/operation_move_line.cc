#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_builder.h"
#include "src/operation_bisect.h"
#include "src/transformation_bisect.h"

using namespace afc::language::lazy_string;
using namespace afc::language::text;
using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor::operation::commands {
const Description& MoveUpDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🧗👆");
  return output;
}
const Description& MoveDownDescription() {
  static const Description output = NON_EMPTY_SINGLE_LINE_CONSTANT(L"🧗👇");
  return output;
}

namespace {
class Impl : public MoveOperationCommand {
  Repetitions repetitions_ = {0};

 public:
  Impl(Repetitions repetitions) : repetitions_(std::move(repetitions)) {}

  LineBuilder status() const override {
    LineBuilder output;
    commands::SerializeCall(repetitions_.get() >= 0
                                ? MoveDownDescription().read()
                                : MoveUpDescription().read(),
                            {repetitions_.ToString()}, output);
    return output;
  }

  transformation::Stack GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
      transformation::Stack&) const override {
    return ApplyRepetitions(repetitions_, Structure::Line,
                            NewMoveTransformation(operation_scope));
  }

  KeyCommandsMap key_commands_map(Receiver push) override {
    KeyCommandsMap cmap;
    cmap.Insert(L'K', {.category = KeyCommandsMap::Category::NewCommand,
                       .description =
                           Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👆")},
                       .active = !repetitions_.empty() &&
                                 repetitions_.get_list().back() < 0,
                       .handler =
                           [push](ExtendedChar) {
                             push(Bisect(commands::BisectOptions{
                                 .structure = Structure::Line,
                                 .directions = {Direction::Backwards}}));
                           }})
        .Insert(L'J', {.category = KeyCommandsMap::Category::NewCommand,
                       .description =
                           Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👇")},
                       .active = !repetitions_.empty() &&
                                 repetitions_.get_list().back() > 0,
                       .handler = [push](ExtendedChar) {
                         push(Bisect(commands::BisectOptions{
                             .structure = Structure::Line,
                             .directions = {Direction::Forwards}}));
                       }});

    CheckRepetitionsChar(cmap, &repetitions_);
    cmap.Insert(L'j',
                {.category = KeyCommandsMap::Category::Repetitions,
                 .description = MoveDownDescription(),
                 .handler = [this](ExtendedChar) { repetitions_.sum(1); }})
        .Insert(ControlChar::DownArrow,
                {.category = KeyCommandsMap::Category::Repetitions,
                 .description = MoveDownDescription(),
                 .handler = [this](ExtendedChar) { repetitions_.sum(1); }})
        .Insert(L'k',
                {.category = KeyCommandsMap::Category::Repetitions,
                 .description = MoveUpDescription(),
                 .handler = [this](ExtendedChar) { repetitions_.sum(-1); }})
        .Insert(ControlChar::UpArrow,
                {.category = KeyCommandsMap::Category::Repetitions,
                 .description = MoveUpDescription(),
                 .handler = [this](ExtendedChar) { repetitions_.sum(-1); }});

    return cmap;
  }

  Repetitions* repetitions() override { return &repetitions_; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> MoveLine(
    Repetitions repetitions) {
  return MakeNonNullShared<Impl>(std::move(repetitions));
}
}  // namespace afc::editor::operation::commands
