// Defines symbol `CamelCaseTransformation` that can be used to transform the
// case back-and-forth to CamelCase for the symbol under the cursor.

#include "lib/sections.cc"

bool LessThan(LineColumn a, LineColumn b) {
  return a.line() < b.line() ||
         (a.line() == b.line() && a.column() < b.column());
}

string TransformCase(string input) {
  if (input == input.tolower() || input == input.toupper()) {
    string output = "";
    bool at_start = true;
    for (int i = 0; i < input.size(); i++) {
      string c = input.substr(i, 1);
      if (c == "_") {
        at_start = true;
      } else if (at_start) {
        output += c.toupper();
        at_start = false;
      } else {
        output += c.tolower();
      }
    }
    return output;
  } else {
    string output = "";
    for (int i = 0; i < input.size(); i++) {
      string c = input.substr(i, 1);
      if (c == c.toupper() && i > 0) {
        output += "_";
      }
      output += c.tolower();
    }
    return output;
  }
  return input;
}

TransformationOutput CamelCaseTransformation(Buffer buffer,
                                             TransformationInput input) {
  TransformationOutput output = TransformationOutput();
  auto begin = FindSymbolBegin(buffer, input.position());
  auto end = FindSymbolEnd(buffer, begin);
  output.push(SetPositionTransformation(begin));
  auto text = buffer.line(input.position().line())
                  .substr(begin.column(), end.column() - begin.column());
  output.push(DeleteTransformationBuilder()
                  .set_modifiers(Modifiers().set_repetitions(text.size()))
                  .build());
  output.push(
      InsertTransformationBuilder().set_text(TransformCase(text)).build());
  return output;
}
