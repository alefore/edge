#include "numbers.cc"
#include "strings.cc"

namespace csv {
namespace internal {
ParseTree TreeForCell(ParseTree row, int column) {
  return row.children().get(column);
}

int RangeWidth(Range range) {
  return range.end().column() - range.begin().column();
}

string ReadContent(Buffer buffer, Range range) {
  return buffer.line(range.begin().line())
      .substr(range.begin().column(),
              range.end().column() - range.begin().column());
}

void FindCellContentInTree(ParseTree tree, VectorParseTree output) {
  if (tree.properties().contains("cell_content"))
    output.push_back(tree);
  else
    tree.children().ForEach(
        [](ParseTree child) -> void { FindCellContentInTree(child, output); });
}

ParseTree FindCellContentInTree(ParseTree cell) {
  VectorParseTree content_cells = VectorParseTree();
  FindCellContentInTree(cell, content_cells);
  return content_cells.empty() ? cell : content_cells.get(0);
}

string ReadCellContent(Buffer buffer, ParseTree cell) {
  return ReadContent(buffer, FindCellContentInTree(cell).range());
}

int CountColumns(Buffer csv_file) {
  int output = 0;
  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    output = max(output, row.children().size());
  });
  return output;
}

VectorInt GetColumnSizes(Buffer csv_file) {
  VectorInt column_sizes = VectorInt();
  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    if (row.children().size() == 0) return;
    int columns = row.children().size();
    for (int column = 0; column < columns; column++) {
      if (column_sizes.size() == column) column_sizes.push_back(0);
      int width = RangeWidth(TreeForCell(row, column).range());
      column_sizes.set(column, max(column_sizes.get(column), width));
    }
  });
  return column_sizes;
}

string GetCell(Buffer buffer, int row, int column) {
  ParseTree tree = buffer.tree();
  if (tree.children().size() < row) return "";
  ParseTree row_tree = tree.children().get(row);
  if (row_tree.children().size() < column) return "";
  return ReadCellContent(buffer, TreeForCell(row_tree, column));
}

int FindRowIndex(Buffer buffer, string row_name) {
  ParseTree header = buffer.tree().children().get(0);
  for (int column = 0; column < header.children().size(); column++) {
    if (ReadCellContent(buffer, TreeForCell(header, column)) == row_name)
      return column;
  }
  return -1;
}

VectorInt ColumnToVectorInt(Buffer buffer, int column, bool skip_first) {
  VectorInt output = VectorInt();
  bool at_first = true;
  buffer.tree().children().ForEach([](ParseTree row) -> void {
    // TODO(errors): Warn that some values were ignored?
    if (!at_first && row.children().size() > column)
      output.push_back(
          ReadCellContent(buffer, TreeForCell(row, column)).toint());
    at_first = false;
  });
  return output;
}

void SortByIntColumn(Buffer buffer, int column) {
  buffer.SortLinesByKey([](int line) -> int {
    return buffer.line(line) == ""
               ? -1
               : SkipInitialSpaces(GetCell(buffer, line, column)).toint();
  });
}

void SortByColumn(Buffer buffer, int column) {
  buffer.SortLinesByKey([](int line) -> string {
    return buffer.line(line) == ""
               ? ""
               : SkipInitialSpaces(GetCell(buffer, line, column));
  });
}

////////////////////////////////////////////////////////////////////////////////
// Aligning columns
////////////////////////////////////////////////////////////////////////////////

TransformationOutput AlignColumnsTransformation(Buffer csv_file) {
  VectorInt column_sizes = GetColumnSizes(csv_file);
  TransformationOutput output = TransformationOutput();

  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    int columns = row.children().size();
    for (int index = 0; index < columns; index++) {
      // We work backwards (starting at the last column):
      int column = columns - index - 1;

      Range range = TreeForCell(row, column).range();
      int padding = column_sizes.get(column) - RangeWidth(range);
      if (padding > 0)
        output.push(InsertTransformationBuilder()
                        .set_position(LineColumn(range.end().line(),
                                                 range.end().column()))
                        .set_text(" " * padding)
                        .build());
    }
  });
  return output;
}

void AlignColumns(Buffer csv_file) {
  csv_file.ApplyTransformation(FunctionTransformation(
      [](TransformationInput input) -> TransformationOutput {
        return AlignColumnsTransformation(csv_file);
      }));
}
}  // namespace internal

////////////////////////////////////////////////////////////////////////////////
// Public Interface
////////////////////////////////////////////////////////////////////////////////

void Enable(Buffer buffer) {
  buffer.set_tree_parser("csv");
  buffer.set_cpp_prompt_namespaces("csv");
  buffer.SetStatus("🔡 CSV file");
}

void SortByIntColumn(string column) {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    internal::SortByIntColumn(buffer, column.toint());
  });
}

void SortByColumn(string column) {
  editor.ForEachActiveBuffer([](Buffer buffer) -> void {
    internal::SortByColumn(buffer, column.toint());
  });
}

void Align() {
  editor.ForEachActiveBuffer(
      [](Buffer buffer) -> void { internal::AlignColumns(buffer); });
}

}  // namespace csv
