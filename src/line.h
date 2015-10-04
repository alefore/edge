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

namespace afc {
namespace editor {

class EditorMode;
class EditorState;
class LazyString;
class OpenBuffer;

using std::hash;
using std::shared_ptr;
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
    CYAN,
  };

  struct Options {
    Options() : contents(EmptyString()) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(input_contents) {}

    vector<unordered_set<Modifier, hash<int>>> modifiers;
    shared_ptr<LazyString> contents;
  };

  Line(const Options& options);

  shared_ptr<LazyString> contents() { return contents_; }
  size_t size() const { return contents_->size(); }
  wint_t get(size_t column) const {
    CHECK_LT(column, contents_->size());
    return contents_->get(column);
  }
  shared_ptr<LazyString> Substring(size_t pos, size_t length);
  shared_ptr<LazyString> Substring(size_t pos);
  wstring ToString() const {
    return contents_->ToString();
  }
  void DeleteUntilEnd(size_t position);
  void DeleteCharacters(size_t position, size_t amount);
  void InsertCharacterAtPosition(size_t position);
  void SetCharacter(size_t position, int c,
                    const std::unordered_set<Modifier, hash<int>>& modifiers);

  const vector<unordered_set<Modifier, hash<int>>> modifiers() const {
    return modifiers_;
  }

  bool modified() const { return modified_; }
  void set_modified(bool modified) { modified_ = modified; }

  EditorMode* activate() const { return activate_.get(); }
  void set_activate(unique_ptr<EditorMode> activate);

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
    virtual void AddCharacter(wchar_t character) = 0;
    virtual void AddString(const wstring& str) = 0;
    virtual void AddModifier(Modifier modifier) = 0;
    virtual size_t width() const = 0;
  };
  void Output(const EditorState* editor_state,
              const shared_ptr<OpenBuffer>& buffer,
              size_t line,
              OutputReceiverInterface* receiver);

 private:
  unique_ptr<EditorMode> activate_;
  shared_ptr<LazyString> contents_;
  vector<unordered_set<Modifier, hash<int>>> modifiers_;
  bool modified_;
  bool filtered_;
  size_t filter_version_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_H__
