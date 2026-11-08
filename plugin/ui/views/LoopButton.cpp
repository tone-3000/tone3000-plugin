#include "LoopButton.h"

#include "core/Help.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

help::Key helpFor(LoopFeed::Phase phase) {
  switch (phase) {
    case LoopFeed::Phase::armed: return help::Key::loopArmed;
    case LoopFeed::Phase::recording: return help::Key::loopRecording;
    case LoopFeed::Phase::playing: return help::Key::loopPlaying;
    case LoopFeed::Phase::stopped: return help::Key::loopStopped;
    case LoopFeed::Phase::empty: break;
  }
  return help::Key::loopEmpty;
}

}  // namespace

LoopButton::LoopButton(LoopFeed& feed) : Clickable({}), feed_(feed) {
  setSize(kBox, kBox);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  sync();
  feed_.addListener(this);
}

LoopButton::~LoopButton() { feed_.removeListener(this); }

void LoopButton::loopChanged(const LoopFeed::State&) { sync(); }

void LoopButton::sync() {
  setHelpText(help::text(helpFor(feed_.state().phase)));
  repaint();
}

void LoopButton::clicked(const juce::ModifierKeys& modifiers) {
  if (modifiers.isPopupMenu()) feed_.clear();
  else feed_.tap();
}

void LoopButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
  using Phase = LoopFeed::Phase;
  const auto& s = feed_.state();
  const auto box = getLocalBounds().toFloat();
  const auto c = box.getCentre();
  const float alpha = isEnabled() ? 1.0f : theme::kDisabledOpacity;

  // The accent: red while a take is pending or being made, blue once there
  // is a loop. The track under the arc is the accent, faint; empty has no
  // accent and a subtle track.
  const bool red = s.phase == Phase::armed || s.phase == Phase::recording;
  const bool blue = s.phase == Phase::playing || s.phase == Phase::stopped;
  const auto accent = (red ? theme::kBrandRed : blue ? theme::kBrandBlue : theme::kGray).withMultipliedAlpha(alpha);
  const auto track = (s.phase == Phase::empty ? theme::kSubtle : accent.withMultipliedAlpha(0.3f))
                         .withMultipliedAlpha(highlighted ? 1.4f : 1.0f);

  juce::Path ring;
  ring.addEllipse(c.x - kRingRadius, c.y - kRingRadius, kRingRadius * 2, kRingRadius * 2);
  g.setColour(track);
  g.strokePath(ring, juce::PathStrokeType(kRingStroke));

  // Armed: the whole ring, lit. Recording / playing / stopped: the arc from
  // twelve o'clock to the position.
  const double sweep = s.phase == Phase::armed ? 1.0 : (s.phase == Phase::empty ? 0.0 : s.position);
  if (sweep > 0.0) {
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, kRingRadius, kRingRadius, 0.0f, 0.0f,
                      static_cast<float>(sweep * juce::MathConstants<double>::twoPi), true);
    g.setColour(s.phase == Phase::stopped ? accent.withMultipliedAlpha(0.6f) : accent);
    g.strokePath(arc, juce::PathStrokeType(kArcStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }

  // The glyph: hollow circle (empty / armed), dot (recording), play
  // triangle (playing lit, stopped muted).
  switch (s.phase) {
    case Phase::empty:
    case Phase::armed: {
      juce::Path hollow;
      hollow.addEllipse(c.x - kDotRadius, c.y - kDotRadius, kDotRadius * 2, kDotRadius * 2);
      g.setColour(s.phase == Phase::armed ? accent : theme::kGray.withMultipliedAlpha(alpha));
      g.strokePath(hollow, juce::PathStrokeType(kRingStroke));
      break;
    }
    case Phase::recording:
      g.setColour(accent);
      g.fillEllipse(c.x - kDotRadius - 0.5f, c.y - kDotRadius - 0.5f, kDotRadius * 2 + 1, kDotRadius * 2 + 1);
      break;
    case Phase::playing:
    case Phase::stopped: {
      juce::Path play;
      const float h = kPlaySize, w = kPlaySize * 0.9f;
      // Optically centred: a triangle's centroid sits left of its box centre.
      const float x0 = c.x - w / 2 + 1.0f, y0 = c.y - h / 2;
      play.addTriangle(x0, y0, x0, y0 + h, x0 + w, c.y);
      g.setColour((s.phase == Phase::playing ? theme::kWhite : theme::kGray).withMultipliedAlpha(alpha));
      g.fillPath(play);
      break;
    }
  }
}

}  // namespace t3k::ui
