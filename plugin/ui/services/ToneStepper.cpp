#include "ToneStepper.h"

#include <algorithm>

#include "model/ToneQuery.h"

namespace t3k::ui {

namespace {

const juce::Array<juce::var>* tonesOf(const juce::var& context) { return context["tones"].getArray(); }

int intOf(const juce::var& context, const char* key, int fallback) {
  return static_cast<int>(context.getProperty(key, fallback));
}

// NAM tones without an A2 model can't load (ToneCard::unavailable); a step
// skips over them.
bool loadable(const juce::var& tone) {
  return !tone["format"].toString().equalsIgnoreCase("nam") || static_cast<bool>(tone.getProperty("a2", true));
}

juce::var withIndex(const juce::var& context, int index) {
  auto copy = context.clone();
  if (auto* o = copy.getDynamicObject()) o->setProperty("index", index);
  return copy;
}

}  // namespace

ToneStepper::ToneStepper(ChainStore& chain, ToneSession& session, ConnectionGate& connection, Toast& toast)
    : chain_(chain), session_(session), connection_(connection), toast_(toast) {}

ToneStepper::~ToneStepper() = default;

juce::var ToneStepper::makeContext(const juce::var& query, const TonePage& page, int pageSize, int index) {
  juce::Array<juce::var> tones;
  for (const auto& t : page.data) {
    juce::DynamicObject::Ptr row = new juce::DynamicObject();
    row->setProperty("id", t.id);
    row->setProperty("title", t.title);
    row->setProperty("gear", t.gear);
    row->setProperty("format", t.format);
    row->setProperty("a2", !t.isNam() || t.a2ModelsCount > 0);
    tones.add(juce::var(row.get()));
  }
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("query", query);
  o->setProperty("page", page.page);
  o->setProperty("pageSize", pageSize);
  o->setProperty("totalPages", page.totalPages);
  o->setProperty("index", index);
  o->setProperty("tones", tones);
  return juce::var(o.get());
}

juce::var ToneStepper::contextOf(const std::string& blockId) const {
  if (const auto it = pending_.find(blockId); it != pending_.end()) return it->second.context;
  const auto* item = chain_.state().findBlock(blockId);
  return item != nullptr && item->isTone() ? item->browse : juce::var();
}

std::optional<ToneStepper::Cursor> ToneStepper::cursor(const std::string& blockId) const {
  const auto context = contextOf(blockId);
  const auto* tones = tonesOf(context);
  if (tones == nullptr || tones->isEmpty()) return std::nullopt;

  const auto it = pending_.find(blockId);
  const int index = it != pending_.end() ? it->second.index : intOf(context, "index", 0);
  const int page = intOf(context, "page", 1);
  const int totalPages = intOf(context, "totalPages", 1);
  const int pageSize = juce::jmax(1, intOf(context, "pageSize", tones->size()));

  Cursor c;
  c.position = (page - 1) * pageSize + index + 1;
  // The last page's length is unknown until it is fetched; assume full.
  c.total = page == totalPages ? (page - 1) * pageSize + tones->size() : totalPages * pageSize;
  c.canPrev = index > 0 || page > 1;
  c.canNext = index < tones->size() - 1 || page < totalPages;
  c.busy = it != pending_.end();
  return c;
}

void ToneStepper::setFocusBlock(const std::string& blockId) {
  if (focus_ == blockId) return;
  focus_ = blockId;
  notify();
}

bool ToneStepper::stepFocused(int delta) {
  // No focus yet (or it went stale): the first block with a cursor.
  if (!cursor(focus_) && !moveFocus(+1)) return false;
  step(focus_, delta);
  return true;
}

bool ToneStepper::moveFocus(int direction) {
  std::vector<std::string> ids;
  for (const auto* item : chain_.state().toneBlocks())
    if (cursor(item->blockId)) ids.push_back(item->blockId);
  if (ids.empty()) return false;
  const auto it = std::find(ids.begin(), ids.end(), focus_);
  if (it == ids.end()) {
    setFocusBlock(direction >= 0 ? ids.front() : ids.back());
    return true;
  }
  const auto n = static_cast<int>(ids.size());
  const int at = static_cast<int>(std::distance(ids.begin(), it));
  setFocusBlock(ids[static_cast<size_t>(((at + (direction >= 0 ? 1 : -1)) % n + n) % n)]);
  return true;
}

void ToneStepper::step(const std::string& blockId, int delta) {
  if (delta == 0) return;
  setFocusBlock(blockId);
  const auto context = contextOf(blockId);
  const auto* tones = tonesOf(context);
  if (tones == nullptr || tones->isEmpty()) return;

  const auto it = pending_.find(blockId);
  int index = it != pending_.end() ? it->second.index : intOf(context, "index", 0);
  const int dir = delta > 0 ? 1 : -1;
  for (int moved = 0; moved < std::abs(delta);) {
    index += dir;
    if (index < 0 || index >= tones->size()) break;
    if (loadable((*tones)[index])) ++moved;
  }

  if (index >= 0 && index < tones->size()) {
    resolve(blockId, context, index);
    return;
  }
  const int page = intOf(context, "page", 1);
  const int totalPages = intOf(context, "totalPages", 1);
  if (dir > 0 && page < totalPages) return fetchPage(blockId, context, page + 1, false);
  if (dir < 0 && page > 1) return fetchPage(blockId, context, page - 1, true);
  toast_.show(dir > 0 ? "Last result" : "First result");
}

void ToneStepper::fetchPage(const std::string& blockId, const juce::var& context, int page, bool fromEnd) {
  const int pageSize = intOf(context, "pageSize", 12);
  const auto query = context["query"];
  auto& p = pending_[blockId];
  const int generation = ++p.generation;
  p.context = context;
  notify();
  connection_.requireConnection([this, blockId, query, page, pageSize, fromEnd, generation] {
    session_.searchTones(
        ToneQuery::fromVar(query), page, pageSize,
        scope_.wrap([this, blockId, query, pageSize, fromEnd, generation](Result<TonePage> r) {
          const auto it = pending_.find(blockId);
          if (it == pending_.end() || it->second.generation != generation) return;
          if (!r || r->data.empty()) {
            pending_.erase(it);
            notify();
            toast_.show(r ? juce::String("No more results") : r.error);
            return;
          }
          auto next = makeContext(query, *r, pageSize, 0);
          const auto& tones = *tonesOf(next);
          // First loadable from the near end of the new page.
          int index = fromEnd ? tones.size() - 1 : 0;
          while (index >= 0 && index < tones.size() && !loadable(tones[index])) index += fromEnd ? -1 : 1;
          if (index < 0 || index >= tones.size()) {
            pending_.erase(it);
            notify();
            return;
          }
          resolve(blockId, next, index);
        }));
  });
}

void ToneStepper::resolve(const std::string& blockId, juce::var context, int index) {
  auto& p = pending_[blockId];
  const int generation = ++p.generation;
  p.index = index;
  p.context = context;
  notify();

  const int toneId = static_cast<int>((*tonesOf(context))[index]["id"]);
  connection_.requireConnection([this, blockId, toneId, context, index, generation] {
    session_.resolveTone(toneId, scope_.wrap([this, blockId, context, index, generation](Result<Tone> tone) {
      const auto it = pending_.find(blockId);
      if (it == pending_.end() || it->second.generation != generation) return;  // a newer press won
      pending_.erase(it);
      if (!tone || tone->models.empty()) {
        notify();
        toast_.show(tone ? juce::String("That tone has no loadable model") : tone.error);
        return;
      }
      if (!chain_.swapTone(blockId, tone->toJson(), withIndex(context, index)))
        toast_.show("That block is gone");
      notify();
    }));
  });
}

void ToneStepper::notify() {
  listeners_.call([](Listener& l) { l.stepperChanged(); });
}

}  // namespace t3k::ui
