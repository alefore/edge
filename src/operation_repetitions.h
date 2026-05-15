#pragma once

#include <list>

#include "src/key_commands_map.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/structure.h"
#include "src/transformation_composite.h"

namespace afc::editor::operation::commands {
const Description& MoveLeftDescription();
const Description& MoveRightDescription();

class Repetitions {
  struct Entry {
    int additive = 0;
    int additive_default = 0;
    int multiplicative = 0;
    int multiplicative_sign;
  };
  std::list<Entry> entries_;

 public:
  Repetitions(int repetitions);

  language::lazy_string::SingleLine ToString() const;
  // Returns the total sum of all entries.
  int get() const;
  std::list<int> get_list() const;
  void sum(int value);
  void factor(int value);

  bool empty() const;
  bool PopValue();

  transformation::Stack Apply(
      std::optional<Structure> structure,
      language::NonNull<std::shared_ptr<CompositeTransformation>>
          inner_transformation) const;

  void ExtendKeyCommandsMap(KeyCommandsMap& cmap);
  // Add bindings for `h` and `l` (and left/right arrows) to sum(1) or sum(-1).
  void LeftRightKeyCommandsMap(KeyCommandsMap& cmap);

 private:
  static int Flatten(const Entry& entry);
};
}  // namespace afc::editor::operation::commands
