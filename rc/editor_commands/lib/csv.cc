namespace csv {
// Range must be a single-line range.
string ReadContent(Buffer buffer, Range range) {
  return buffer.line(range.begin().line())
      .substr(range.begin().column(),
              range.end().column() - range.begin().column());
}

int ColumnIdToTreeChildren(int column) {
  // We multiply it by 2 to skip the commas.
  return column * 2;
}

ParseTree TreeForCell(ParseTree row, int column) {
  ParseTree cell = row.children().get(ColumnIdToTreeChildren(column));
  if (cell.children().size() == 3) {
    // If we have 3 children, this must mean we're in a string. We don't want
    // the double quotes to be part of the value we emit. So we recurse down
    // into the child in the middle.
    return cell.children().get(1);
  }
  return cell;
}

string GetCell(Buffer buffer, int row, int column) {
  ParseTree tree = buffer.tree();
  if (tree.children().size() < row) return "";
  ParseTree row_tree = tree.children().get(row);
  if (row_tree.children().size() < ColumnIdToTreeChildren(column)) return "";
  return ReadContent(buffer, TreeForCell(row_tree, column).range());
}

int FindRowIndex(Buffer buffer, string row_name) {
  ParseTree header = buffer.tree().children().get(0);
  for (int i = 0; ColumnIdToTreeChildren(i) < header.children().size(); i++) {
    if (ReadContent(buffer, TreeForCell(header, i).range()) == row_name)
      return i;
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
          ReadContent(buffer, TreeForCell(row, column).range()).toint());
    at_first = false;
  });
  return output;
}

void SortByIntColumn(Buffer buffer, int column) {
  buffer.SortLinesByKey([](int line) -> int {
    return buffer.line(line) == "" ? 0 : GetCell(buffer, line, 1).toint();
  });
}
}  // namespace csv
