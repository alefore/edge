SetStatus("Enjoy your navigation.");

void SetDepthToZero() {
  if (navigation_buffer_depth == 0) {
    SetStatus("We're already at the surface.");
    return;
  }
  navigation_buffer_depth = 0;
  buffer.Reload();
  SetStatus("Thought is the wind, and knowledge the sail.");
}

void IncrementDepth() {
  navigation_buffer_depth = navigation_buffer_depth + 1;
  buffer.Reload();
  SetStatus("We must go deeper (" + tostring(navigation_buffer_depth) + ")");
}

void DecrementDepth() {
  if (navigation_buffer_depth == 0) {
    SetStatus("We're already at the surface.");
    return;
  }
  navigation_buffer_depth = navigation_buffer_depth - 1;
  buffer.Reload();
  SetStatus("Simplifying view (" + tostring(navigation_buffer_depth) + ")");
}

buffer.AddBinding("sk", "navigation_depth := 0", SetDepthToZero);
buffer.AddBinding("sh", "navigation_depth--", DecrementDepth);
buffer.AddBinding("sl", "navigation_depth++", IncrementDepth);
