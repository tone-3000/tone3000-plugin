#include "Library.h"

namespace t3k::ui {

namespace {

LibraryNode::Kind kindFromString(const juce::String& s) {
  if (s == "library") return LibraryNode::Kind::library;
  if (s == "preset") return LibraryNode::Kind::preset;
  if (s == "tone") return LibraryNode::Kind::tone;
  if (s == "capture") return LibraryNode::Kind::capture;
  return LibraryNode::Kind::folder;
}

}  // namespace

LibraryToneRef LibraryToneRef::parse(const juce::var& v) {
  LibraryToneRef ref;
  ref.raw = v;
  const auto& tone = v["tone"];
  const auto& model = v["model"];
  ref.toneId = tone.getProperty("id", 0);
  ref.modelId = model.getProperty("id", 0);
  ref.title = tone["title"].toString();
  ref.gear = tone["gear"].toString();
  ref.format = tone["format"].toString();
  ref.creator = tone["user"]["username"].toString();
  ref.modelName = model["name"].toString();
  return ref;
}

namespace {

const LibraryNode* findIn(const std::vector<LibraryNode>& nodes, const juce::String& path,
                          const LibraryNode* parent, const LibraryNode** parentOut) {
  for (const auto& node : nodes) {
    if (node.path == path) {
      if (parentOut != nullptr) *parentOut = parent;
      return &node;
    }
    if (const auto* hit = findIn(node.children, path, &node, parentOut)) return hit;
  }
  return nullptr;
}

void addSubtree(const LibraryNode& node, int depth, std::vector<LibraryRow>& out, const std::set<juce::String>& closed) {
  const bool isOpen = node.isContainer() && closed.count(node.path) == 0;
  out.push_back({&node, depth, isOpen});
  if (isOpen)
    for (const auto& child : node.children) addSubtree(child, depth + 1, out, closed);
}

// Filtered: true when anything under (or at) `node` shows.
bool gearMatches(const LibraryNode& node, const std::set<juce::String>& gears) {
  if (gears.empty()) return true;
  auto kind = node.gearKind();
  if (kind == "full-rig") kind = "amp-cab";  // the deprecated id
  return gears.count(kind) != 0;
}

// `nameHit`: a container above matched the text, so everything in it does.
// `firstHit`: the first container whose name matched is taken (it alone
// shows open; the others closed, until opened).
bool addMatches(const LibraryNode& node, int depth, const juce::String& filter, std::vector<LibraryRow>& out,
                const std::set<juce::String>& closed, const std::set<juce::String>& opened,
                const std::set<juce::String>& gears, bool nameHit, bool& firstHit) {
  const bool hit = nameHit || filter.trim().isEmpty() || libraryNameMatches(node.name, filter);
  if (!node.isContainer()) {
    if (!hit || !gearMatches(node, gears)) return false;
    out.push_back({&node, depth, false});
    return true;
  }
  if (hit && gears.empty()) {
    const bool first = !firstHit && !node.children.empty();
    firstHit = firstHit || first;
    if ((first && closed.count(node.path) == 0) || opened.count(node.path) != 0) {
      addSubtree(node, depth, out, closed);
    } else {
      out.push_back({&node, depth, false});
    }
    return true;
  }
  std::vector<LibraryRow> inside;
  for (const auto& child : node.children)
    addMatches(child, depth + 1, filter, inside, closed, opened, gears, hit, firstHit);
  if (inside.empty()) return false;
  const bool isOpen = closed.count(node.path) == 0;
  out.push_back({&node, depth, isOpen});
  if (isOpen) out.insert(out.end(), inside.begin(), inside.end());
  return true;
}

void addOpen(const LibraryNode& node, int depth, const std::set<juce::String>& open, std::vector<LibraryRow>& out) {
  const bool isOpen = node.isContainer() && open.count(node.path) != 0;
  out.push_back({&node, depth, isOpen});
  if (isOpen)
    for (const auto& child : node.children) addOpen(child, depth + 1, open, out);
}

}  // namespace

LibraryNode LibraryNode::parse(const juce::var& v) {
  LibraryNode node;
  node.kind = kindFromString(v["kind"].toString());
  node.name = v["name"].toString();
  node.path = v["path"].toString();
  node.mine = v.getProperty("mine", false);
  node.mount = v.getProperty("mount", false);
  node.linked = v.getProperty("linked", false);
  node.local = v.getProperty("local", false);
  node.missing = v.getProperty("missing", false);
  node.editable = v.getProperty("editable", false);
  node.removable = v.getProperty("removable", false);
  node.writable = v.getProperty("writable", false);
  node.presetId = v["id"].toString();
  if (node.kind == Kind::tone) node.tone = LibraryToneRef::parse(v["ref"]);
  node.type = v["type"].toString();
  node.nam = v["format"].toString() != "ir";
  node.gear = v["gear"].toString();
  if (const auto* kids = v["children"].getArray())
    for (const auto& kid : *kids) node.children.push_back(parse(kid));
  return node;
}

LibraryTree LibraryTree::parse(const juce::var& v, bool index) {
  LibraryTree tree;
  tree.root = v["root"].toString();
  tree.owner = v["owner"].toString();
  tree.truncated = v.getProperty("truncated", false);
  if (const auto* libs = v["libraries"].getArray())
    for (const auto& lib : *libs) tree.libraries.push_back(LibraryNode::parse(lib));
  if (index) tree.reindex();
  return tree;
}

void LibraryTree::reindex() {
  byPath_.clear();
  parentByPath_.clear();
  capturesByName_.clear();
  for (const auto& library : libraries) indexNode(library, nullptr);
  for (const auto& h : hidden) {
    const auto parent = byPath_.find(h.parent);
    indexNode(h.node, parent != byPath_.end() ? parent->second : nullptr);
  }
}

void LibraryTree::indexNode(const LibraryNode& node, const LibraryNode* parent) {
  byPath_[node.path] = &node;
  parentByPath_[node.path] = parent;
  if (node.kind == LibraryNode::Kind::capture) capturesByName_.emplace(node.file().getFileName().toLowerCase(), &node);
  for (const auto& child : node.children) indexNode(child, &node);
}

juce::String LibraryNode::gearKind() const {
  switch (kind) {
    case Kind::tone: return tone.gear.toLowerCase();
    case Kind::capture: return gear;
    case Kind::preset: return "preset";
    default: return {};
  }
}

const LibraryNode* LibraryTree::mine() const {
  for (const auto& library : libraries)
    if (library.mine && !library.site) return &library;
  return nullptr;
}

std::vector<const LibraryNode*> LibraryTree::capturesNamed(const juce::String& fileName) const {
  std::vector<const LibraryNode*> found;
  const auto range = capturesByName_.equal_range(fileName.toLowerCase());
  for (auto it = range.first; it != range.second; ++it) found.push_back(it->second);
  return found;
}

bool LibraryTree::inLinked(const juce::String& path) const {
  for (const auto* node = find(path); node != nullptr; node = parentOf(node->path))
    if (node->linked) return true;
  return false;
}

bool LibraryNode::accepts(const LibraryNode& item) const {
  if (!isContainer()) return false;
  if (favorites) return item.kind == Kind::tone;  // only catalog tones can be favorites
  if (isPresets()) return item.kind == Kind::preset || (item.isContainer() && !item.isCaptures() && !item.favorites);
  if (isCaptures())
    return item.kind == Kind::tone || item.kind == Kind::capture || (item.isContainer() && !item.isPresets());
  return true;
}

const LibraryNode* LibraryTree::find(const juce::String& path) const {
  if (path.isEmpty()) return nullptr;
  if (!byPath_.empty()) {
    const auto it = byPath_.find(path);
    return it != byPath_.end() ? it->second : nullptr;
  }
  return findIn(libraries, path, nullptr, nullptr);
}

const LibraryNode* LibraryTree::parentOf(const juce::String& path) const {
  if (!parentByPath_.empty()) {
    const auto it = parentByPath_.find(path);
    return it != parentByPath_.end() ? it->second : nullptr;
  }
  const LibraryNode* parent = nullptr;
  return findIn(libraries, path, nullptr, &parent) != nullptr ? parent : nullptr;
}

bool libraryNameMatches(const juce::String& name, const juce::String& filter) {
  for (const auto& word : juce::StringArray::fromTokens(filter, true))
    if (word.isNotEmpty() && !name.containsIgnoreCase(word)) return false;
  return true;
}

std::vector<LibraryRow> libraryRows(const LibraryTree& tree, const std::set<juce::String>& open,
                                    const juce::String& filter, const std::set<juce::String>& closedInFilter,
                                    const std::set<juce::String>& gears,
                                    const std::set<juce::String>& openedInFilter) {
  std::vector<LibraryRow> rows;
  const bool filtering = filter.trim().isNotEmpty() || !gears.empty();
  bool firstHit = false;
  for (const auto& library : tree.libraries) {
    if (!filtering) {
      addOpen(library, 0, open, rows);
      continue;
    }
    // A library's own name doesn't count as a match: every library would
    // match its owner's name and drown the results. Search inside it.
    std::vector<LibraryRow> inside;
    for (const auto& child : library.children)
      addMatches(child, 1, filter, inside, closedInFilter, openedInFilter, gears, false, firstHit);
    if (inside.empty()) continue;
    const bool isOpen = closedInFilter.count(library.path) == 0;
    rows.push_back({&library, 0, isOpen});
    if (isOpen) rows.insert(rows.end(), inside.begin(), inside.end());
  }
  return rows;
}

}  // namespace t3k::ui
