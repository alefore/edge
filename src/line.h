#ifndef __AFC_EDITOR_LINE_H__
#define __AFC_EDITOR_LINE_H__

#include <cassert>
#include <map>
#include <memory>
#include <unordered_set>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "lazy_string.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class EditorMode;
class EditorState;
class LazyString;
class OpenBuffer;

using std::hash;
using std::shared_ptr;
using std::string;
using std::wstring;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

class Line {
 public:
  enum Modifier {
    RESET,
    BOLD,
    DIM,
    UNDERLINE,
    REVERSE,
    BLACK,
    RED,
    GREEN,
    BLUE,
    CYAN,
    YELLOW,
    MAGENTA,
    BG_RED,
  };

  static string ModifierToString(Modifier modifier) {
    switch (modifier) {
      case RESET: return "RESET";
      case BOLD: return "BOLD";
      case DIM: return "DIM";
      case UNDERLINE: return "UNDERLINE";
      case REVERSE: return "REVERSE";
      case BLACK: return "BLACK";
      case RED: return "RED";
      case GREEN: return "GREEN";
      case BLUE: return "BLUE";
      case CYAN: return "CYAN";
      case YELLOW: return "YELLOW";
      case MAGENTA: return "MAGENTA";
      case BG_RED: return "BG_RED";
    }
    return "UNKNOWN";
  }

  static Modifier ModifierFromString(string modifier) {
    // TODO: Turn into a map.
    if (modifier == "RESET") return RESET;
    if (modifier == "BOLD") return BOLD;
    if (modifier == "DIM") return DIM;
    if (modifier == "UNDERLINE") return UNDERLINE;
    if (modifier == "REVERSE") return REVERSE;
    if (modifier == "BLACK") return BLACK;
    if (modifier == "RED") return RED;
    if (modifier == "GREEN") return GREEN;
    if (modifier == "BLUE") return BLUE;
    if (modifier == "CYAN") return CYAN;
    if (modifier == "YELLOW") return YELLOW;
    if (modifier == "MAGENTA") return MAGENTA;
    if (modifier == "BG_RED") return BG_RED;
    return RESET;  // Ugh.
  }

  struct Options {
    Options() : contents(EmptyString()) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(std::move(input_contents)),
          modifiers(contents->size()) {}

    void AppendFromLine(const Line& line);

    shared_ptr<LazyString> contents;
    vector<unordered_set<Modifier, hash<int>>> modifiers;
    std::shared_ptr<vm::Environment> environment = nullptr;
  };

  Line(const Options& options);
  Line(const Line& line) = default;

  shared_ptr<LazyString> contents() const { return contents_; }
  size_t size() const {
    CHECK(contents_ != nullptr);
    return contents_->size();
  }
  bool empty() const {
    CHECK(contents_ != nullptr);
    return size() == 0;
  }
  wint_t get(size_t column) const {
    CHECK_LT(column, contents_->size());
    return contents_->get(column);
  }
  shared_ptr<LazyString> Substring(size_t pos, size_t length) const;
  // Returns the substring from pos to the end of the string.
  shared_ptr<LazyString> Substring(size_t pos) const;
  wstring ToString() const {
    return contents_->ToString();
  }
  // Delete characters in [position, position + amount).
  void DeleteCharacters(size_t position, size_t amount);
  void InsertCharacterAtPosition(size_t position);
  void SetCharacter(size_t position, int c,
                    const std::unordered_set<Modifier, hash<int>>& modifiers);

  const vector<unordered_set<Modifier, hash<int>>> modifiers() const {
    return modifiers_;
  }

  bool modified() const { return modified_; }
  void set_modified(bool modified) { modified_ = modified; }

  std::shared_ptr<vm::Environment> environment() const;

  bool filtered() const {
    return filtered_;
  }
  bool filter_version() const {
    return filter_version_;
  }
  void set_filtered(bool filtered, size_t filter_version) {
    filtered_ = filtered;
    filter_version_ = filter_version;
  }

  class OutputReceiverInterface {
   public:
    virtual ~OutputReceiverInterface() {}

    virtual void AddCharacter(wchar_t character) = 0;
    virtual void AddString(const wstring& str) = 0;
    virtual void AddModifier(Modifier modifier) = 0;
  };

  void Output(const EditorState* editor_state,
              const shared_ptr<OpenBuffer>& buffer,
              size_t line,
              OutputReceiverInterface* receiver,
              size_t width) const;

 private:
  std::shared_ptr<vm::Environment> environment_;
  shared_ptr<LazyString> contents_;
  vector<unordered_set<Modifier, hash<int>>> modifiers_;
  bool modified_;
  bool filtered_;
  size_t filter_version_;
};

// Wrapper of a Line::OutputReceiverInterface that coallesces multiple calls to
// AddCharacter and/or AddString into as few calls (to the delegate) as
// possible.
class OutputReceiverOptimizer : public Line::OutputReceiverInterface {
 public:
  OutputReceiverOptimizer(OutputReceiverInterface* delegate)
      : delegate_(delegate) {
    DCHECK(delegate_ != nullptr);
  }

  ~OutputReceiverOptimizer() override;

  void AddCharacter(wchar_t character) override;
  void AddString(const wstring& str) override;
  void AddModifier(Line::Modifier modifier) override;

 private:
  void Flush();

  Line::OutputReceiverInterface* const delegate_;

  unordered_set<Line::Modifier, hash<int>> modifiers_;
  unordered_set<Line::Modifier, hash<int>> last_modifiers_;
  wstring buffer_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_H__
