LineColumn FindSymbolBegin(Buffer buffer, LineColumn position) {
  auto line = buffer.line(position.line());
  int column = position.column();
  while (column > 0) {
    if (buffer.symbol_characters().find(line.substr(column - 1, 1), 0) == -1)
      return LineColumn(position.line(), column);
    column--;
  }
  return LineColumn(position.line(), column);
}

LineColumn FindSymbolEnd(Buffer buffer, LineColumn position) {
  auto line = buffer.line(position.line());
  int column = position.column();
  while (column + 1 < line.size()) {
    if (buffer.symbol_characters().find(line.substr(column + 1, 1), 0) == -1)
      return LineColumn(position.line(), column + 1);
    column++;
  }
  editor.SetStatus("Moved from " + position.column().tostring() + " to " +
                   column.tostring());
  return LineColumn(position.line(), column + 1);
}
