#ifndef __AFC_EDITOR_LINE_H__
#define __AFC_EDITOR_LINE_H__

#include <cassert>
#include <memory>
#include <string>

#include "lazy_string.h"

namespace afc {
namespace editor {

class EditorMode;
class EditorState;
class LazyString;
class OpenBuffer;

using std::shared_ptr;
using std::string;
using std::unique_ptr;

class Line {
 public:
  struct Options {
    Options() : contents(EmptyString()), terminal(false) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(input_contents), terminal(false) {}
    shared_ptr<LazyString> contents;
    bool terminal;
  };

  Line(const Options& options);

  shared_ptr<LazyString> contents() { return contents_; }
  size_t size() const { return contents_->size(); }
  int get(size_t column) {
    assert(column < contents_->size());
    return contents_->get(column);
  }
  shared_ptr<LazyString> Substring(size_t pos, size_t length);
  shared_ptr<LazyString> Substring(size_t pos);
  string ToString() const {
    return contents_->ToString();
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
    virtual void AddCharacter(int character) = 0;
    virtual void AddString(const string& str) = 0;
    virtual size_t width() const = 0;
  };
  void Output(const EditorState* editor_state,
              const shared_ptr<OpenBuffer>& buffer,
              OutputReceiverInterface* receiver);

 private:
  unique_ptr<EditorMode> activate_;
  shared_ptr<LazyString> contents_;
  bool modified_;
  bool filtered_;
  size_t filter_version_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_H__
