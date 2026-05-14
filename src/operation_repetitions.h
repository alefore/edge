#pragma once

#include <list>

#include "src/language/lazy_string/single_line.h"

namespace afc::editor::operation::commands {
class Repetitions {
  struct Entry {
    int additive = 0;
    int additive_default = 0;
    int multiplicative = 0;
    int multiplicative_sign;
  };
  std::list<Entry> entries_;

 public:
  Repetitions(int repetitions)
      : entries_({{.additive_default = repetitions,
                   .multiplicative_sign = repetitions >= 0 ? 1 : -1}}) {}

  language::lazy_string::SingleLine ToString() const;
  // Returns the total sum of all entries.
  int get() const;
  std::list<int> get_list() const;
  void sum(int value);
  void factor(int value);

  bool empty() const;
  bool PopValue();

 private:
  static int Flatten(const Entry& entry);
};
}  // namespace afc::editor::operation::commands
