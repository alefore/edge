#ifndef __AFC_EDITOR_LINE_PARSER_H__
#define __AFC_EDITOR_LINE_PARSER_H__

#include <memory>
#include <string>

namespace afc {
namespace editor {

class Token;

std::unique_ptr<Token> Parse(const std::string& contents);

}  // namespace editor
}  // namespace afc

#endif
