number x = 0;

void OnReload(Buffer buffer) {
  // Just burn some cycles.
  for (number i = 0; i < 1000; i++) buffer.line_count();

  x = 5678;
}
