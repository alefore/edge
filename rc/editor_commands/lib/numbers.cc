// Various functions for handling numbers.

int max(int a, int b) { return a >= b ? a : b; }
double max(double a, double b) { return a >= b ? a : b; }
double max(int a, double b) { return a >= b ? a + 0.0 : b; }
double max(double a, int b) { return a >= b ? a : b + 0.0; }

int min(int a, int b) { return a <= b ? a : b; }
double min(double a, double b) { return a <= b ? a : b; }
double min(int a, double b) { return a <= b ? a + 0.0 : b; }
double min(double a, int b) { return a <= b ? a : b + 0.0; }

int abs(int a) { return a < 0 ? -a : a; }
double abs(double a) { return a < 0 ? -a : a; }

string IntegerAsString(Buffer buffer, LineColumn position) {
  string line = buffer.line(position.line());
  line = line.substr(position.column(), line.size() - position.column());
  string numbers = "-0123456789";
  int i = 0;
  while (i < line.size() && numbers.find(line.substr(i, 1), 0) != -1) {
    numbers = "0123456789";  // Disallow "-".
    i++;
  }
  return line.substr(0, i);
}

string number_characters = "-0123456789";

LineColumn FindNextNumber(Buffer buffer, LineColumn position) {
  while (true) {
    string line = buffer.line(position.line());
    int column = line.find_first_of(number_characters, position.column());
    if (column != -1) {
      return LineColumn(position.line(), column);
    }
    if (position.line() + 1 == buffer.line_count()) {
      return position;
    }
    position = LineColumn(position.line() + 1, 0);
  }
}

LineColumn ScrollBackToFirstPositionInNumber(Buffer buffer,
                                             LineColumn position) {
  string line = buffer.line(position.line());
  while (position.column() > 0 &&
         number_characters.find(line.substr(position.column() - 1, 1), 0) !=
             -1) {
    position = LineColumn(position.line(), position.column() - 1);
  }
  return position;
}

TransformationOutput AddToIntegerTransformationCallback(
    Buffer buffer, int delta, TransformationInput input) {
  auto position = ScrollBackToFirstPositionInNumber(
      buffer, FindNextNumber(buffer, input.position()));
  string integer_str = IntegerAsString(buffer, position);
  return TransformationOutput()
      .push(SetPositionTransformation(position))
      .push(DeleteTransformationBuilder()
                .set_modifiers(Modifiers().set_repetitions(integer_str.size()))
                .build())
      .push(InsertTransformationBuilder()
                .set_text((integer_str.toint() + delta).tostring())
                .build())
      .push(SetPositionTransformation(position));
}

void AddToIntegerTransformation(Buffer buffer, int delta) {
  buffer.ApplyTransformation(FunctionTransformation(
      [](TransformationInput input) -> TransformationOutput {
        return AddToIntegerTransformationCallback(buffer, delta, input);
      }));
}
