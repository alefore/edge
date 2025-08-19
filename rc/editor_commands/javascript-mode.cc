void JavaScriptMode(Buffer buffer) {
  buffer.set_paragraph_line_prefix_characters(" /*");
  buffer.set_line_prefix_characters(" /*");
  buffer.set_line_width(100);
  buffer.set_language_keywords(
      // General keywords
      "break case catch class const continue debugger default delete do else "
      "export extends false finally for function if import in instanceof new "
      "null return super switch this throw true try typeof var void while with "
      // ES6+ keywords
      "let static yield await async of "
      // Reserved words (strict mode)
      "implements interface package private protected public "
      // Global objects (common in keywords lists for highlighting purposes)
      "Array Date eval function hasOwnProperty Infinity isFinite isNaN JSON "
      "Math NaN Number Object prototype String undefined decodeURI "
      "decodeURIComponent encodeURI encodeURIComponent escape unescape "
      // Common types/values
      "boolean number string symbol object Map Set WeakMap WeakSet Promise Proxy Reflect "
      "Generator GeneratorFunction");
  buffer.set_tree_parser("javascript");
}
