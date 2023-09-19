#include "numbers.cc"
#include "strings.cc"

namespace csv {
namespace internal {
string ReadContent(Buffer buffer, Range range) {
  return buffer.line(range.begin().line())
      .substr(range.begin().column(),
              range.end().column() - range.begin().column());
}

string ReadCellContent(Buffer buffer, ParseTree cell) {
  if (cell.children().size() == 3) {
    // If we have 3 children, this must mean we're in a string. We don't want
    // the double quotes to be part of the value we emit. So we recurse down
    // into the child in the middle.
    cell = cell.children().get(1);
  }
  return ReadContent(buffer, cell.range());
}

int ColumnIdToTreeChildren(int column) {
  // We multiply it by 2 to skip the commas.
  return column * 2;
}

int TreeChildrenToColumnId(int children_id) { return children_id / 2; }

int CountColumns(Buffer csv_file) {
  int output = 0;
  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    output = max(output, TreeChildrenToColumnId(row.children().size() - 1));
  });
  return output;
}

ParseTree TreeForCell(ParseTree row, int column) {
  return row.children().get(ColumnIdToTreeChildren(column));
}

ParseTree TreeForCellContent(ParseTree row, int column) {
  ParseTree cell = TreeForCell(row, column);
  return cell;
}

VectorInt GetColumnSizes(Buffer csv_file) {
  VectorInt column_sizes = VectorInt();
  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    if (row.children().size() == 0) return;
    int columns = TreeChildrenToColumnId(row.children().size() - 1);
    for (int column = 0; column < columns; column++) {
      if (column_sizes.size() == column) column_sizes.push_back(0);
      string cell = ReadContent(csv_file, TreeForCell(row, column).range());
      column_sizes.set(column, max(column_sizes.get(column), cell.size()));
    }
  });
  return column_sizes;
}

string GetCell(Buffer buffer, int row, int column) {
  ParseTree tree = buffer.tree();
  if (tree.children().size() < row) return "";
  ParseTree row_tree = tree.children().get(row);
  if (row_tree.children().size() < ColumnIdToTreeChildren(column)) return "";
  return ReadCellContent(buffer, TreeForCell(row_tree, column));
}

int FindRowIndex(Buffer buffer, string row_name) {
  ParseTree header = buffer.tree().children().get(0);
  for (int i = 0; ColumnIdToTreeChildren(i) < header.children().size(); i++) {
    if (ReadCellContent(buffer, TreeForCell(header, i)) == row_name) return i;
  }
  return -1;
}

VectorInt ColumnToVectorInt(Buffer buffer, int column, bool skip_first) {
  VectorInt output = VectorInt();
  bool at_first = true;
  buffer.tree().children().ForEach([](ParseTree row) -> void {
    // TODO(errors): Warn that some values were ignored?
    if (!at_first && row.children().size() > ColumnIdToTreeChildren(column))
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

////////////////////////////////////////////////////////////////////////////////
// Aligning columns
////////////////////////////////////////////////////////////////////////////////

TransformationOutput AlignColumnsTransformation(Buffer csv_file) {
  VectorInt column_sizes = GetColumnSizes(csv_file);
  TransformationOutput output = TransformationOutput();

  csv_file.tree().children().ForEach([](ParseTree row) -> void {
    int columns = TreeChildrenToColumnId(row.children().size() - 1);
    for (int index = 0; index < columns; index++) {
      // We work backwards (starting at the last column):
      int column = columns - index - 1;

      Range range = TreeForCell(row, column).range();
      int width = range.end().column() - range.begin().column();
      int padding = column_sizes.get(column) - width;
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

void Align() {
  editor.ForEachActiveBuffer(
      [](Buffer buffer) -> void { internal::AlignColumns(buffer); });
}

}  // namespace csv
