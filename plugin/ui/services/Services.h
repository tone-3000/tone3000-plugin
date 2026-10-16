// Everything the views share, owned by the editor for its lifetime. Views
// take a `Services&`, subscribe to the stores they need and call their
// actions; nothing here is global, so two editors in one process (two plugin
// instances) never see each other's state.
#pragma once

#include "AudioDeviceStore.h"
#include "AutoMeasure.h"
#include "Banners.h"
#include "BrowserState.h"
#include "ChainStore.h"
#include "ConnectionGate.h"
#include "HintBus.h"
#include "ImageLoader.h"
#include "LibraryStore.h"
#include "LocalFiles.h"
#include "MeterStore.h"
#include "MidiMapStore.h"
#include "ModelLoads.h"
#include "Pointer.h"
#include "PresetStore.h"
#include "Toast.h"
#include "T3kConfig.h"
#include "ToneLoadFlow.h"
#include "ToneSession.h"
#include "UiClock.h"
#include "UiPrefs.h"
#include "UpdateCheck.h"
#include "Zoom.h"
#include "backend/Backend.h"

namespace t3k::ui {

// What the window around the root provides (NativeEditor in the plugin, the
// testbed window in the testbed).
class Shell {
public:
  virtual ~Shell() = default;
  // Chrome strips (banner, hint bar) grow the window instead of squishing the
  // core UI; `persistent` is the part remembered for the next session.
  virtual void setExtraContentHeight(int total, int persistent) = 0;
};

class Services {
public:
  // `prefs` outlives the services (the session persists its tokens there,
  // so the editor owns it first). `updateNotice` is the build's
  // T3K_UPDATE_NOTICE unless a shell (the testbed) decides otherwise.
  Services(Backend& b, ToneSession& t, Shell& s, UiPrefs& p, bool updateNotice = config::kUpdateNoticeEnabled)
      : backend(b),
        session(t),
        shell(s),
        prefs(p),
        hints(prefs),
        chain(b, clock),
        meters(b, clock),
        presets(b, chain),
        audioDevice(b),
        banners(audioDevice, prefs),
        midiMap(b),
        autoBalance(b, toast, AutoMeasure::Kind::balance),
        autoAlign(b, toast, AutoMeasure::Kind::align),
        localFiles(chain, toast),
        modelLoads(chain, t),
        connection(t),
        loadFlow(chain, connection, t),
        updates(t, prefs, b.pluginVersion(), updateNotice),
        library(b, chain, presets, t, connection, prefs, toast) {}

  Backend& backend;
  ToneSession& session;
  Shell& shell;
  UiPrefs& prefs;
  UiClock clock;  // before the stores and feeds that subscribe to it
  HintBus hints;
  ChainStore chain;
  MeterStore meters;
  PresetStore presets;
  AudioDeviceStore audioDevice;
  BannerStore banners;
  MidiMapStore midiMap;
  Toast toast;
  AutoMeasure autoBalance;
  AutoMeasure autoAlign;
  ImageLoader images;
  BrowserState browser;
  LocalFiles localFiles;
  ModelLoads modelLoads;
  ConnectionGate connection;
  ToneLoadFlow loadFlow;
  UpdateCheck updates;
  Zoom zoom;
  Pointer pointer;
  LibraryStore library;
};

}  // namespace t3k::ui
