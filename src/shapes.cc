#include "src/shapes.h"

#include <glog/logging.h>

#include <set>

#include "src/concurrent/protected.h"
#include "src/language/container.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_column_vm.h"
#include "src/vm/callbacks.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace container = afc::language::container;

using afc::concurrent::Protected;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::vm::kPurityTypeUnknown;

namespace afc ::editor {
futures::ValueOrError<
    NonNull<std::shared_ptr<Protected<std::vector<std::wstring>>>>>
Justify(
    NonNull<std::shared_ptr<Protected<std::vector<std::wstring>>>> input_ptr,
    int width) {
  return input_ptr->lock([&](std::vector<std::wstring>& input) {
    LOG(INFO) << "Evaluating breaks with inputs: " << input.size();

    // Push back a dummy string for the end. This is the goal of our graph
    // search.
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
    NonNull<std::shared_ptr<Protected<std::vector<std::wstring>>>>
        protected_output;
    protected_output->lock([&](std::vector<std::wstring> output) {
      auto route = std::get<1>(options.back());
      for (size_t line = 0; line < route.size(); line++) {
        size_t previous_word = line == 0 ? 0 : route[line - 1];
        std::wstring output_line;
        for (int word = previous_word; word < route[line]; word++) {
          output_line += (output_line.empty() ? L"" : L" ") + input[word];
        }
        output.push_back(output_line);
      }
      LOG(INFO) << "Returning breaks: " << output.size() << ", cost "
                << std::get<0>(options.back());
    });
    return futures::Past(Success(std::move(protected_output)));
  });
}

void LockAndInsert(
    NonNull<std::shared_ptr<Protected<std::set<LineColumn>>>> output,
    LineColumn value) {
  output->lock(
      [value](std::set<LineColumn>& container) { container.insert(value); });
}

// output_right contains LineColumn(i, j) if there's a line cross into
// LineColumn(i, j + 1). output_down if there's a line crossing into
// LineColumn(i + 1, j).
void FindBoundariesLine(
    LineColumn start, LineColumn end,
    NonNull<std::shared_ptr<Protected<std::set<LineColumn>>>> output_right,
    NonNull<std::shared_ptr<Protected<std::set<LineColumn>>>> output_down) {
  if (start.column > end.column) {
    LineColumn tmp = start;
    start = end;
    end = tmp;
  }
  double delta_x = static_cast<double>((end.column - start.column).read());
  double delta_y = static_cast<double>((end.line - start.line).read());
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
      LockAndInsert(output_down, start);
      start.line++;
    } else if (error < -0.5) {
      error += 1.0;
      start.line--;
      LockAndInsert(output_down, start);
    } else {
      error += delta_error;
      LockAndInsert(output_right, start);
      start.column++;
    }
  }
}

struct Point {
  static Point New(LineColumn position) {
    return Point{.x = static_cast<double>(position.column.read()),
                 .y = static_cast<double>(position.line.read())};
  }

  LineColumn ToLineColumn() {
    CHECK_GE(x, 0.0);
    CHECK_GE(y, 0.0);
    return LineColumn(LineNumber(static_cast<size_t>(y)),
                      ColumnNumber(static_cast<size_t>(x)));
  }

  double x = 0;
  double y = 0;
};

Point PointInLine(Point a, Point b, double delta) {
  CHECK_GE(delta, 0.0);
  CHECK_LE(delta, 1.0);
  return Point{.x = a.x * (1.0 - delta) + b.x * delta,
               .y = a.y * (1.0 - delta) + b.y * delta};
}

bool Adjacent(LineColumn a, LineColumn b) {
  return (a.line == b.line && (a.column == b.column + ColumnNumberDelta(1) ||
                               b.column == a.column + ColumnNumberDelta(1))) ||
         (a.column == b.column && (a.line == b.line + LineNumberDelta(1) ||
                                   b.line == a.line + LineNumberDelta(1)));
}

LineColumn EvaluateBezier(const std::vector<Point> points, double delta) {
  CHECK_GE(points.size(), 2ul);
  if (points.size() == 2ul) {
    return PointInLine(points[0], points[1], delta).ToLineColumn();
  }

  std::vector<Point> new_points;
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    new_points.push_back(PointInLine(points[i], points[i + 1], delta));
  }
  CHECK_EQ(new_points.size(), points.size() - 1);
  return EvaluateBezier(new_points, delta);
}

void InternalFindBoundariesBezier(const std::vector<Point> points, double start,
                                  double end, LineColumn start_position,
                                  LineColumn end_position,
                                  std::vector<LineColumn>* output) {
  if (start_position == end_position ||
      Adjacent(start_position, end_position)) {
    output->push_back(start_position);
    output->push_back(end_position);
    return;
  }
  LOG(INFO) << "Evaluating range: " << start << " (" << start_position
            << ") to " << end << " (" << end_position << "): ";
  double delta = (start + end) / 2;
  LineColumn position = EvaluateBezier(points, delta);
  InternalFindBoundariesBezier(points, start, delta, start_position, position,
                               output);
  LOG(INFO) << "At: " << delta << " found: " << position;
  output->push_back(position);
  InternalFindBoundariesBezier(points, delta, end, position, end_position,
                               output);
}

void FindBoundariesBezier(
    NonNull<std::shared_ptr<Protected<std::vector<LineColumn>>>> positions,
    NonNull<std::shared_ptr<Protected<std::set<LineColumn>>>> output_right,
    NonNull<std::shared_ptr<Protected<std::set<LineColumn>>>> output_down) {
  std::vector<Point> points = positions->lock([](std::vector<LineColumn> data) {
    return container::MaterializeVector(data |
                                        std::views::transform(Point::New));
  });

  if (points.size() < 2) {
    return;
  }

  std::vector<LineColumn> journey;
  InternalFindBoundariesBezier(points, 0.0, 1.0, points.front().ToLineColumn(),
                               points.back().ToLineColumn(), &journey);
  LineColumn last_point = points[0].ToLineColumn();
  for (auto& position : journey) {
    if (last_point == position) {
      continue;
    }
    LOG(INFO) << "Now: " << position;
    if (last_point.column != position.column) {
      LockAndInsert(output_right, last_point.column < position.column
                                      ? last_point
                                      : position);
    }
    if (last_point.line != position.line) {
      LockAndInsert(output_down,
                    last_point.line < position.line ? last_point : position);
    }
    last_point = position;
  }
}

void InitShapes(language::gc::Pool& pool, vm::Environment& environment) {
  using vm::Identifier;
  using vm::PurityType;
  environment.Define(Identifier{LazyString{L"ShapesReflow"}},
                     vm::NewCallback(pool, kPurityTypeUnknown, Justify));
  environment.Define(
      Identifier{LazyString{L"FindBoundariesLine"}},
      vm::NewCallback(pool, kPurityTypeUnknown, FindBoundariesLine));
  environment.Define(
      Identifier{LazyString{L"FindBoundariesBezier"}},
      vm::NewCallback(pool, kPurityTypeUnknown, FindBoundariesBezier));
}

}  // namespace afc::editor
