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

namespace {

class Impl : public MoveOperationCommand {
  ValueWithDefault<Structure> structure_;
  Repetitions repetitions_;

 public:
  Impl(Structure structure, int repetitions)
      : structure_(structure), repetitions_(std::move(repetitions)) {}

  LineBuilder status() const override {
    LineBuilder output;
    SerializeCall(NON_EMPTY_SINGLE_LINE_CONSTANT(L"🦀"),
                  {commands::StructureToString(structure_.value()).read(),
                   repetitions_.ToString()},
                  output);
    return output;
  }

  transformation::Variant GetTransformation(
      const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
      transformation::Stack&) const override {
    return repetitions_.Apply(structure_.value(),
                              NewMoveTransformation(operation_scope));
  }

  KeyCommandsMap key_commands_map(Receiver push) override {
    KeyCommandsMap cmap;
    if (structure_ == Structure::Char && !repetitions_.empty()) {
      cmap.Insert(L'H', {.category = KeyCommandsMap::Category::NewCommand,
                         .description = commands::BisectLeftDescription(),
                         .active = repetitions_.get_list().back() < 0,
                         .handler =
                             [push](ExtendedChar) {
                               push(Bisect(commands::BisectOptions{
                                   .structure = Structure::Char,
                                   .directions = {Direction::Backwards}}));
                             }})
          .Insert(L'L', {.category = KeyCommandsMap::Category::NewCommand,
                         .description = commands::BisectRightDescription(),
                         .active = repetitions_.get_list().back() > 0,
                         .handler = [push](ExtendedChar) {
                           push(Bisect(commands::BisectOptions{
                               .structure = Structure::Char,
                               .directions = {Direction::Forwards}}));
                         }});
    }

    if (structure_.value() == Structure::Line && !repetitions_.empty()) {
      cmap.Insert(L'K', {.category = KeyCommandsMap::Category::NewCommand,
                         .description = commands::BisectUpDescription(),
                         .active = repetitions_.get_list().back() < 0,
                         .handler =
                             [push](ExtendedChar) {
                               push(Bisect(commands::BisectOptions{
                                   .structure = Structure::Line,
                                   .directions = {Direction::Backwards}}));
                             }})
          .Insert(L'J', {.category = KeyCommandsMap::Category::NewCommand,
                         .description = commands::BisectDownDescription(),
                         .active = repetitions_.get_list().back() > 0,
                         .handler = [push](ExtendedChar) {
                           push(Bisect(commands::BisectOptions{
                               .structure = Structure::Line,
                               .directions = {Direction::Forwards}}));
                         }});
    }

    AddSetStructureChar(cmap, structure_, repetitions_);
    repetitions_.LeftRightKeyCommandsMap(cmap);
    repetitions_.ExtendKeyCommandsMap(cmap);
    return cmap;
  }

  Repetitions* repetitions() override { return &repetitions_; }
};
}  // namespace
NonNull<std::shared_ptr<MoveOperationCommand>> Move(Structure structure,
                                                    int repetitions) {
  return MakeNonNullShared<Impl>(structure, repetitions);
}
}  // namespace afc::editor::operation::commands
