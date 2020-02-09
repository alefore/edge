#include "src/tokenize.h"

namespace afc::editor {
std::vector<Token> TokenizeBySpaces(const LazyString& command) {
  std::vector<Token> output;
  Token token;
  auto push = [&](ColumnNumber end) {
    if (!token.value.empty()) {
      token.end = end;
      output.push_back(std::move(token));
    }
    token.value = L"";
    token.begin = ++end;
  };

  for (ColumnNumber i; i.ToDelta() < command.size(); ++i) {
    char c = command.get(i);
    if (c == ' ') {
      push(i);
    } else if (c == '\"') {
      ++i;
      while (i.ToDelta() < command.size() && command.get(i) != '\"') {
        token.value.push_back(command.get(i));
        ++i;
      }
    } else {
      token.value.push_back(c);
    }
  }
  push(ColumnNumber() + command.size());
  return output;
}
}  // namespace afc::editor
