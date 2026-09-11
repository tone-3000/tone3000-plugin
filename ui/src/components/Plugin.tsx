import React, { useState, useCallback, useMemo, useRef } from 'react';
import { useNativeFunction } from '../hooks/useFunction';
import { useChainState } from '../hooks/useChainState';
import { ChainActionsProvider } from '../hooks/useChainActions';
import type { ChainActions } from '../hooks/useChainActions';
import { usePresets } from '../hooks/usePresets';
import { useParameter } from '../hooks/useParameter';
import { useAudioDevice } from '../hooks/useAudioDevice';
import { useConnectionGate } from '../hooks/useConnectionGate';
import { useToneSession } from '../hooks/useToneSession';
import { useToneLoadFlow } from '../hooks/useToneLoadFlow';
import { useUpdateNotice } from '../hooks/useUpdateNotice';
import { useUiScale, DESIGN_WIDTH, DESIGN_HEIGHT } from '../hooks/useUiScale';
import { shouldRestoreToneBrowser } from '../hooks/useT3kSelect';
import { ChainView, DETAIL_BLOCK_STORAGE_KEY } from './ChainView';
import { Faceplate, PLATE_HEIGHT } from './Faceplate';
import { HintBar, HINT_HEIGHT } from './HintBar';
import { ToastProvider } from './Toast';
import { PluginHeader } from './PluginHeader';
import { useHintsEnabled } from './helpText';
import { AppBanner, useAppBanner, type BannerAction } from './AppBanner';
import { useChromeChoreography, BANNER_ANIM_MS } from '../hooks/useChromeChoreography';
import { DbMeter } from './DbMeter';
import { TunerView } from './TunerView';
import { OAuthOverlay } from './OAuthOverlay';
import { ConnectionModal } from './ConnectionModal';
import { ToneBrowser } from './ToneBrowser';
import { UpdateNotice } from './UpdateNotice';
import Settings, { type SettingsTab } from './Settings';
import { T3K_API } from '../t3k/config';
import type { Model } from '../types/tone';
import type { ToneBlock } from '../types/chain';

export const Plugin: React.FC = () => {
  const [showSettings, setShowSettings] = useState(false);
  // Which tab Settings opens on; banner / gear land on System (setup first).
  const settingsTabRef = useRef<SettingsTab>('system');
  const [showTuner, setShowTuner] = useState(false);
  // In-plugin tone browser takeover (streams of TONE3000 tones). Opened by
  // the + when already authenticated, or right after the no-prompt login
  // flow returns. Seeded true when we're returning from a browse-intent
  // redirect without a picked tone (Browse closed/canceled) so the browser
  // is already mounted under the busy scrim; no flash of the main chain.
  const [showToneBrowser, setShowToneBrowser] = useState(shouldRestoreToneBrowser);
  // Block info view fills the center column to the header and faceplate so
  // scroll content isn't stopped by the 24px meter-band pads.
  const [fillToFaceplate, setFillToFaceplate] = useState(false);
  // Bumped on preset load so ChainView drops an open detail takeover (and a
  // remount after closing the tuner/browser doesn't restore it).
  const [returnToGallery, setReturnToGallery] = useState(0);

  // Chain state: revision-gated polling + mutation actions, owned by one hook.
  const {
    chain,
    chainRight,
    branch,
    canUndo,
    canRedo,
    canPaste,
    atDefault,
    activePreset,
    stereoEnabled,
    stereoInput,
    stereoOutput,
    inputMode,
    namSlimSizeDefault,
    multiCore,
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

  // The output carries a real stereo image only when a stereo-image feature
  // is on (stereo mode, or mono-mode spread) AND the rig can reproduce it
  // (stereoOutput: stereo host bus / 2+ channel output device); drives the
  // output meter's stereo form. On a mono rig, Spread is idle and greyed
  // out, while stereo chains keep running and native sums them to mono
  // (monoSum below): the Bal knob then trims the two chains *inside* the
  // sum, so it stays visible whenever stereo chains are on, regardless of
  // the rig (balanceActive).
  const [spreadEnabled] = useParameter('spreadEnabled', 'toggle');
  const stereoImage = (stereoEnabled || spreadEnabled) && stereoOutput;
  const balanceActive = stereoEnabled || (spreadEnabled && stereoOutput);
  const monoSum = stereoEnabled && !stereoOutput;

  const setTunerEnabled = useNativeFunction<boolean>('setTunerEnabled');
  const copyToClipboard = useNativeFunction<boolean>('copyToClipboard');
  const setExtraContentHeight = useNativeFunction<boolean>('setExtraContentHeight');
  const pickLocalToneFile = useNativeFunction<{
    blockId?: string;
    error?: string;
    cancelled?: boolean;
  }>('pickLocalToneFile');

  const openSettings = useCallback((tab: SettingsTab) => {
    settingsTabRef.current = tab;
    setShowSettings(true);
  }, []);
  const openDefaultSettings = useCallback(() => openSettings('system'), [openSettings]);

  // App banner: one priority-picked banner over the audio device state
  // (standalone only). Both the banner (top) and the hint bar (bottom) are
  // chrome strips that grow the window rather than squish the 578px core.
  const { banner, dismiss: dismissBanner } = useAppBanner(standalone ? audioDevice.state : null);
  // Whole-UI proportional scaling: keeps the root font-size at the current
  // scale so the rem-denominated design space below tracks the window.
  useUiScale();
  const hintsVisible = useHintsEnabled();
  // Chrome choreography: reports the strip heights to native (before paint)
  // and sequences the banner mount against the window resize so existing
  // content never jumps; see useChromeChoreography for the phase machine.
  const chrome = useChromeChoreography(banner, hintsVisible, setExtraContentHeight);

  const handleBannerAction = useCallback(
    (kind: BannerAction) => {
      if (kind === 'openSettings') openSettings('system');
      else if (kind === 'switchToAsio') audioDevice.actions.setDeviceType('ASIO');
      else if (kind === 'openMicSettings') audioDevice.actions.openMicSettings();
    },
    [audioDevice.actions, openSettings]
  );

  // Toggle the tuner screen; native only feeds the pitch detector while it's on.
  const handleToggleTuner = useCallback(
    async (show: boolean) => {
      setShowTuner(show);
      await setTunerEnabled(show);
    },
    [setTunerEnabled]
  );
  const closeTuner = useCallback(() => handleToggleTuner(false), [handleToggleTuner]);

  // Top-bar actions whose effect lands on the main screen (stereo mode,
  // undo/redo, loading or saving a preset) leave the tuner first, so the
  // result is visible instead of hidden behind the tuner takeover.
  const closeTunerThen = useCallback(
    <A extends unknown[], R>(fn: (...args: A) => R) =>
      (...args: A): R => {
        if (showTuner) void handleToggleTuner(false);
        return fn(...args);
      },
    [showTuner, handleToggleTuner]
  );
  const handleStereoToggle = useMemo(
    () => closeTunerThen(actions.setStereoMode),
    [closeTunerThen, actions]
  );
  const handleUndo = useMemo(() => closeTunerThen(actions.undo), [closeTunerThen, actions]);
  const handleRedo = useMemo(() => closeTunerThen(actions.redo), [closeTunerThen, actions]);

  // Share: copy the tone's public TONE3000 page URL, the API's canonical
  // `url` (title slug + id). The plain id path is a fallback for summaries
  // that predate it. Clipboard writes go through native (webview clipboard
  // APIs are unreliable in JUCE), with the browser API as a dev-server
  // fallback.
  const handleShareBlock = useCallback(
    async (block: ToneBlock): Promise<boolean> => {
      const url = block.tone.url ?? `${T3K_API}/tones/${block.tone.id}`;
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

  // First line of defence for network-dependent actions: an instant
  // `navigator.onLine` check (no probe, no latency at click time). Actions
  // are never blocked beyond that: a throttled background probe verifies
  // HTTPS to TONE3000 on the side and, only after a confirming re-probe,
  // explains a broken-TLS environment (wrong system clock, intercepting
  // proxy) that would otherwise strand OAuth on a dead page. Failures still
  // land on the recovery paths (failed-navigation recovery, stream retry,
  // block retry).
  const connectionGate = useConnectionGate();
  const { requireConnection } = connectionGate;

  // The add/swap browse flows and their pending targets.
  const loadFlow = useToneLoadFlow({
    actions,
    stereoEnabled,
    requireConnection,
    setShowToneBrowser,
  });

  // Loading a preset or resetting to default replaces the chain. Leave any
  // takeover (tuner, tone browser, block detail) first so the new chain is
  // visible on the gallery, matching closeTunerThen.
  const showChainThen = useCallback(
    <A extends unknown[], R>(fn: (...args: A) => R) =>
      (...args: A): R => {
        if (showTuner) void handleToggleTuner(false);
        if (showToneBrowser) {
          loadFlow.clearPendingTargets();
          setShowToneBrowser(false);
        }
        sessionStorage.removeItem(DETAIL_BLOCK_STORAGE_KEY);
        setReturnToGallery((n) => n + 1);
        return fn(...args);
      },
    [showTuner, handleToggleTuner, showToneBrowser, loadFlow]
  );

  const handleReset = useMemo(
    () => showChainThen(actions.resetToDefault),
    [showChainThen, actions]
  );

  // Rename/delete/move only touch the preset list, so they pass through.
  const headerPresetStore = useMemo(
    () => ({
      ...presetStore,
      actions: {
        ...presetStore.actions,
        save: closeTunerThen(presetStore.actions.save),
        load: showChainThen(presetStore.actions.load),
      },
    }),
    [presetStore, closeTunerThen, showChainThen]
  );

  const openToneBrowser = useCallback(() => setShowToneBrowser(true), []);

  // TONE3000 session: API client, OAuth flows, signed-in identity, and
  // native's copy of the access token.
  const session = useToneSession({
    onToneSelected: loadFlow.handleToneSelected,
    onAuthenticated: openToneBrowser,
  });
  const { client: t3kClient, ensureNativeAuth, startLoginFlow, startSelectFlow } = session;

  const handleLogin = useCallback(
    () => requireConnection(() => startLoginFlow()),
    [requireConnection, startLoginFlow]
  );
  // Sign-in CTAs inside the browser (gated streams / Trending's discovery
  // footer) run the no-prompt login flow and return to this same browser,
  // never the full Select catalog.
  const handleBrowserSignIn = useCallback(
    () => requireConnection(() => startLoginFlow({ openBrowser: true })),
    [requireConnection, startLoginFlow]
  );
  // Browse on TONE3000 leaves for the Select OAuth catalog, so it takes the
  // same gate as login.
  const handleBrowseTone3000 = useCallback(
    () => requireConnection(() => startSelectFlow()),
    [requireConnection, startSelectFlow]
  );

  const handleLogout = useCallback(async () => {
    loadFlow.clearPendingTargets();
    setShowToneBrowser(false);
    await session.logout();
  }, [loadFlow, session]);

  // Closing without picking abandons any pending swap/insert target.
  const handleBrowserClose = useCallback(() => {
    loadFlow.clearPendingTargets();
    setShowToneBrowser(false);
  }, [loadFlow]);

  // Switch a block's model. Native downloads the new model file itself, so
  // refresh-and-sync the token first; switching after the editor has been
  // sitting idle is exactly when the last-pushed token has expired. Local
  // (drop-loaded) models switch from the on-disk stash instead: no download,
  // no token, works signed out.
  const handleSwitchModel = useCallback(
    async (blockId: string, modelId: number, model: Pick<Model, 'id' | 'name' | 'model_url'>) => {
      if (!model.model_url.startsWith('file:')) {
        try {
          await ensureNativeAuth();
        } catch (err) {
          // Refresh token rejected: tokens are cleared, the model select
          // disables itself on the next render, and the next + re-authenticates.
          console.error('Cannot switch model: TONE3000 session expired', err);
          return;
        }
      }
      const success = await actions.switchModel(blockId, modelId, JSON.stringify(model));
      if (!success) console.error('Failed to switch model');
    },
    [actions, ensureNativeAuth]
  );

  // Retry a failed model download. Refresh the token first when signed in
  // (the failure may have left the block waiting long enough for the last
  // pushed token to expire); signed out we retry anyway, since public model
  // URLs still work anonymously.
  const handleRetryLoad = useCallback(
    async (blockId: string) => {
      if (t3kClient.isAuthenticated()) {
        try {
          await ensureNativeAuth();
        } catch {
          // Session expired; the retry below still runs and native falls
          // back to whatever token it holds.
        }
      }
      await actions.retryModelLoad(blockId);
    },
    [actions, ensureNativeAuth, t3kClient]
  );

  // Menu-driven local load (the tiles' Load File / Load Folder): native owns
  // the whole flow (OS picker, validation, stash, load) and resolves when the
  // dialog closes. This is the route that works on Linux, where OS file drags
  // never reach the embedded webview and handleDropFile can't fire. Resync
  // after: the load lands outside useChainState's mutation wrapper.
  const handlePickLocalFile = useCallback(
    async (targetBlockId: string, kind: 'file' | 'folder') => {
      const res = await pickLocalToneFile(kind === 'folder', targetBlockId);
      await refresh();
      if (res?.blockId || res?.cancelled) return null;
      return res?.error ?? "Couldn't load the file";
    },
    [pickLocalToneFile, refresh]
  );

  // Non-blocking update check (enabled via VITE_T3K_UPDATE_NOTICE); also
  // resolves the running build's version for the Settings footer.
  const { notice: updateNotice, update, localVersion, remindLater } = useUpdateNotice(t3kClient);

  // Auth-dependent block actions (model switching) key off this. Reading
  // localStorage per render is fine: every login/logout transition already
  // re-renders Plugin (user / oauthPhase state), refreshing the value.
  const authenticated = t3kClient.isAuthenticated();

  // Single stable bundle of everything a block can do. ChainView and the
  // tiles/cards below it read this from context instead of threading a dozen
  // callback props (which would defeat their React.memo).
  const chainActions = useMemo<ChainActions>(
    () => ({
      addModel: loadFlow.handleAddModel,
      loadLocalFile: loadFlow.handleDropFile,
      pickLocalFile: handlePickLocalFile,
      removeBlock: actions.removeBlock,
      swapBlock: loadFlow.handleSwapBlock,
      shareBlock: handleShareBlock,
      reorderBlocks: actions.reorderBlocks,
      moveBlock: actions.moveBlockToChain,
      duplicateBlock: actions.duplicateBlock,
      copyBlock: actions.copyBlock,
      pasteBlock: actions.pasteBlock,
      swapChains: actions.swapChains,
      setBranch: actions.setBranch,
      clearBranch: actions.clearBranch,
      switchModel: handleSwitchModel,
      retryLoad: handleRetryLoad,
      listToneModels: session.listToneModels,
      getTone: session.getTone,
      setToneFavorite: session.setToneFavorite,
      refreshToneMetadata: actions.refreshToneMetadata,
      setBlockParam: actions.setBlockParam,
      setBlockSlimSize: actions.setBlockSlimSize,
      setBlockIrDecay: actions.setBlockIrDecay,
      setBlockEqBand: actions.setBlockEqBand,
      setBlockEqEnabled: actions.setBlockEqEnabled,
      setBlockEqPre: actions.setBlockEqPre,
      resetBlockEq: actions.resetBlockEq,
      authenticated,
      login: handleLogin,
    }),
    [
      actions,
      authenticated,
      handleLogin,
      handlePickLocalFile,
      handleRetryLoad,
      handleShareBlock,
      handleSwitchModel,
      loadFlow.handleAddModel,
      loadFlow.handleDropFile,
      loadFlow.handleSwapBlock,
      session.getTone,
      session.setToneFavorite,
      session.listToneModels,
    ]
  );

  return (
    <div
      style={{
        position: 'relative',
        // Explicit design-space box: rem lengths track the root font-size
        // (useUiScale), so this and every dimension inside scale together.
        width: `${DESIGN_WIDTH}rem`,
        // The window grows by the chrome-strip height (see useChromeChoreography),
        // so the 578px core UI between them keeps its full space.
        // (Figma's 600 includes a 22px mock OS title bar outside JUCE setSize.)
        height: `${DESIGN_HEIGHT + chrome.rootExtraHeight}rem`,
        // While the banner slides, the root and the banner wrapper animate
        // height with the same curve, so the flex middle (root minus fixed
        // strips) stays exactly constant and nothing inside moves.
        transition: chrome.animating ? `height ${BANNER_ANIM_MS}ms ease` : undefined,
        display: 'flex',
        flexDirection: 'column',
        backgroundColor: '#000000',
        boxSizing: 'border-box',
        overflow: 'hidden',
        color: '#ffffff',
      }}
    >
      {/* One app-wide toast pill, floating above the faceplate. Everything
          that raises toasts (preset save, share, auto measure) is inside. */}
      <ToastProvider bottom={PLATE_HEIGHT + (hintsVisible ? HINT_HEIGHT : 0) + 24}>
        {chrome.renderedBanner && (
          // Slide slot: the banner is anchored to the slot's bottom edge, so
          // opening/closing the slot slides it down/up from behind the top
          // edge. The window has already grown before the slide starts.
          <div
            style={{
              height: `${chrome.bannerSlotHeight}rem`,
              overflow: 'hidden',
              flexShrink: 0,
              display: 'flex',
              flexDirection: 'column',
              justifyContent: 'flex-end',
              transition: chrome.animating ? `height ${BANNER_ANIM_MS}ms ease` : undefined,
            }}
          >
            <AppBanner
              banner={chrome.renderedBanner}
              onAction={handleBannerAction}
              onDismiss={dismissBanner}
            />
          </div>
        )}

        <PluginHeader
          presetStore={headerPresetStore}
          activePreset={activePreset}
          atDefault={atDefault}
          onReset={handleReset}
          stereoEnabled={stereoEnabled}
          onStereoToggle={handleStereoToggle}
          showTuner={showTuner}
          onToggleTuner={handleToggleTuner}
          canUndo={canUndo}
          canRedo={canRedo}
          onUndo={handleUndo}
          onRedo={handleRedo}
          user={session.user}
          authenticated={authenticated}
          onOpenSettings={openDefaultSettings}
          onLogin={handleLogin}
          onLogout={handleLogout}
        />

        {/* Middle Section: Tuner (when toggled on) or Meters + Chain View.
          Horizontal inset is on this band; vertical inset lives only on the
          center column so meters always center in the full header-to-faceplate
          height (never shift when Select opens). Select drops the center's
          bottom pad and uses its own scroll padding instead. */}
        {showTuner ? (
          <TunerView onClose={closeTuner} />
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
              padding: '0 24rem',
              boxSizing: 'border-box',
            }}
          >
            <div
              style={{
                height: '100%',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                flexShrink: 0,
                backgroundColor: '#000000',
                // Above the Select Tone header scrim, so stereo columns that
                // overflow this slot into the center aren't covered by it.
                position: 'relative',
                zIndex: 3,
              }}
            >
              {/* 358 matches Figma's BLOCK column (title + gap + card). */}
              <DbMeter type="input" stereo={stereoInput && inputMode === 'stereo'} height={358} />
            </div>

            {/* Center: the chain gallery, or the tone browser takeover. */}
            <div
              style={{
                flex: 1,
                height: '100%',
                overflow: 'hidden',
                minHeight: 0,
                minWidth: 0,
                boxSizing: 'border-box',
                // Shared 24px under the header; 24px above the faceplate only
                // for chain/BLOCK. Select fills to the faceplate; block-info
                // fills to both header and faceplate, putting those pads
                // inside the scroll content instead.
                paddingTop: fillToFaceplate ? 0 : 24,
                paddingBottom: showToneBrowser || fillToFaceplate ? 0 : 24,
              }}
            >
              {showToneBrowser ? (
                <ToneBrowser
                  client={t3kClient}
                  // Pre-mounted during an OAuth return ('returning'), the client
                  // has no tokens until the callback's code exchange finishes;
                  // hold the stream fetch so it doesn't fire unauthenticated.
                  authPending={session.oauthPhase === 'returning'}
                  authenticated={authenticated}
                  onPickTone={session.selectToneById}
                  onBrowseTone3000={handleBrowseTone3000}
                  onSignIn={handleBrowserSignIn}
                  onClose={handleBrowserClose}
                />
              ) : (
                <ChainActionsProvider value={chainActions}>
                  <ChainView
                    chain={chain}
                    chainRight={stereoEnabled ? (chainRight ?? []) : null}
                    branch={stereoEnabled ? branch : null}
                    monoSum={monoSum}
                    canPaste={canPaste}
                    sampleRate={sampleRate}
                    namSlimSizeDefault={namSlimSizeDefault}
                    onFillToFaceplate={setFillToFaceplate}
                    returnToGallery={returnToGallery}
                  />
                </ChainActionsProvider>
              )}
            </div>

            <div
              style={{
                height: '100%',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                flexShrink: 0,
                backgroundColor: '#000000',
                // Above the Select Tone header scrim, so stereo columns that
                // overflow this slot into the center aren't covered by it.
                position: 'relative',
                zIndex: 3,
              }}
            >
              <DbMeter type="output" stereo={stereoImage} height={358} labelsPosition="right" />
            </div>
          </div>
        )}

        {/* Pinned faceplate at the bottom (gains, gate, tone stack), with the
          hint strip under it (hidden entirely when hints are off). */}
        <Faceplate
          balanceActive={balanceActive}
          stereoOutput={stereoOutput}
          stereoChains={stereoEnabled}
          stereoInput={stereoInput}
          branched={branch != null}
          inputMode={inputMode}
          onInputModeChange={actions.setInputMode}
        />
        <HintBar />

        {/* Settings takeover, mounted only while open so its parameter
          subscriptions and screen state don't run behind the main UI. */}
        {showSettings && (
          <Settings
            onClose={() => setShowSettings(false)}
            standalone={standalone}
            device={audioDevice}
            initialTab={settingsTabRef.current}
            version={localVersion}
            update={update}
            namSlimSizeDefault={namSlimSizeDefault}
            onNamSlimSizeDefaultChange={actions.setNamSlimSizeDefault}
            multiCore={multiCore}
            onMultiCoreChange={actions.setMultiCore}
            chain={chain}
            chainRight={chainRight}
          />
        )}

        {/* OAuth callback overlay: covers the chain UI while we resolve the
          tokens + tone after returning from tone3000.com, and surfaces any
          OAuth error (callback failures, failed-navigation recovery) with a
          retry that restarts whichever flow actually failed. */}
        <OAuthOverlay
          phase={session.oauthPhase}
          error={session.oauthError}
          onRetry={session.retryFlow}
          onDismiss={session.clearOauthError}
        />

        {/* Connection gate for internet-dependent actions (add / swap /
          login / select). Offline gates instantly; TLS problems surface from
          a non-blocking background probe, only after confirmation. */}
        <ConnectionModal
          problem={connectionGate.problem}
          onRetry={connectionGate.retry}
          onDismiss={connectionGate.dismiss}
        />

        {/* Update available, below OAuth/connection (z 3000) so those always win. */}
        <UpdateNotice notice={updateNotice} onRemindLater={remindLater} />
      </ToastProvider>
    </div>
  );
};
