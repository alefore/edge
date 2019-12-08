#include "src/directory_cache.h"

#include "src/dirname.h"
#include "src/lru_cache.h"

namespace afc::editor {

using Cache = LRUCache<wstring, DirectoryCacheOutput>;

std::ostream& operator<<(std::ostream& os, const DirectoryCacheOutput& o) {
  os << "[DirectoryCacheOutput count: " << o.count << ", longest_prefix "
     << o.longest_prefix << ", longest_suffix " << o.longest_suffix << "]";
  return os;
}

// Reads the contents of `parent_dir` looking for files matching `prefix`.
// Updates parameters in `output` accordingly.
void IterateDir(std::unique_ptr<DIR, std::function<void(DIR*)>> parent_dir,
                const wstring& prefix, DirectoryCacheOutput* output) {
  struct dirent* entry;
  int longest_prefix_match = 0;
  while ((entry = readdir(parent_dir.get())) != nullptr) {
    auto entry_name = FromByteString(string(entry->d_name));
    if (entry_name == L"." || entry_name == L"..") continue;
    auto mismatch_results = std::mismatch(prefix.begin(), prefix.end(),
                                          entry_name.begin(), entry_name.end());
    if (mismatch_results.first != prefix.end()) {
      VLOG(20) << "The entry " << entry_name
               << " doesn't contain the whole prefix.";
      longest_prefix_match =
          std::max<int>(longest_prefix_match,
                        std::distance(prefix.begin(), mismatch_results.first));
      continue;
    }
    VLOG(10) << "The entry " << entry_name << "contains the entire prefix.";
    if (output->count == 0) {
      output->longest_suffix = entry_name;
    } else if (!output->longest_suffix.empty()) {
      output->longest_suffix = output->longest_suffix.substr(
          0, std::distance(output->longest_suffix.begin(),
                           std::mismatch(output->longest_suffix.begin(),
                                         output->longest_suffix.end(),
                                         entry_name.begin(), entry_name.end())
                               .first));
      VLOG(10) << "Adjusted suffix: " << output;
    }
    if (entry_name == prefix) {
      output->exact_match = DirectoryCacheOutput::ExactMatch::kFound;
    }
    output->count++;
    VLOG(20) << "After iteration: " << output;
  }

  VLOG(7) << "After search: " << output;
}

DirectoryCacheOutput Seek(std::wstring input,
                          std::vector<std::wstring> search_paths) {
  VLOG(5) << "Started seek: " << input;
  DirectoryCacheOutput output;

  if (find(search_paths.begin(), search_paths.end(), L"") ==
      search_paths.end()) {
    search_paths.push_back(L"");
  }

  // path = editor_state->expand_path(path);
  // if (!path.empty() && path[0] == L'/') {
  // search_paths = {L""};
  //}

  std::list<std::wstring> components;
  if (input.empty() || !DirectorySplit(input, &components) ||
      components.empty()) {
    VLOG(4) << "Not really seeking, input: " << input;
    return output;
  }

  if (!components.empty() && input[input.size() - 1] == L'/') {
    components.push_back(L"");
  }

  for (auto search_path : search_paths) {
    if (!search_path.empty() && input.front() == '/') {
      VLOG(5) << "Skipping non-empty search path for absolute path.";
      continue;
    }
    DirectoryCacheOutput search_path_output;
    if (input[0] == L'/') {
      search_path = L"/";
      search_path_output.longest_prefix = L"/";
    } else if (search_path.empty()) {
      search_path = L".";
    }
    VLOG(6) << "Starting search at: " << search_path;
    auto parent_dir = OpenDir(search_path);
    if (parent_dir == nullptr) {
      VLOG(5) << "Unable to open search_path: " << search_path;
      continue;
    }
    while (components.size() > 1) {
      auto subdir_path =
          PathJoin(search_path_output.longest_prefix, components.front());
      auto subdir = OpenDir(PathJoin(search_path, subdir_path));
      if (subdir != nullptr) {
        parent_dir = std::move(subdir);
        search_path_output.longest_prefix = std::move(subdir_path);
        components.pop_front();
      } else {
        break;
      }
    }
    VLOG(5) << "After descent: " << search_path_output.longest_prefix;

    IterateDir(std::move(parent_dir), components.front(), &search_path_output);

    VLOG(5) << "Merging " << search_path_output << " into " << output;
    if (output.longest_prefix.size() <
        search_path_output.longest_prefix.size()) {
      VLOG(8) << "Taking findings from this search path: "
              << search_path_output;
      output = search_path_output;
    } else if (output.longest_prefix.size() ==
               search_path_output.longest_prefix.size()) {
      VLOG(8) << "Merging findings.";
      output.longest_suffix =
          output.count == 0
              ? search_path_output.longest_suffix
              : min(search_path_output.longest_suffix, output.longest_suffix);
      output.count += search_path_output.count;
      if (search_path_output.exact_match ==
          DirectoryCacheOutput::ExactMatch::kFound) {
        output.exact_match = DirectoryCacheOutput::ExactMatch::kFound;
      }
    }
  }

  VLOG(5) << "Seek matches: " << output;
  return output;
}  // namespace afc::editor

AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput> NewDirectoryCache() {
  auto cache = std::make_shared<LRUCache<wstring, DirectoryCacheOutput>>(1024);
  AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput>::Options options;
  options.name = L"DirectoryCache";
  options.factory = [cache](DirectoryCacheInput input) -> DirectoryCacheOutput {
    auto output = *cache->Get(input.pattern, [&]() {
      return Seek(input.pattern, input.search_paths);
    });
    input.callback(output);
    return output;
  };
  return AsyncProcessor<DirectoryCacheInput, DirectoryCacheOutput>(
      std::move(options));
}

}  // namespace afc::editor
