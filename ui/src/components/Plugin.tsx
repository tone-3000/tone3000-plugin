import React, { useState, useCallback, useEffect, useMemo, useRef } from 'react';
import { Undo2, Redo2 } from 'lucide-react';
import { useNativeFunction } from '../hooks/useFunction';
import { useChainState } from '../hooks/useChainState';
import { ChainActionsProvider } from '../hooks/useChainActions';
import type { ChainActions } from '../hooks/useChainActions';
import { usePresets } from '../hooks/usePresets';
import { ChainView } from './ChainView';
import { Faceplate } from './Faceplate';
import { HintBar, HINT_HEIGHT } from './HintBar';
import { PresetBar } from './PresetBar';
import { StereoModeToggle } from './StereoModeToggle';
import { IconButton } from './IconButton';
import { HELP, useHintsEnabled } from './helpText';
import { BORDER } from './theme';
import { useParameter } from '../hooks/useParameter';
import type { Model, Tone, User } from '../types/tone';
import { AccountMenu } from './AccountMenu';
import type { ChainSide, ToneBlock } from '../types/chain';
import Settings, { type SettingsTab } from './Settings';
import { useAudioDevice } from '../hooks/useAudioDevice';
import { AppBanner, BANNER_HEIGHT, useAppBanner, type BannerAction } from './AppBanner';
import { DbMeter } from './DbMeter';
import { TunerView } from './TunerView';
import { useT3kSelect, shouldRestoreToneBrowser } from '../hooks/useT3kSelect';
import { useInternetGate } from '../hooks/useInternetGate';
import { T3K_API, T3K_ARCHITECTURE } from '../t3k/config';
import { OAuthOverlay } from './OAuthOverlay';
import { OfflineModal } from './OfflineModal';
import { ToneBrowser } from './ToneBrowser';
import { UpdateNotice } from './UpdateNotice';
import { useUpdateNotice } from '../hooks/useUpdateNotice';

// Swap targets must survive the Select flow's full-page OAuth redirect (the
// webview navigates to tone3000.com and back, remounting React), so the
// pending swap block id lives in sessionStorage rather than component state.
const SWAP_STORAGE_KEY = 't3k.pendingSwapBlockId';
// Same story for adds: the insert slot the user clicked, so the picked tone
// lands in that slot. Native falls back gracefully when the id went stale.
const INSERT_TARGET_STORAGE_KEY = 't3k.pendingInsertBlockId';
// Signed-in identity, cached so the header avatar/name paint instantly on
// relaunch instead of waiting for the getUser round trip; overwritten with
// fresh data once that request resolves, cleared on logout.
const USER_CACHE_KEY = 't3k.cachedUser';

// Lucide has no tuning fork, so this mimics its 24x24 stroke style.
const TuningForkIcon: React.FC<{ size?: number }> = ({ size = 18 }) => (
  <svg
    width={size}
    height={size}
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    strokeWidth="2"
    strokeLinecap="round"
    strokeLinejoin="round"
  >
    <path d="M8 3v7a4 4 0 0 0 8 0V3" />
    <line x1="12" y1="14" x2="12" y2="21" />
  </svg>
);

export const Plugin: React.FC = () => {
  const [showSettings, setShowSettings] = useState(false);
  // Which tab Settings opens on — banner actions land directly on System.
  const settingsTabRef = useRef<SettingsTab>('plugin');
  const [showTuner, setShowTuner] = useState(false);
  // In-plugin tone browser takeover (streams of TONE3000 tones). Opened by
  // the + when already authenticated, or right after the no-prompt login
  // flow returns. Seeded true when we're returning from a browse-intent
  // redirect without a picked tone (Browse closed/canceled) so the browser is
  // already mounted under the busy scrim — no flash of the main chain first.
  const [showToneBrowser, setShowToneBrowser] = useState(shouldRestoreToneBrowser);

  // Chain state: revision-gated polling + mutation actions, owned by one hook.
  const {
    chain,
    chainRight,
    canUndo,
    canRedo,
    activePreset,
    stereoEnabled,
    stereoInput,
    inputMode,
    standalone,
    sampleRate,
    refresh,
    actions,
  } = useChainState();

  // Audio device state (standalone only): shared by the System Settings tab
  // and the app banner so both read the same snapshot.
  const audioDevice = useAudioDevice(standalone);

  // Internal presets. Mutations resync the chain state immediately (loading a
  // preset replaces the chain; saving/renaming changes the active preset).
  const presetStore = usePresets(refresh);

  // Mono-mode spread: while it's on the output stage is effectively stereo,
  // so the output meter + balance knob switch to their stereo forms.
  const [spreadEnabled] = useParameter('spreadEnabled', 'toggle');
  const stereoOutput = stereoEnabled || spreadEnabled;

  // One-shot native functions (stateless bindings — stable identities).
  const setAccessToken = useNativeFunction<boolean>('setAccessToken');
  const setTunerEnabled = useNativeFunction<boolean>('setTunerEnabled');
  const copyToClipboard = useNativeFunction<boolean>('copyToClipboard');
  const clearAuthCookies = useNativeFunction<boolean>('clearAuthCookies');
  const setExtraContentHeight = useNativeFunction<boolean>('setExtraContentHeight');

  const openSettings = useCallback((tab: SettingsTab) => {
    settingsTabRef.current = tab;
    setShowSettings(true);
  }, []);

  // App banner: one priority-picked banner over the audio device state
  // (standalone only). Both the banner (top) and the hint bar (bottom) are
  // chrome strips that grow the window rather than squish the 600px core — we
  // report their combined height to native whenever either toggles.
  const { banner, dismiss: dismissBanner } = useAppBanner(standalone ? audioDevice.state : null);
  const bannerVisible = banner !== null;
  const hintsVisible = useHintsEnabled();
  const extraHeight = (bannerVisible ? BANNER_HEIGHT : 0) + (hintsVisible ? HINT_HEIGHT : 0);
  useEffect(() => {
    setExtraContentHeight(extraHeight);
  }, [extraHeight, setExtraContentHeight]);

  const handleBannerAction = useCallback(
    (kind: BannerAction) => {
      if (kind === 'openSettings') openSettings('system');
      else if (kind === 'switchToAsio') audioDevice.actions.setDeviceType('ASIO');
      else if (kind === 'openMicSettings') audioDevice.actions.openMicSettings();
    },
    [audioDevice.actions, openSettings]
  );

  // Toggle the tuner screen; native only feeds the pitch detector while it's on.
  const handleToggleTuner = async (show: boolean) => {
    setShowTuner(show);
    await setTunerEnabled(show);
  };

  // Share: copy the tone's public TONE3000 page URL. Clipboard writes go
  // through native (webview clipboard APIs are unreliable in JUCE), with the
  // browser API as a dev-server fallback.
  const handleShareBlock = useCallback(
    async (block: ToneBlock): Promise<boolean> => {
      const url = `${T3K_API}/tones/${block.tone.id}`;
      const ok = await copyToClipboard(url);
      if (ok) return true;
      try {
        await navigator.clipboard.writeText(url);
        return true;
      } catch {
        return false;
      }
    },
    [copyToClipboard]
  );

  // Push the latest access token down to native so model downloads can attach
  // the Bearer header. Called both right after the OAuth Select flow and
  // again whenever T3KClient transparently refreshes the token.
  const pushAccessTokenToNative = useCallback(
    async (accessToken: string) => {
      await setAccessToken(accessToken);
    },
    [setAccessToken]
  );

  // Handle a fully-resolved tone (with embedded models) — from the Select
  // flow's callback or a card pick in the in-plugin tone browser. If a swap
  // was pending (user hit the swap button on a block before browsing),
  // replace that block in place; otherwise add the tone at the insert slot.
  const handleToneSelected = useCallback(
    async (tone: Tone & { models: Model[] }, accessToken: string) => {
      if (!tone.models || tone.models.length === 0) {
        console.error('Tone has no models');
        return;
      }

      // Consume the pending swap/insert targets up front so they can never
      // leak into a later selection. (Each flow clears the other's key before
      // starting; see handleAddModel / handleSwapBlock.)
      const swapBlockId = sessionStorage.getItem(SWAP_STORAGE_KEY);
      sessionStorage.removeItem(SWAP_STORAGE_KEY);
      const insertBlockId = sessionStorage.getItem(INSERT_TARGET_STORAGE_KEY);
      sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);

      // Make sure native has the freshest access token before it tries to
      // download the model from `model_url` (which now requires Bearer auth).
      await pushAccessTokenToNative(accessToken);

      const toneJson = JSON.stringify(tone);

      // A tone landed — return to the main chain view.
      setShowToneBrowser(false);

      if (swapBlockId) {
        console.log('Swapping tone into block', swapBlockId, ':', tone.title);
        const swapped = await actions.swapTone(swapBlockId, toneJson);
        if (swapped) return;
        console.warn('Swap target no longer exists; adding tone as a new block');
      }

      console.log('Loading tone:', tone.title);
      const blockId = await actions.loadTone(toneJson, insertBlockId ?? undefined);
      if (!blockId) console.error('Failed to load tone');
    },
    [actions, pushAccessTokenToNative]
  );

  // TONE3000 integration. Two single-webview redirect flows share one
  // callback: the no-prompt login (from + while signed out) comes back with
  // tokens only and opens the in-plugin tone browser; the full-catalog
  // Select flow (browser CTA / swap) also carries the picked tone_id.
  const {
    client: t3kClient,
    startSelectFlow,
    startLoginFlow,
    selectToneById,
    retryFlow,
    oauthPhase,
    oauthError,
    clearOauthError,
  } = useT3kSelect({
    onToneSelected: handleToneSelected,
    onAccessTokenUpdated: pushAccessTokenToNative,
    onAuthenticated: () => setShowToneBrowser(true),
  });

  // Native downloads model files itself (add / swap / switch all fetch
  // `model_url` with a Bearer header) but only holds a copy of the access
  // token — the React client owns the session and its refresh token. The
  // token listener above pushes every new/refreshed token set as it happens;
  // ensureNativeAuth() is the guarantee on top: it validates the token
  // (refreshing when near expiry) and awaits the push, so a native download
  // can never start against a stale Bearer.
  const ensureNativeAuth = useCallback(async () => {
    await pushAccessTokenToNative(await t3kClient.getAccessToken());
  }, [pushAccessTokenToNative, t3kClient]);

  // A remembered login (tokens read straight from localStorage on a fresh
  // webview) never fires the token listener, so native would otherwise sit
  // tokenless until the next OAuth return or refresh — sync once on mount.
  useEffect(() => {
    if (!t3kClient.isAuthenticated()) return;
    ensureNativeAuth().catch(() => {
      // Refresh token rejected — the next + / login flow re-authenticates.
    });
  }, [ensureNativeAuth, t3kClient]);

  // Signed-in identity for the header's account pill. Refreshed on mount
  // (tokens persist in localStorage) and after each OAuth return. Seeded from
  // the localStorage cache (only when a session is present) so the avatar +
  // name paint instantly on relaunch rather than after the getUser round trip.
  const [user, setUser] = useState<User | null>(() => {
    if (!t3kClient.isAuthenticated()) return null;
    try {
      const cached = localStorage.getItem(USER_CACHE_KEY);
      return cached ? (JSON.parse(cached) as User) : null;
    } catch {
      return null;
    }
  });
  useEffect(() => {
    if (oauthPhase !== 'idle' || !t3kClient.isAuthenticated()) return;
    let cancelled = false;
    t3kClient
      .getUser()
      .then((u) => {
        if (cancelled) return;
        setUser(u);
        // Overwrite the cache with the freshly-resolved identity.
        try {
          localStorage.setItem(USER_CACHE_KEY, JSON.stringify(u));
        } catch {
          // Cache is a nicety; storage failures are non-fatal.
        }
      })
      .catch(() => {
        // Avatar is decorative — auth failures surface via the flows.
      });
    return () => {
      cancelled = true;
    };
  }, [oauthPhase, t3kClient]);

  // Logout clears auth everywhere: the API client's persisted tokens plus
  // any mid-flight PKCE state (React app / OAuth), native's copy of the
  // access token (backend model downloads), and the webview's tone3000.com
  // session cookies — without the last one, the next OAuth redirect would
  // silently re-approve on the still-live site session and it would look
  // like logout never happened.
  const handleLogout = useCallback(async () => {
    t3kClient.logout();
    sessionStorage.removeItem(SWAP_STORAGE_KEY);
    sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
    try {
      localStorage.removeItem(USER_CACHE_KEY);
    } catch {
      // Non-fatal — the stale cache is only read when a session is present.
    }
    setUser(null);
    setShowToneBrowser(false);
    await Promise.all([pushAccessTokenToNative(''), clearAuthCookies()]);
  }, [clearAuthCookies, pushAccessTokenToNative, t3kClient]);

  // Switch a block's model. Native downloads the new model file itself, so
  // refresh-and-sync the token first — switching after the editor has been
  // sitting idle is exactly when the last-pushed token has expired.
  const handleSwitchModel = useCallback(
    async (blockId: string, modelId: number, model: Model) => {
      try {
        await ensureNativeAuth();
      } catch (err) {
        // Refresh token rejected: tokens are cleared, the model select
        // disables itself on the next render, and the next + re-authenticates.
        console.error('Cannot switch model — TONE3000 session expired', err);
        return;
      }
      const success = await actions.switchModel(blockId, modelId, JSON.stringify(model));
      if (!success) console.error('Failed to switch model');
    },
    [actions, ensureNativeAuth]
  );

  // Retry a failed model download. Refresh the token first when signed in
  // (the failure may have left the block waiting long enough for the last
  // pushed token to expire); signed out we retry anyway — legacy public
  // model URLs still work anonymously.
  const handleRetryLoad = useCallback(
    async (blockId: string) => {
      if (t3kClient.isAuthenticated()) {
        try {
          await ensureNativeAuth();
        } catch {
          // Session expired — the retry below still runs; native falls back
          // to whatever token it holds.
        }
      }
      await actions.retryModelLoad(blockId);
    },
    [actions, ensureNativeAuth, t3kClient]
  );

  // Fetch a tone's full model catalog for the detail card's picker (tones max
  // out at 300 models, so one call covers it). The persisted block only
  // carries the active model; NAM keeps the v2-architecture filter — all the
  // plugin loads.
  const handleListToneModels = useCallback(
    async (toneId: number, format: string | undefined) => {
      const isNam = format?.toLowerCase() === 'nam';
      const res = await t3kClient.listModels(toneId, {
        pageSize: 300,
        ...(isNam && T3K_ARCHITECTURE !== undefined ? { architecture: T3K_ARCHITECTURE } : {}),
      });
      return res.data;
    },
    [t3kClient]
  );

  // First line of defence for network-dependent actions: an instant
  // `navigator.onLine` check (no probe, no latency). It only catches the
  // "no interface up" case — a connected-but-dead network still gets through
  // and lands on the recovery paths (failed-navigation recovery, stream
  // retry, block retry).
  const internetGate = useInternetGate();
  const { requireInternet } = internetGate;

  // Non-blocking update check (enabled via VITE_T3K_UPDATE_NOTICE); also
  // resolves the running build's version for the Settings footer.
  const { notice: updateNotice, update, localVersion, remindLater } = useUpdateNotice();

  // Adding routes to a lane via the native active-edit side (it has to
  // survive the OAuth redirect, so it lives in native state, not React's).
  // Signed in: straight to the in-plugin tone browser. Signed out: run the
  // no-prompt login flow first; its callback opens the browser.
  const handleAddModel = useCallback(
    (side: ChainSide, insertBlockId: string) => {
      requireInternet(async () => {
        sessionStorage.removeItem(SWAP_STORAGE_KEY);
        // The clicked slot; the active side stays the native-state fallback
        // for when the slot id goes stale (e.g. undone away mid-flow).
        sessionStorage.setItem(INSERT_TARGET_STORAGE_KEY, insertBlockId);
        if (stereoEnabled) await actions.setActiveSide(side);
        if (t3kClient.isAuthenticated()) setShowToneBrowser(true);
        else startLoginFlow({ openBrowser: true });
      });
    },
    [actions, requireInternet, startLoginFlow, stereoEnabled, t3kClient]
  );

  // Swap: remember the target block, then run the same browse flow as add —
  // the pending swap id is consumed when the picked tone lands.
  const handleSwapBlock = useCallback(
    (blockId: string) => {
      requireInternet(() => {
        sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
        sessionStorage.setItem(SWAP_STORAGE_KEY, blockId);
        if (t3kClient.isAuthenticated()) setShowToneBrowser(true);
        else startLoginFlow({ openBrowser: true });
      });
    },
    [requireInternet, startLoginFlow, t3kClient]
  );

  // Auth-dependent block actions (model switching) key off this. Reading
  // localStorage per render is fine — every login/logout transition already
  // re-renders Plugin (user / oauthPhase state), refreshing the value.
  const authenticated = t3kClient.isAuthenticated();

  // Single stable bundle of everything a block can do — ChainView and the
  // tiles/cards below it read this from context instead of threading a dozen
  // callback props (which would defeat their React.memo).
  const chainActions = useMemo<ChainActions>(
    () => ({
      addModel: handleAddModel,
      removeBlock: actions.removeBlock,
      swapBlock: handleSwapBlock,
      shareBlock: handleShareBlock,
      reorderBlocks: actions.reorderBlocks,
      moveBlock: actions.moveBlockToChain,
      swapChains: actions.swapChains,
      switchModel: handleSwitchModel,
      retryLoad: handleRetryLoad,
      listToneModels: handleListToneModels,
      setBlockParam: actions.setBlockParam,
      setBlockEqBand: actions.setBlockEqBand,
      setBlockEqEnabled: actions.setBlockEqEnabled,
      setBlockEqPre: actions.setBlockEqPre,
      resetBlockEq: actions.resetBlockEq,
      authenticated,
    }),
    [
      actions,
      authenticated,
      handleAddModel,
      handleListToneModels,
      handleRetryLoad,
      handleShareBlock,
      handleSwapBlock,
      handleSwitchModel,
    ]
  );

  return (
    <div
      style={{
        position: 'relative',
        width: '100%',
        maxWidth: '100%',
        // The window grows by the chrome-strip height (see setExtraContentHeight
        // above), so the 600px core UI between them keeps its full space.
        height: `${600 + extraHeight}px`,
        display: 'flex',
        flexDirection: 'column',
        backgroundColor: '#000000',
        boxSizing: 'border-box',
        overflow: 'hidden',
        color: '#ffffff',
      }}
    >
      {banner && (
        <AppBanner banner={banner} onAction={handleBannerAction} onDismiss={dismissBanner} />
      )}

      {/* Header with T3K Logo and Settings - Full Width */}
      <div
        style={{
          width: '100%',
          height: '64px',
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          backgroundColor: '#000000',
          padding: '0 24px',
          boxSizing: 'border-box',
          borderBottom: BORDER,
        }}
      >
        <a
          href="https://www.tone3000.com"
          target="_blank"
          rel="noopener noreferrer"
          style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 16 }}
        >
          <img
            src="/t3k.svg"
            alt="T3K"
            style={{
              width: '160px',
            }}
          />
          {/* <img
            src="/beta.svg"
            alt="Beta"
            style={{
              height: '12px',
            }}
          /> */}
        </a>
        {/* 40px between header items; tight pairs (undo/redo) group inside. */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '40px' }}>
          <PresetBar
            active={activePreset}
            presets={presetStore.presets}
            onSave={presetStore.actions.save}
            onLoad={presetStore.actions.load}
            onRename={presetStore.actions.rename}
            onDelete={presetStore.actions.remove}
            onMove={presetStore.actions.move}
          />
          <StereoModeToggle
            stereoEnabled={stereoEnabled}
            onToggle={(enabled) => actions.setStereoMode(enabled)}
          />
          <IconButton
            onClick={() => handleToggleTuner(!showTuner)}
            help={HELP.tuner}
            active={showTuner}
            fillWhenActive
            size={28}
          >
            <TuningForkIcon size={18} />
          </IconButton>
          <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
            <IconButton
              onClick={() => actions.undo()}
              disabled={!canUndo}
              help={HELP.undo}
              size={28}
            >
              <Undo2 size={18} />
            </IconButton>
            <IconButton
              onClick={() => actions.redo()}
              disabled={!canRedo}
              help={HELP.redo}
              size={28}
            >
              <Redo2 size={18} />
            </IconButton>
          </div>
          <AccountMenu
            user={user}
            authenticated={authenticated}
            onOpenSettings={() => openSettings('plugin')}
            onLogin={() => requireInternet(() => startLoginFlow())}
            onLogout={handleLogout}
          />
        </div>
      </div>

      {/* Middle Section: Tuner (when toggled on) or Meters + Chain View */}
      {showTuner ? (
        <TunerView onClose={() => handleToggleTuner(false)} />
      ) : (
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            flex: 1,
            width: '100%',
            backgroundColor: '#000000',
            overflow: 'hidden',
            minHeight: 0,
            padding: '0 24px',
          }}
        >
          {/* Left Meter - Input */}
          <div
            style={{
              height: '100%',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              flexShrink: 0,
              backgroundColor: '#000000',
            }}
          >
            {/* 368px yields 23 dots (6px dot + 10px gap); wrapper centers vertically.
                A mono input mode folds the source down, so the meter collapses too. */}
            <DbMeter type="input" stereo={stereoInput && inputMode === 'stereo'} height={368} />
          </div>

          {/* Chain View - Center (gallery lanes scroll horizontally inside).
              Vertical padding is a minimum gap only (lanes center themselves);
              it must stay small enough that the 376px stereo stack fits in the
              middle section even with the hint bar showing. */}
          <div
            style={{
              flex: 1,
              height: '100%',
              overflow: 'hidden',
              minHeight: 0,
              minWidth: 0,
              boxSizing: 'border-box',
            }}
          >
            {showToneBrowser ? (
              <ToneBrowser
                client={t3kClient}
                // Pre-mounted during an OAuth return ('returning'), the client
                // has no tokens until the callback's code exchange finishes —
                // hold the stream fetch so it doesn't fire unauthenticated.
                authPending={oauthPhase === 'returning'}
                onPickTone={selectToneById}
                onBrowseTone3000={startSelectFlow}
                onClose={() => {
                  // Closing without picking abandons any pending swap/insert target.
                  sessionStorage.removeItem(SWAP_STORAGE_KEY);
                  sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
                  setShowToneBrowser(false);
                }}
              />
            ) : (
              <ChainActionsProvider value={chainActions}>
                <ChainView
                  chain={chain}
                  chainRight={stereoEnabled ? (chainRight ?? []) : null}
                  sampleRate={sampleRate}
                />
              </ChainActionsProvider>
            )}
          </div>

          {/* Right Meter - Output */}
          <div
            style={{
              height: '100%',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              flexShrink: 0,
              backgroundColor: '#000000',
            }}
          >
            <DbMeter type="output" stereo={stereoOutput} height={368} labelsPosition="right" />
          </div>
        </div>
      )}

      {/* Pinned faceplate at the bottom (gains, gate, tone stack), with the
          hint strip under it (hidden entirely when hints are off). */}
      <Faceplate
        stereoOutput={stereoOutput}
        stereoChains={stereoEnabled}
        stereoInput={stereoInput}
        inputMode={inputMode}
        onInputModeChange={actions.setInputMode}
      />
      <HintBar />

      {/* Settings takeover — mounted only while open so its parameter
          subscriptions and screen state don't run behind the main UI. */}
      {showSettings && (
        <Settings
          onClose={() => setShowSettings(false)}
          standalone={standalone}
          device={audioDevice}
          initialTab={settingsTabRef.current}
          version={localVersion}
          update={update}
        />
      )}

      {/* OAuth callback overlay — covers the chain UI while we resolve the
          tokens + tone after returning from tone3000.com, and surfaces any
          OAuth error (callback failures, failed-navigation recovery) with a
          retry that restarts whichever flow actually failed. */}
      <OAuthOverlay
        phase={oauthPhase}
        error={oauthError}
        onRetry={retryFlow}
        onDismiss={clearOauthError}
      />

      {/* Offline gate for internet-dependent actions (add / swap / login). */}
      <OfflineModal
        open={internetGate.offlineModalOpen}
        onRetry={internetGate.retry}
        onDismiss={internetGate.dismiss}
      />

      {/* Update available — below OAuth/offline (z 3000) so those always win. */}
      <UpdateNotice notice={updateNotice} onRemindLater={remindLater} />
    </div>
  );
};
