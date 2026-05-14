#include "src/operation_repetitions.h"

#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/trim.h"

using namespace afc::language::container;
using namespace afc::language::lazy_string;

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

/* static */ int Repetitions::Flatten(const Entry& entry) {
  return entry.additive_default + entry.additive + entry.multiplicative;
}
}  // namespace afc::editor::operation::commands
