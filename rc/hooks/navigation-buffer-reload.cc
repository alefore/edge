SetStatus("Enjoy your navigation.");

void IncrementDepth() {
  navigation_buffer_depth = navigation_buffer_depth + 1;
}

void DecrementDepth() {
  navigation_buffer_depth = navigation_buffer_depth - 1;
}

buffer.AddBinding("sj", IncrementDepth);
buffer.AddBinding("sk", DecrementDepth);
