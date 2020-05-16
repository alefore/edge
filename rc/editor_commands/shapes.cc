#include "lib/numbers.cc"
#include "lib/strings.cc"

double total_columns = 80.0;
double total_lines = 25.0;

bool simple_characters = false;
bool delete_mode = false;
bool bold_mode = false;

LineColumn source = LineColumn(0, 0);
VectorLineColumn bezier_points = VectorLineColumn();

void ShapesSetStatus(string description) {
  SetStatus("Shapes: " + description);
}

void PadToLineColumn(Buffer buffer, LineColumn position) {
  buffer.ApplyTransformation(SetPositionTransformation(position));
  if (buffer.line_count() <= position.line()) {
    buffer.ApplyTransformation(
        InsertTransformationBuilder()
            .set_position(LineColumn(buffer.line_count(), 0))
            .set_text("\n" * (position.line() + 1 - buffer.line_count()))
            .build());
  }
  string line = buffer.line(position.line());
  int insertions = max(0, position.column() - line.size());
  buffer.ApplyTransformation(InsertTransformationBuilder()
                                 .set_position(position)
                                 .set_text(" " * insertions)
                                 .build());
}

void DrawPosition(Buffer buffer, LineColumn position, string text) {
  if (text.empty()) return;
  string line = buffer.line(position.line());
  PadToLineColumn(buffer, position);
  if (line.size() > position.column()) {
    buffer.ApplyTransformation(
        DeleteTransformationBuilder()
            .set_modifiers(Modifiers().set_repetitions(
                min(line.size() - position.column(), text.size())))
            .build());
  }
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text(text).build());
}

string GetCode(bool up, bool down, bool left, bool right, bool up_bold,
               bool down_bold, bool left_bold, bool right_bold) {
  if (simple_characters) {
    int count =
        (up ? 1 : 0) + (down ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
    if (!up && !down && !left && !right) {
      return " ";
    } else if (!up && !down) {
      return "-";
    } else if (!left && !right) {
      return "|";
    } else if (count == 2 && up && right) {
      return "`";
    } else if (count == 2 && down && right) {
      return ",";
    } else if (count == 2 && up && left) {
      return "´";
    } else if (count == 2 && down && left) {
      return ".";
    } else {
      return "+";
    }
  }

  if (up_bold) {
    if (down_bold) {
      if (left_bold) {
        if (right_bold) {
          return "╋";
        } else if (right) {
          return "╉";
        } else {
          return "┫";
        }
      } else if (left) {
        if (right_bold) {
          return "╊";
        } else if (right) {
          return "╂";
        } else {
          return "┨";
        }
      } else {
        if (right_bold) {
          return "┣";
        } else if (right) {
          return "┠";
        } else {
          return "┃";
        }
      }
    } else if (down) {
      if (left_bold) {
        if (right_bold) {
          return "╇";
        } else if (right) {
          return "╃";
        } else {
          return "┩";
        }
      } else if (left) {
        if (right_bold) {
          return "╄";
        } else if (right) {
          return "╀";
        } else {
          return "┦";
        }
      } else {
        if (right_bold) {
          return "┡";
        } else if (right) {
          return "┞";
        } else {
          return "╿";
        }
      }
    } else {
      if (left_bold) {
        if (right_bold) {
          return "┻";
        } else if (right) {
          return "┹";
        } else {
          return "┛";
        }
      } else if (left) {
        if (right_bold) {
          return "┺";
        } else if (right) {
          return "┸";
        } else {
          return "┚";
        }
      } else {
        if (right_bold) {
          return "┗";
        } else if (right) {
          return "┖";
        } else {
          return "╵";  // Best fit.
        }
      }
    }
  } else if (up) {
    if (down_bold) {
      if (left_bold) {
        if (right_bold) {
          return "╈";
        } else if (right) {
          return "╅";
        } else {
          return "┪";
        }
      } else if (left) {
        if (right_bold) {
          return "╆";
        } else if (right) {
          return "╁";
        } else {
          return "┧";
        }
      } else {
        if (right_bold) {
          return "┢";
        } else if (right) {
          return "┟";
        } else {
          return "╽";
        }
      }
    } else if (down) {
      if (left_bold) {
        if (right_bold) {
          return "┿";
        } else if (right) {
          return "┽";
        } else {
          return "┥";
        }
      } else if (left) {
        if (right_bold) {
          return "┾";
        } else if (right) {
          return "┼";
        } else {
          return "┤";
        }
      } else {
        if (right_bold) {
          return "┝";
        } else if (right) {
          return "├";
        } else {
          return "│";
        }
      }
    } else {
      if (left_bold) {
        if (right_bold) {
          return "┷";
        } else if (right) {
          return "┵";
        } else {
          return "┙";
        }
      } else if (left) {
        if (right_bold) {
          return "┶";
        } else if (right) {
          return "┴";
        } else {
          return "╯";
        }
      } else {
        if (right_bold) {
          return "┕";
        } else if (right) {
          return "╰";
        } else {
          return "╵";
        }
      }
    }
  } else {
    if (down_bold) {
      if (left_bold) {
        if (right_bold) {
          return "┳";
        } else if (right) {
          return "┱";
        } else {
          return "┓";
        }
      } else if (left) {
        if (right_bold) {
          return "┲";
        } else if (right) {
          return "┰";
        } else {
          return "┒";
        }
      } else {
        if (right_bold) {
          return "┏";
        } else if (right) {
          return "┎";
        } else {
          return "╷";  // Best fit.
        }
      }
    } else if (down) {
      if (left_bold) {
        if (right_bold) {
          return "┯";
        } else if (right) {
          return "┭";
        } else {
          return "┑";
        }
      } else if (left) {
        if (right_bold) {
          return "┮";
        } else if (right) {
          return "┬";
        } else {
          return "╮";
        }
      } else {
        if (right_bold) {
          return "┍";
        } else if (right) {
          return "╭";
        } else {
          return "╷";
        }
      }
    } else {
      if (left_bold) {
        if (right_bold) {
          return "━";
        } else if (right) {
          return "╾";
        } else {
          return "╶";  // Best fit.
        }
      } else if (left) {
        if (right_bold) {
          return "╼";
        } else if (right) {
          return "─";
        } else {
          return "╴";
        }
      } else {
        if (right_bold) {
          return "╶";  // Best fit.
        } else if (right) {
          return "╶";
        } else {
          return " ";
        }
      }
    }
  }
}

void GetLineColumnsToDraw(SetLineColumn right, SetLineColumn down,
                          SetLineColumn output) {
  for (int i = 0; i < right.size(); i++) {
    LineColumn position = right.get(i);
    output.insert(position);
    output.insert(LineColumn(position.line(), position.column() + 1));
  }

  for (int i = 0; i < down.size(); i++) {
    LineColumn position = down.get(i);
    output.insert(position);
    output.insert(LineColumn(position.line() + 1, position.column()));
  }
}

bool IsMovingLeft(string c) {
  return ("╴─-´╯.╮+┼┤┴┬╊╆╄╂╀╁┾┨┺┲┦┸┧┰┶┮┚┒╼").find(c, 0) != -1;
}

bool IsMovingLeftBold(string c) {
  return ("╋╉╇╈┿╅╃┽┫┻┳┹┩┪┱┷┯┥┵┭┛┓━┙┑╾").find(c, 0) != -1;
}

bool IsMovingUp(string c) {
  return ("│|`╰´╯+┼┤├┴╵╈┿╆╅╁┽┾┪┢┷┧┟┥┵┝┶╽┙┕").find(c, 0) != -1;
}

bool IsMovingUpBold(string c) {
  return ("╋╉╊╇╄╃╂╀┫┣┻┨┠┹┩┺┡┦┞┸┃┛┗╿┚┖").find(c, 0) != -1;
}

bool IsMovingRight(string c) {
  return ("╶─-`╰,╭+┼├┴┬╉╅╃╂╀╁┽┠┹┱┞┸┟┰┵┭┖┎╾").find(c, 0) != -1;
}

bool IsMovingRightBold(string c) {
  return ("╋╊╇╈┿╆╄┾┣┻┳┺┡┢┲┷┯┝┶┮┗┏┕┍╼━").find(c, 0) != -1;
}

bool IsMovingDown(string c) {
  return ("│|,╭.╮+┼┤├┬╷╇┿╄╃╀┽┾┩┡┯┦┞┥┭┝┮╿┑┍").find(c, 0) != -1;
}

bool IsMovingDownBold(string c) {
  return ("╋╉╊╈╆╅╂╁┫┣┳┨┠┪┱┢┲┧┟┰┃┓┏╽┒┎").find(c, 0) != -1;
}

void DrawLineColumns(Buffer buffer, SetLineColumn line_column_right,
                     SetLineColumn line_column_down, string code) {
  buffer.PushTransformationStack();
  SetLineColumn line_columns = SetLineColumn();
  GetLineColumnsToDraw(line_column_right, line_column_down, line_columns);
  ShapesSetStatus("Positions to draw: " + line_columns.size().tostring());
  for (int i = 0; i < line_columns.size(); i++) {
    LineColumn position = line_columns.get(i);
    string current_code = code;
    if (current_code.empty()) {
      string current_line = buffer.line(position.line());
      string current_char = position.column() < current_line.size()
                                ? current_line.substr(position.column(), 1)
                                : " ";

      bool left = IsMovingLeft(current_char);
      bool left_bold = IsMovingLeftBold(current_char);
      if (position.column() > 0 &&
          line_column_right.contains(
              LineColumn(position.line(), position.column() - 1))) {
        if (delete_mode) {
          left = false;
          left_bold = false;
        } else if (bold_mode) {
          left_bold = true;
        } else {
          left = true;
        }
      }

      bool up = IsMovingUp(current_char);
      bool up_bold = IsMovingUpBold(current_char);
      if (position.line() > 0 && line_column_down.contains(LineColumn(
                                     position.line() - 1, position.column()))) {
        if (delete_mode) {
          up = false;
          up_bold = false;
        } else if (bold_mode) {
          up_bold = true;
        } else {
          up = true;
        }
      }

      bool right = IsMovingRight(current_char);
      bool right_bold = IsMovingRightBold(current_char);
      if (line_column_right.contains(position)) {
        if (delete_mode) {
          right = false;
          right_bold = false;
        } else if (bold_mode) {
          right_bold = true;
        } else {
          right = true;
        }
      }

      bool down = IsMovingDown(current_char);
      bool down_bold = IsMovingDownBold(current_char);
      if (line_column_down.contains(position)) {
        if (delete_mode) {
          down = false;
          down_bold = false;
        } else if (bold_mode) {
          down_bold = true;
        } else {
          down = true;
        }
      }

      current_code = GetCode(up, down, left, right, up_bold, down_bold,
                             left_bold, right_bold);
    }
    DrawPosition(buffer, line_columns.get(i), current_code);
  }
  buffer.PopTransformationStack();
}

void FindBoundariesSquare(LineColumn start, LineColumn end,
                          SetLineColumn output_right,
                          SetLineColumn output_down) {
  FindBoundariesLine(start, LineColumn(start.line(), end.column()),
                     output_right, output_down);
  FindBoundariesLine(start, LineColumn(end.line(), start.column()),
                     output_right, output_down);
  FindBoundariesLine(end, LineColumn(start.line(), end.column()), output_right,
                     output_down);
  FindBoundariesLine(end, LineColumn(end.line(), start.column()), output_right,
                     output_down);
}

void ShapesAddSquareInPositions(Buffer buffer, LineColumn a, LineColumn b) {
  SetLineColumn output_right = SetLineColumn();
  SetLineColumn output_down = SetLineColumn();
  FindBoundariesSquare(a, b, output_right, output_down);
  DrawLineColumns(buffer, output_right, output_down, "");
}

void ShapesSquare() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    LineColumn position = buffer.position();
    ShapesAddSquareInPositions(buffer, position, source);
    buffer.ApplyTransformation(SetPositionTransformation(position));
    source = position;
  });
}

bool IsActualContent(Buffer buffer, string c) {
  string additional = "()";
  return buffer.symbol_characters().find(c, 0) != -1 ||
         additional.find(c, 0) != -1;
}

string TrimLine(Buffer buffer, string line) {
  int start = 0;
  while (start < line.size() &&
         !IsActualContent(buffer, line.substr(start, 1))) {
    start++;
  }
  if (start == line.size()) {
    return "";
  }
  int end = line.size() - 1;
  while (end > start && !IsActualContent(buffer, line.substr(end, 1))) {
    end = end - 1;
  }
  return line.substr(start, end - start + 1);
}

string GetSquareContents(Buffer buffer, LineColumn start, LineColumn end) {
  string output = "";
  while (start.line() <= end.line() && start.line() < buffer.line_count()) {
    string line = buffer.line(start.line());
    if (line.size() > start.column()) {
      string part = TrimLine(
          buffer,
          line.substr(start.column(),
                      min(end.column() + 1, line.size()) - start.column()));
      output = output + (!output.empty() && !part.empty() ? " " : "") + part;
    }
    start = LineColumn(start.line() + 1, start.column());
  }
  return output;
}

string JoinLines(VectorString v) {
  string output = "";
  for (int i = 0; i < v.size(); i++) {
    output = output + "[" + v.get(i) + "]";
  }
  return output;
}

string BuildPadding(int size, string c) {
  string output = "";
  for (int i = 0; i < size; i++) {
    output = output + c;
  }
  return output;
}

void ShapesSquareCenter() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    LineColumn a = buffer.position();
    LineColumn b = source;
    int border_delta = 1;
    LineColumn start = LineColumn(min(a.line(), b.line()) + border_delta,
                                  min(a.column(), b.column()) + border_delta);
    LineColumn end = LineColumn(max(a.line(), b.line()) - border_delta,
                                max(a.column(), b.column()) - border_delta);
    if (start.line() > end.line() || start.column() > end.column()) {
      ShapesSetStatus("Square is too small.");
      return;
    }
    int width = end.column() - start.column() + 1;
    VectorString contents =
        ShapesReflow(BreakWords(GetSquareContents(buffer, start, end)), width);
    int start_contents = (end.line() - start.line() + 1 - contents.size()) / 2;
    for (int i = 0; start.line() + i <= end.line(); i++) {
      string input = "";
      if (i >= start_contents && i - start_contents < contents.size()) {
        input = contents.get(i - start_contents);
        int padding = (width - input.size()) / 2;
        input = BuildPadding(padding, " ") + input +
                BuildPadding(width - padding - input.size(), " ");
      } else {
        input = BuildPadding(width, " ");
      }
      DrawPosition(buffer, LineColumn(start.line() + i, start.column()), input);
    }
    buffer.ApplyTransformation(SetPositionTransformation(a));
  });
}

void ShapesAddLineToPosition(Buffer buffer, LineColumn a, LineColumn b) {
  SetLineColumn output_right = SetLineColumn();
  SetLineColumn output_down = SetLineColumn();
  FindBoundariesLine(a, b, output_right, output_down);
  DrawLineColumns(buffer, output_right, output_down, "");
}

// Draws a line.
void ShapesLine() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    LineColumn position = buffer.position();
    ShapesAddLineToPosition(buffer, position, source);
    buffer.ApplyTransformation(SetPositionTransformation(position));
    source = position;
  });
}

void ShapesAddBezier(Buffer buffer) {
  auto position = buffer.position();
  SetLineColumn output_right = SetLineColumn();
  SetLineColumn output_down = SetLineColumn();
  VectorLineColumn points = VectorLineColumn();
  points.push_back(source);
  for (int i = 0; i < bezier_points.size(); i++) {
    points.push_back(bezier_points.get(i));
  }
  points.push_back(position);
  FindBoundariesBezier(points, output_right, output_down);
  DrawLineColumns(buffer, output_right, output_down, "");
  buffer.ApplyTransformation(SetPositionTransformation(position));
  source = position;
  bezier_points = VectorLineColumn();
}

void ShapesToggleDeleteMode() {
  delete_mode = !delete_mode;
  ShapesSetStatus(delete_mode ? "Delete" : "Insert");
}

void ShapesBold() {
  bold_mode = !bold_mode;
  ShapesSetStatus(bold_mode ? "Bold" : "Normal");
}

void ShapesSource() {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    source = buffer.position();
    ShapesSetStatus("Source position: " + source.tostring());
  });
}

void ShapesPushBezierPoint(Buffer buffer) {
  bezier_points.push_back(buffer.position());
  ShapesSetStatus("Add Bezier point (" + bezier_points.size().tostring() + ")");
}

int GetDiagramInputLinesCount(Buffer buffer) {
  for (int i = 0; i < buffer.line_count(); i++) {
    if (SkipSpaces(buffer.line(i + 1)) == "") {
      return i;
    }
  }
  return i;
}

VectorString GetDiagramNouns(Buffer buffer, int lines) {
  // Use a set to eliminate repetitions.
  SetString nouns = SetString();
  for (int i = 0; i < lines; i++) {
    string line = buffer.line(i);
    if (line.substr(0, 1) != " ") {
      nouns.insert(line);
    } else {
      line = SkipSpaces(line);
      int colon = line.find(":", 0);
      if (colon == -1) {
        nouns.insert(line);
      } else {
        nouns.insert(
            SkipSpaces(line.substr(colon + 1, line.size() - (colon + 1))));
      }
    }
  }

  VectorString output = VectorString();
  for (int i = 0; i < nouns.size(); i++) {
    output.push_back(nouns.get(i));
  }

  return output;
}

VectorInt DiagramGetEdges(Buffer buffer, int lines, string a,
                          VectorString nouns) {
  string source = "";
  SetString edges = SetString();
  for (int i = 0; i < lines; i++) {
    string line = buffer.line(i);
    if (line.substr(0, 1) != " ") {
      source = line;
    } else if (source == a) {
      line = SkipSpaces(line);
      int colon = line.find(":", 0);
      if (colon != -1) {
        line = SkipSpaces(line.substr(colon + 1, line.size() - (colon + 1)));
      }
      edges.insert(line);
    }
  }

  VectorInt output = VectorInt();
  for (int i = 0; i < nouns.size(); i++) {
    if (edges.contains(nouns.get(i))) {
      output.push_back(i);
    }
  }
  return output;
}

int GetMaxNounWidth(VectorString nouns) {
  int output = 0;
  for (int i = 0; i < nouns.size(); i++) {
    output = max(output, nouns.get(i).size());
  }
  return output;
}

int GetMaxNounSize(VectorString nouns) {
  int output = 0;
  for (int i = 0; i < nouns.size(); i++) {
    output = max(output, nouns.size());
  }
  return output;
}

VectorString NounLines(string noun) {
  VectorString output = VectorString();
  int start = 0;
  while (start < noun.size()) {
    int next = noun.find(" ", start);
    if (next == -1) {
      next = noun.size();
    }
    output.push_back(noun.substr(start, next - start));
    start = next + 1;
  }
  return output;
}

int NounWidth(VectorString noun_lines) {
  int output = noun_lines.get(0).size();
  for (int i = 1; i < noun_lines.size(); i++) {
    output = max(output, noun_lines.get(i).size());
  }
  return output;
}

VectorLineColumn DiagramGetPositions(int nouns) {
  VectorLineColumn output = VectorLineColumn();
  return output;
}

LineColumn DiagramPositionForNoun(int start, int i, int column_width,
                                  int lines_per_noun) {
  int row = i / 3;
  int column = i - row * 3;
  return LineColumn(start + (row * lines_per_noun), column_width * column);
}

void DiagramDrawEdge(Buffer buffer, int start, VectorString nouns, int i, int j,
                     int column_width, int lines_per_noun) {
  LineColumn position_i =
      DiagramPositionForNoun(start, i, column_width, lines_per_noun);
  LineColumn position_j =
      DiagramPositionForNoun(start, j, column_width, lines_per_noun);

  VectorString noun_lines_i = NounLines(nouns.get(i));
  VectorString noun_lines_j = NounLines(nouns.get(j));

  ShapesAddLineToPosition(
      buffer,
      LineColumn(
          position_i.line() + (position_i.line() >= position_j.line()
                                   ? 0
                                   : noun_lines_i.size() + 2),
          position_i.column() + (position_i.column() >= position_j.column()
                                     ? 0
                                     : NounWidth(noun_lines_i))),

      LineColumn(
          position_j.line() + (position_j.line() >= position_i.line()
                                   ? 0
                                   : noun_lines_j.size() + 2),
          position_j.column() + (position_j.column() >= position_i.column()
                                     ? 0
                                     : NounWidth(noun_lines_j))));
}

void DiagramDrawEdges(Buffer buffer, int lines, int start, VectorString nouns,
                      int column_width, int lines_per_noun) {
  for (int i = 0; i < nouns.size(); i++) {
    VectorInt edges = DiagramGetEdges(buffer, lines, nouns.get(i), nouns);
    for (int j = 0; j < edges.size(); j++) {
      SetStatus("Connected: " + nouns.get(i) + "->" + nouns.get(j));
      DiagramDrawEdge(buffer, start, nouns, i, edges.get(j), column_width,
                      lines_per_noun);
    }
  }
}

void DrawNouns(Buffer buffer, int start, VectorString nouns, int column_width,
               int lines_per_noun) {
  int columns = 3;

  int row = 0;
  int column = 0;

  SetStatus("Writing nouns");
  for (int noun = 0; noun < nouns.size(); noun++) {
    VectorString noun_lines = NounLines(nouns.get(noun));
    LineColumn base_position =
        LineColumn(start + (row * lines_per_noun), column_width * column);

    for (int line = 0; line < noun_lines.size(); line++) {
      LineColumn position = LineColumn(base_position.line() + line + 1,
                                       base_position.column() + 1);
      PadToLineColumn(buffer, position);
      buffer.ApplyTransformation(
          InsertTransformationBuilder().set_text(noun_lines.get(line)).build());
    }

    ShapesAddSquareInPositions(
        buffer, base_position,
        LineColumn(base_position.line() + noun_lines.size() + 1,
                   base_position.column() + NounWidth(noun_lines) + 2));

    column++;
    if (column >= columns) {
      column = 0;
      row++;
    }
  }
}

void ShapesDrawDiagram(Buffer buffer) {
  int lines = GetDiagramInputLinesCount(buffer);
  VectorString nouns = GetDiagramNouns(buffer, lines);

  buffer.ApplyTransformation(
      SetPositionTransformation(LineColumn(buffer.line_count(), 0)));

  int start = buffer.position().line();
  int column_width = GetMaxNounWidth(nouns) + 6;
  int row_width = GetMaxNounSize(nouns) + 6;
  DrawNouns(buffer, start, nouns, column_width, row_width);
  DiagramDrawEdges(buffer, lines, start, nouns, column_width, row_width);
}

AddBinding("Sl", "shapes: line: draw", ShapesLine);
AddBinding("Sq", "shapes: square: draw", ShapesSquare);
AddBinding("Sc", "shapes: square: center contents", ShapesSquareCenter);
AddBinding("Sd", "shapes: delete_mode = !delete_mode", ShapesToggleDeleteMode);
AddBinding("S=", "shapes: set source", ShapesSource);
AddBinding("Sb", "shapes: bold_mode = !bold_mode", ShapesBold);
AddBinding("SB", "shapes: bezier: draw",
           []() -> void { editor.ForEachActiveBuffer(ShapesAddBezier); });
AddBinding("SM", "shapes: bezier: set middle point",
           []() -> void { editor.ForEachActiveBuffer(ShapesPushBezierPoint); });
AddBinding("SD", "shapes: Draw a diagram",
           []() -> void { editor.ForEachActiveBuffer(ShapesDrawDiagram); });
