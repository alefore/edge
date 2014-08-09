#ifndef __AFC_EDITOR_TOKEN_H__
#define __AFC_EDITOR_TOKEN_H__

#include <memory>
#include <list>
#include <string>

namespace afc {
namespace editor {

class Token {
 public:
  Token(const std::string& root) : root_(root) {}

  void AppendToString(std::string* output) const;
  void AppendToken(std::unique_ptr<Token> token);

 private:
  std::string root_;
  std::list<std::unique_ptr<Token>> sub_tokens_;
};

}  // namespace editor
}  // namespace afc

#endif
