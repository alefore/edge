#include <string>

#include "token.h"

namespace afc {
namespace editor {

void Token::AppendToString(std::string* output) const {
  // CHECK(output);
  output->append(root_);
  for (const auto& token : sub_tokens_) {
    token->AppendToString(output);
  }
}

void Token::AppendToken(std::unique_ptr<Token> token) {
  sub_tokens_.emplace_back(std::move(token));
}

}  // namespace editor
}  // namespace afc
