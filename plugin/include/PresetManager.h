#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
 * On-disk internal preset store. Pure file layer: one XML file per preset,
 * no knowledge of what a preset contains (the processor builds/consumes the
 * ValueTree payloads). Message-thread only.
 *
 * Layout:
 *   <user data dir>/TONE3000/Presets/<uuid>.t3kpreset   (user presets)
 *   <user data dir>/TONE3000/Presets/Factory/…          (read-only factory)
 *   <system data dir>/TONE3000/Presets/Factory/…        (installer-shipped)
 *
 * The system Factory folder is where installers drop shipped presets
 * (macOS /Library/Application Support, Windows ProgramData). Both Factory
 * dirs are scanned; a user-Factory file with the same stem wins so local
 * overrides of a shipped preset are possible.
 *
 * Ids are "user:<stem>" / "factory:<stem>" so the two namespaces can never
 * collide and the UI can tell them apart without extra lookups. Display
 * names live *inside* the file (filenames are uuids), so any characters are
 * fine and renames never touch the filesystem name.
 *
 * The list is rescanned on every call; it's a handful of stat()s, and it
 * keeps multiple plugin instances sharing the folder coherent for free.
 *
 * Ordering: user presets always come before factory presets (the browser's
 * two sections; a player's own presets own the low MIDI program-change
 * numbers). Within each section a custom order can be set via move() and
 * persists in order.json beside the preset files; presets not in the order
 * file (new saves, first run) fall back to name order after the ordered
 * ones. List order is user-facing truth: the browser, prev/next stepping
 * and MIDI program-change numbers all follow it.
 */
class PresetManager {
public:
  struct Info {
    juce::String id;
    juce::String name;
    juce::String category;
    bool favorite{false};
    bool factory{false};
  };

  static constexpr const char* kFileExtension = ".t3kpreset";
  static constexpr const char* kPresetTag = "T3KPreset";
  static constexpr int kMaxCategoryNameLength = 50;

  PresetManager();

  /** Store presets under an explicit base directory (tests use a temp dir).
      `systemFactory` stands in for the installer-shipped Factory dir; the
      default keeps temp stores isolated from presets installed on the
      machine. */
  explicit PresetManager(const juce::File& baseDir, const juce::File& systemFactory = {});

  /** All presets, user first then factory, each section sorted by name. */
  std::vector<Info> list() const;

  /** Full preset tree for an id, or an invalid tree when missing/corrupt. */
  juce::ValueTree load(const juce::String& id) const;

  /** Store a preset under `name`. A user preset with the same name is
      overwritten in place (same id); that's the "update" path, since the
      save popover is the only write UI. Returns the resulting Info, or an
      empty-id Info on IO failure. */
  Info save(const juce::String& name, juce::ValueTree preset) const;

  /** Rename a user preset (rewrites the name inside the file). */
  bool rename(const juce::String& id, const juce::String& newName) const;

  /** Delete a user preset. Factory presets are refused. */
  bool remove(const juce::String& id) const;

  /** Move a preset by `delta` steps within its section (negative = earlier).
      Clamped to the factory/user boundary so the browser's sections and the
      global order can't disagree. Persists the whole current order. */
  bool move(const juce::String& id, int delta) const;

  /** User categories, sorted alphabetically (case-insensitive). */
  juce::StringArray listCategories() const;

  /** Add a user category (trimmed, non-empty, max 50 chars, case-insensitive unique). */
  bool addCategory(const juce::String& name) const;

  /** Delete a category. Existing presets in this category are moved to root (""). */
  bool deleteCategory(const juce::String& name) const;

  /** Assign a user preset to a category (empty string = root "Your Presets"). */
  bool setPresetCategory(const juce::String& id, const juce::String& category) const;

  /** Move multiple presets to a category. */
  bool movePresetsToCategory(const juce::StringArray& ids, const juce::String& category) const;

  /** Set favourite / starred status for a preset. */
  bool setPresetFavorite(const juce::String& id, bool isFavorite) const;

  /** Set favourite / starred status for multiple presets. */
  bool setPresetsFavorite(const juce::StringArray& ids, bool isFavorite) const;

  /** Duplicate a user preset with "Copy-" prepended to the name. */
  Info duplicatePreset(const juce::String& id) const;

  /** Duplicate multiple user presets with "Copy-" prepended. */
  std::vector<Info> duplicatePresets(const juce::StringArray& ids) const;

  /** Delete multiple user presets. */
  bool removePresets(const juce::StringArray& ids) const;

private:
  juce::File fileForId(const juce::String& id) const;
  static juce::File defaultSystemFactoryDir();
  static juce::ValueTree readPresetFile(const juce::File& file);
  static bool writePresetFile(const juce::File& file, const juce::ValueTree& preset);

  juce::File orderFile() const;
  juce::StringArray readOrder() const;
  bool writeOrder(const juce::StringArray& ids) const;

  juce::File categoriesFile() const;
  juce::StringArray readCategories() const;
  bool writeCategories(const juce::StringArray& categories) const;

  juce::File favoritesFile() const;
  juce::StringArray readFavorites() const;
  bool writeFavorites(const juce::StringArray& ids) const;

  juce::File userDir;
  juce::File factoryDir;        // user-local Factory/ (dev drops, Linux install)
  juce::File systemFactoryDir;  // installer-shipped Factory/ (invalid when absent)
};
