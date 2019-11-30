#include "src/directory_cache.h"

#include <dirent.h>

#include "src/dirname.h"
#include "src/lru_cache.h"

namespace afc {
namespace editor {

using Cache = LRUCache<wstring, DirectoryCacheOutput>;

std::unique_ptr<DIR, std::function<void(DIR*)>> OpenDir(std::wstring path) {
  VLOG(10) << "Open dir: " << path;
  return std::unique_ptr<DIR, std::function<void(DIR*)>>(
      opendir(ToByteString(path).c_str()), closedir);
}

DirectoryCacheOutput Seek(std::wstring input) {
  VLOG(5) << "Started seek: " << input;
  DirectoryCacheOutput output;

  std::list<std::wstring> components;
  if (input.empty() || !DirectorySplit(input, &components) ||
      components.empty()) {
    VLOG(4) << "Not really seeking, input: " << input;
    return output;
  }

  if (!components.empty() && input[input.size() - 1] == L'/') {
    components.push_back(L"");
  }

  output.longest_prefix = input[0] == L'/' ? L"/" : L"";
  auto parent_dir = OpenDir(input[0] == L'/' ? L"/" : L".");
  while (components.size() > 1) {
    auto subdir_path = PathJoin(output.longest_prefix, components.front());
    auto subdir = OpenDir(subdir_path);
    if (subdir != nullptr) {
      parent_dir = std::move(subdir);
      output.longest_prefix = std::move(subdir_path);
      components.pop_front();
    } else {
      break;
    }
  }

  struct dirent* entry;
  auto prefix = components.front();
  int longest_prefix_match = 0;
  while ((entry = readdir(parent_dir.get())) != nullptr) {
    auto entry_name = FromByteString(string(entry->d_name));
    auto mismatch_results = std::mismatch(prefix.begin(), prefix.end(),
                                          entry_name.begin(), entry_name.end());
    if (mismatch_results.first != prefix.end()) {
      longest_prefix_match =
          std::max<int>(longest_prefix_match,
                        std::distance(prefix.begin(), mismatch_results.first));
      continue;
    }
    if (output.count == 0) {
      output.longest_suffix = entry_name;
    } else if (!output.longest_suffix.empty()) {
      output.longest_suffix = output.longest_suffix.substr(
          0, std::distance(output.longest_suffix.begin(),
                           std::mismatch(output.longest_suffix.begin(),
                                         output.longest_suffix.end(),
                                         entry_name.begin(), entry_name.end())
                               .first));
    }
    if (entry_name == prefix) {
      output.exact_match = DirectoryCacheOutput::ExactMatch::kFound;
    }
    output.count++;
  }

  if (output.count == 0) {
    output.longest_prefix =
        PathJoin(output.longest_prefix, prefix.substr(0, longest_prefix_match));
  }

  VLOG(5) << "Seek matches: " << output.count << " with prefix "
          << output.longest_prefix << " and suffix " << output.longest_suffix;
  return output;
}

AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput> NewDirectoryCache() {
  auto cache = std::make_shared<LRUCache<wstring, DirectoryCacheOutput>>(1024);
  return AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput>(
      [cache](DirectoryCacheInput input) -> DirectoryCacheOutput {
        auto output =
            *cache->Get(input.pattern, [&]() { return Seek(input.pattern); });
        input.callback(output);
        return output;
      },
      [] {});
}

}  // namespace editor
}  // namespace afc
