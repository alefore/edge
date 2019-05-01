#include "src/shapes.h"

#include <glog/logging.h>

#include <set>

#include "src/line_column.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/set.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

namespace afc {
namespace editor {

using namespace afc::vm;

std::vector<wstring> Justify(std::vector<wstring> input, int width) {
  LOG(INFO) << "Evaluating breaks with inputs: " << input.size();

  // Push back a dummy string for the end. This is the goal of our graph search.
  input.push_back(L"*");

  // At position i, contains the best solution to reach word i. The values are
  // the cost of the solution, and the solution itself.
  std::vector<std::tuple<int, std::vector<int>>> options(input.size());

  for (size_t i = 0; i < input.size(); i++) {
    if (i > 0 && std::get<1>(options[i]).empty()) {
      continue;
    }
    // Consider doing the next break (after word i) at word next.
    int length = input[i].size();
    for (size_t next = i + 1; next < input.size(); next++) {
      if (length > width) {
        continue;  // Line was too long, this won't work.
      }
      int cost = width - length;
      cost = cost * cost;
      cost += std::get<0>(options[i]);
      if (std::get<1>(options[next]).empty() ||
          std::get<0>(options[next]) >= cost) {
        std::get<0>(options[next]) = cost;
        std::get<1>(options[next]) = std::get<1>(options[i]);
        std::get<1>(options[next]).push_back(next);
      }
      length += 1 + input[next].size();
    }
  }
  std::vector<wstring> output;
  auto route = std::get<1>(options.back());
  for (size_t line = 0; line < route.size(); line++) {
    size_t previous_word = line == 0 ? 0 : route[line - 1];
    wstring output_line;
    for (int word = previous_word; word < route[line]; word++) {
      output_line += (output_line.empty() ? L"" : L" ") + input[word];
    }
    output.push_back(output_line);
  }
  LOG(INFO) << "Returning breaks: " << output.size() << ", cost "
            << std::get<0>(options.back());
  return output;
}

// output_right contains LineColumn(i, j) if there's a line cross into
// LineColumn(i, j + 1). output_down if there's a line crossing into
// LineColumn(i + 1, j).
void FindBoundariesLine(LineColumn start, LineColumn end,
                        std::set<LineColumn>* output_right,
                        std::set<LineColumn>* output_down) {
  if (start.column > end.column) {
    LineColumn tmp = start;
    start = end;
    end = tmp;
  }
  double delta_x = static_cast<double>((end.column - start.column).value);
  double delta_y = static_cast<double>(end.line - start.line);
  double delta_error = delta_x == 0.0
                           ? delta_y * std::numeric_limits<double>::max()
                           : delta_y / delta_x;
  double error = delta_error / 2.0;
  LOG(INFO) << "delta_error " << delta_error << " from " << delta_x << " and "
            << delta_y;
  while (start.column < end.column ||
         (delta_error >= 0 ? start.line < end.line : start.line > end.line)) {
    if (error > 0.5) {
      error -= 1.0;
      output_down->insert(start);
      start.line++;
    } else if (error < -0.5) {
      error += 1.0;
      start.line--;
      output_down->insert(start);
    } else {
      error += delta_error;
      output_right->insert(start);
      start.column++;
    }
  }
}

void InitShapes(vm::Environment* environment) {
  environment->Define(
      L"ShapesReflow",
      Value::NewFunction(
          {VMType::ObjectType(L"VectorString"),
           VMType::ObjectType(L"VectorString"), VMType::Integer()},
          [](std::vector<Value::Ptr> args) {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
            auto input =
                static_cast<std::vector<wstring>*>(args[0]->user_value.get());
            CHECK(input != nullptr);
            return Value::NewObject(L"VectorString",
                                    std::make_shared<std::vector<wstring>>(
                                        Justify(*input, args[1]->integer)));
          }));
  environment->Define(
      L"FindBoundariesLine",
      vm::NewCallback(
          std::function<void(LineColumn, LineColumn, std::set<LineColumn>*,
                             std::set<LineColumn>*)>(&FindBoundariesLine)));
}

}  // namespace editor
}  // namespace afc
