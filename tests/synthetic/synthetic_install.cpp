#include "synthetic/synthetic_install.h"

#include <format>
#include <memory>
#include <stdexcept>

#include "synthetic/generators.h"
#include "synthetic/manifest.h"

namespace sh::test {

namespace synth {

const FileManifest& Context::entry(const std::string& path) const {
  const auto it = manifest.find(path);
  if (it == manifest.end()) {
    throw std::runtime_error(std::format("synthetic install: no manifest entry for {}", path));
  }
  return it->second;
}

std::vector<const FileManifest*> Context::entries(std::string_view prefix, std::string_view kind) const {
  std::vector<const FileManifest*> out;
  for (auto it = manifest.lower_bound(std::string(prefix)); it != manifest.end() && it->first.starts_with(prefix);
       ++it) {
    if (kind.empty() || it->second.kind == kind) {
      out.push_back(&it->second);
    }
  }
  return out;
}

}  // namespace synth

InstallFiles synthetic_install_files(const SyntheticOptions& options) {
  InstallFiles files;
  synth::Context context{.manifest = committed_manifest(),
                         .random = SynthRandom(options.seed),
                         .files = files,
                         .model = {}};
  // The tables name the classes the art and the scenarios use, the world
  // the maps the scenarios use.
  synth::generate_tables(context);
  synth::generate_art(context);
  synth::generate_world(context);
  synth::generate_sounds(context);
  synth::generate_videos(context);
  synth::generate_scenarios(context);
  synth::generate_misc(context);
  return files;
}

const Install& synthetic_install() {
  static const Install install =
      install_from_files(std::make_shared<const InstallFiles>(synthetic_install_files({})), "synthetic install");
  return install;
}

}  // namespace sh::test
