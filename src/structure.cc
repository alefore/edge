#include "structure.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

Structure LowerStructure(Structure s) {
  switch (s) {
    case CHAR:
      return CHAR;
    case SYMBOL:
      return CHAR;
    case LINE:
      return SYMBOL;
    case MARK:
      return LINE;
    case PAGE:
      return MARK;
    case SEARCH:
      return PAGE;
    case CURSOR:
      return SEARCH;
    case BUFFER:
      return CURSOR;
    case TREE:
      return TREE;
  }
  CHECK(false) << "Structure type not handled by LowerStructure.";
}

}  // namespace editor
}  // namespace afc
