#include "buffer_output.cc"
#include "lib/markdown.cc"

VectorBuffer LoadFromGlob(string glob_pattern, Buffer log_buffer) {
  OutputBufferLog(log_buffer, "Glob: " + glob_pattern);
  VectorString paths = Glob(glob_pattern);
  OutputBufferLog(log_buffer, "Opening buffers: " + paths.size().tostring());
  VectorBuffer input_buffers = editor.OpenFile(paths, false);
  OutputBufferLog(log_buffer, "Waiting for EOF (buffers: " +
                                  input_buffers.size().tostring() + ")");
  return WaitForEndOfFile(input_buffers);
}

VectorBuffer LoadTagsFromGlob(string glob_pattern, Buffer log_buffer) {
  VectorBuffer output = LoadFromGlob(glob_pattern, log_buffer);
  OutputBufferLog(log_buffer, "Buffers loaded, filtering.");
  output = md::SearchOptionsForSection("Tags", 2).filter(output);
  OutputBufferLog(log_buffer,
                  "Filter done (matches: " + output.size().tostring() + ").");
  return output;
}

VectorFileTags LoadFileTagsFromGlob(string glob_pattern, Buffer log_buffer) {
  return VectorFileTags(LoadTagsFromGlob(glob_pattern, log_buffer));
}
