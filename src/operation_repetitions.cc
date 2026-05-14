#include "src/operation_repetitions.h"

#include <glog/logging.h>

#include "src/infrastructure/extended_char.h"
#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/trim.h"
#include "src/operation_scope.h"
#include "src/tests/tests.h"

using namespace afc::infrastructure;
using namespace afc::language::container;
using namespace afc::language::lazy_string;
using namespace afc::language;

namespace afc::editor::operation::commands {
SingleLine Repetitions::ToString() const {
  return TrimLeft(
      Concatenate(get_list() | std::views::transform([](int r) -> SingleLine {
                    return (r > 0 ? SingleLine::Char<L'+'>() : SingleLine{}) +
                           NonEmptySingleLine(r);
                  })),
      {L'+'});
}

int Repetitions::get() const { return Sum(get_list()); }

std::list<int> Repetitions::get_list() const {
  return entries_ | std::views::transform(Flatten) |
         std::views::filter([](int c) { return c != 0; }) |
         std::ranges::to<std::list>();
}

void Repetitions::sum(int value) {
  if (entries_.empty() || (Flatten(entries_.back()) != 0 &&
                           Flatten(entries_.back()) >= 0) != (value >= 0)) {
    if (!entries_.empty()) {
      auto& entry_to_freeze = entries_.back();
      entry_to_freeze.additive +=
          entry_to_freeze.additive_default + entry_to_freeze.multiplicative;
      entry_to_freeze.additive_default = 0;
      entry_to_freeze.multiplicative = 0;
    }
    entries_.push_back({});  // Change of sign.
  }
  auto& last_entry = entries_.back();
  last_entry.additive +=
      value + last_entry.additive_default + last_entry.multiplicative;
  last_entry.additive_default = 0;
  last_entry.multiplicative = 0;
  last_entry.multiplicative_sign = value >= 0 ? 1 : -1;
}

void Repetitions::factor(int value) {
  if (entries_.empty() || entries_.back().multiplicative == 0) {
    entries_.push_back(
        {.multiplicative_sign =
             entries_.empty() || Flatten(entries_.back()) >= 0 ? 1 : -1});
  }
  auto& last_entry = entries_.back();
  last_entry.additive_default = 0;
  last_entry.multiplicative =
      last_entry.multiplicative * 10 + last_entry.multiplicative_sign * value;
}

bool Repetitions::empty() const { return entries_.empty(); }

bool Repetitions::PopValue() {
  if (entries_.empty()) return false;
  entries_.pop_back();
  return true;
}

Modifiers GetModifiers(std::optional<Structure> structure,
                       const commands::Repetitions& repetitions,
                       Direction direction) {
  int repetitions_int = repetitions.get();
  return Modifiers{.structure = structure.value_or(Structure::Char),
                   .direction = repetitions_int < 0
                                    ? ReverseDirection(direction)
                                    : direction,
                   .repetitions = abs(repetitions_int)};
}

transformation::Stack Repetitions::Apply(
    std::optional<Structure> structure,
    NonNull<std::shared_ptr<CompositeTransformation>> inner_transformation)
    const {
  transformation::Stack output;
  std::ranges::copy(
      get_list() | std::views::transform([&](int repetitions_value) {
        return transformation::ModifiersAndComposite{
            .modifiers =
                GetModifiers(structure, repetitions_value, Direction::Forwards),
            .transformation = inner_transformation};
      }),
      std::back_inserter(output));
  return output;
}

void Repetitions::ExtendKeyCommandsMap(KeyCommandsMap& cmap) {
  cmap.Insert(
      ControlChar::Backspace,
      {.category = KeyCommandsMap::Category::StringControl,
       .description =
           Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"PopRepetitions")},
       .active = !empty(),
       .handler = [this](ExtendedChar) { PopValue(); }});
  for (int i = 0; i < 10; i++)
    cmap.Insert(
        L'0' + i,
        {.category = KeyCommandsMap::Category::Repetitions,
         .description =
             Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Repetitions")},
         .handler = [this, i](ExtendedChar) { factor(i); }});
}

/* static */ int Repetitions::Flatten(const Entry& entry) {
  return entry.additive_default + entry.additive + entry.multiplicative;
}

namespace {
using ::operator<<;
bool apply_repetitions_test = tests::Register(
    L"operation::ApplyRepetitions",
    std::vector<tests::Test>(
        {{.name = L"Empty",
          .callback =
              [] {
                NonNull<std::shared_ptr<OperationScope>> operation_scope;
                LOG(INFO) << ToString(Repetitions(1).Apply(
                    Structure::Line, NewMoveTransformation(operation_scope)));
              }},
         {.name = L"LongRepetitionsList", .callback = [] {
            NonNull<std::shared_ptr<OperationScope>> operation_scope;
            commands::Repetitions repetitions(1);
            repetitions.sum(1);
            repetitions.sum(-1);
            repetitions.sum(1);
            repetitions.sum(-1);
            LOG(INFO) << ToString(repetitions.Apply(
                Structure::Line, NewMoveTransformation(operation_scope)));
          }}}));
}  // namespace
}  // namespace afc::editor::operation::commands
