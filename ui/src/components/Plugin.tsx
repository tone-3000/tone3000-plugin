import React, { useState, useCallback, useEffect, useMemo, useRef } from 'react';
import { useNativeFunction } from '../hooks/useFunction';
import { useChainState } from '../hooks/useChainState';
import { ChainActionsProvider } from '../hooks/useChainActions';
import type { ChainActions } from '../hooks/useChainActions';
import { usePresets } from '../hooks/usePresets';
import { useParameter } from '../hooks/useParameter';
import { useAudioDevice } from '../hooks/useAudioDevice';
import { useInternetGate } from '../hooks/useInternetGate';
import { useToneSession } from '../hooks/useToneSession';
import { useToneLoadFlow } from '../hooks/useToneLoadFlow';
import { useUpdateNotice } from '../hooks/useUpdateNotice';
import { useUiScale, DESIGN_WIDTH, DESIGN_HEIGHT } from '../hooks/useUiScale';
import { shouldRestoreToneBrowser } from '../hooks/useT3kSelect';
import { ChainView } from './ChainView';
import { Faceplate } from './Faceplate';
import { HintBar, HINT_HEIGHT } from './HintBar';
import { PluginHeader } from './PluginHeader';
import { useHintsEnabled } from './helpText';
import { AppBanner, BANNER_HEIGHT, useAppBanner, type BannerAction } from './AppBanner';
import { DbMeter } from './DbMeter';
import { TunerView } from './TunerView';
import { OAuthOverlay } from './OAuthOverlay';
import { OfflineModal } from './OfflineModal';
import { ToneBrowser } from './ToneBrowser';
import { UpdateNotice } from './UpdateNotice';
import Settings, { type SettingsTab } from './Settings';
import { T3K_API } from '../t3k/config';
import type { Model } from '../types/tone';
import type { ToneBlock } from '../types/chain';

export const Plugin: React.FC = () => {
  const [showSettings, setShowSettings] = useState(false);
  // Which tab Settings opens on; banner actions land directly on System.
  const settingsTabRef = useRef<SettingsTab>('plugin');
  const [showTuner, setShowTuner] = useState(false);
  // In-plugin tone browser takeover (streams of TONE3000 tones). Opened by
  // the + when already authenticated, or right after the no-prompt login
  // flow returns. Seeded true when we're returning from a browse-intent
  // redirect without a picked tone (Browse closed/canceled) so the browser
  // is already mounted under the busy scrim; no flash of the main chain.
  const [showToneBrowser, setShowToneBrowser] = useState(shouldRestoreToneBrowser);

  // Chain state: revision-gated polling + mutation actions, owned by one hook.
  const {
    chain,
    chainRight,
    branch,
    canUndo,
    canRedo,
    activePreset,
    stereoEnabled,
    stereoInput,
    inputMode,
    namFullSize,
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

  // Mono-mode spread: while it's on the output stage is effectively stereo,
  // so the output meter + balance knob switch to their stereo forms.
  const [spreadEnabled] = useParameter('spreadEnabled', 'toggle');
  const stereoOutput = stereoEnabled || spreadEnabled;

  const setTunerEnabled = useNativeFunction<boolean>('setTunerEnabled');
  const copyToClipboard = useNativeFunction<boolean>('copyToClipboard');
  const setExtraContentHeight = useNativeFunction<boolean>('setExtraContentHeight');

  const openSettings = useCallback((tab: SettingsTab) => {
    settingsTabRef.current = tab;
    setShowSettings(true);
  }, []);
  const openPluginSettings = useCallback(() => openSettings('plugin'), [openSettings]);

  // App banner: one priority-picked banner over the audio device state
  // (standalone only). Both the banner (top) and the hint bar (bottom) are
  // chrome strips that grow the window rather than squish the 578px core; we
  // report their combined height to native whenever either toggles.
  const { banner, dismiss: dismissBanner } = useAppBanner(standalone ? audioDevice.state : null);
  // Whole-UI proportional scaling: the root div below is a fixed 1024-wide
  // design-space box and this ref's CSS zoom stretches it to the window.
  const uiScaleRef = useUiScale<HTMLDivElement>();
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
  const handleToggleTuner = useCallback(
    async (show: boolean) => {
      setShowTuner(show);
      await setTunerEnabled(show);
    },
    [setTunerEnabled]
  );
  const closeTuner = useCallback(() => handleToggleTuner(false), [handleToggleTuner]);

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

  // First line of defence for network-dependent actions: an instant
  // `navigator.onLine` check (no probe, no latency). It only catches the
  // "no interface up" case; a connected-but-dead network still gets through
  // and lands on the recovery paths (failed-navigation recovery, stream
  // retry, block retry).
  const internetGate = useInternetGate();
  const { requireInternet } = internetGate;

  // The add/swap browse flows and their pending targets.
  const loadFlow = useToneLoadFlow({
    actions,
    stereoEnabled,
    requireInternet,
    setShowToneBrowser,
  });

  const openToneBrowser = useCallback(() => setShowToneBrowser(true), []);

  // TONE3000 session: API client, OAuth flows, signed-in identity, and
  // native's copy of the access token.
  const session = useToneSession({
    onToneSelected: loadFlow.handleToneSelected,
    onAuthenticated: openToneBrowser,
  });
  const { client: t3kClient, ensureNativeAuth, startLoginFlow } = session;

  const handleLogin = useCallback(
    () => requireInternet(() => startLoginFlow()),
    [requireInternet, startLoginFlow]
  );
  // Sign-in CTAs inside the browser (gated streams / Trending's discovery
  // footer) run the no-prompt login flow and return to this same browser,
  // never the full Select catalog.
  const handleBrowserSignIn = useCallback(
    () => requireInternet(() => startLoginFlow({ openBrowser: true })),
    [requireInternet, startLoginFlow]
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
  // sitting idle is exactly when the last-pushed token has expired.
  const handleSwitchModel = useCallback(
    async (blockId: string, modelId: number, model: Model) => {
      try {
        await ensureNativeAuth();
      } catch (err) {
        // Refresh token rejected: tokens are cleared, the model select
        // disables itself on the next render, and the next + re-authenticates.
        console.error('Cannot switch model: TONE3000 session expired', err);
        return;
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

  // Non-blocking update check (enabled via VITE_T3K_UPDATE_NOTICE); also
  // resolves the running build's version for the Settings footer.
  const { notice: updateNotice, update, localVersion, remindLater } = useUpdateNotice();

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
      removeBlock: actions.removeBlock,
      swapBlock: loadFlow.handleSwapBlock,
      shareBlock: handleShareBlock,
      reorderBlocks: actions.reorderBlocks,
      moveBlock: actions.moveBlockToChain,
      duplicateBlock: actions.duplicateBlock,
      swapChains: actions.swapChains,
      setBranch: actions.setBranch,
      clearBranch: actions.clearBranch,
      switchModel: handleSwitchModel,
      retryLoad: handleRetryLoad,
      listToneModels: session.listToneModels,
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
      handleRetryLoad,
      handleShareBlock,
      handleSwitchModel,
      loadFlow.handleAddModel,
      loadFlow.handleSwapBlock,
      session.listToneModels,
    ]
  );

  return (
    <div
      ref={uiScaleRef}
      style={{
        position: 'relative',
        // Explicit design-space box: the useUiScale zoom stretches it to the
        // real window size, so every hard-coded px inside scales with it.
        width: `${DESIGN_WIDTH}px`,
        // The window grows by the chrome-strip height (see setExtraContentHeight
        // above), so the 578px core UI between them keeps its full space.
        // (Figma's 600 includes a 22px mock OS title bar outside JUCE setSize.)
        height: `${DESIGN_HEIGHT + extraHeight}px`,
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

      <PluginHeader
        presetStore={presetStore}
        activePreset={activePreset}
        stereoEnabled={stereoEnabled}
        onStereoToggle={actions.setStereoMode}
        showTuner={showTuner}
        onToggleTuner={handleToggleTuner}
        canUndo={canUndo}
        canRedo={canRedo}
        onUndo={actions.undo}
        onRedo={actions.redo}
        user={session.user}
        authenticated={authenticated}
        onOpenSettings={openPluginSettings}
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
            padding: '0 24px',
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
              // for chain/BLOCK; Select fills to the faceplate edge.
              paddingTop: 24,
              paddingBottom: showToneBrowser ? 0 : 24,
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
                onBrowseTone3000={session.startSelectFlow}
                onSignIn={handleBrowserSignIn}
                onClose={handleBrowserClose}
              />
            ) : (
              <ChainActionsProvider value={chainActions}>
                <ChainView
                  chain={chain}
                  chainRight={stereoEnabled ? (chainRight ?? []) : null}
                  branch={stereoEnabled ? branch : null}
                  sampleRate={sampleRate}
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
            }}
          >
            <DbMeter type="output" stereo={stereoOutput} height={358} labelsPosition="right" />
          </div>
        </div>
      )}

      {/* Pinned faceplate at the bottom (gains, gate, tone stack), with the
          hint strip under it (hidden entirely when hints are off). */}
      <Faceplate
        stereoOutput={stereoOutput}
        stereoChains={stereoEnabled}
        stereoInput={stereoInput}
        branched={branch != null}
        inputMode={inputMode}
        onInputModeChange={actions.setInputMode}
      />
      <HintBar namFullSize={namFullSize} onNamFullSizeChange={actions.setNamFullSize} />

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
          namFullSize={namFullSize}
          onNamFullSizeChange={actions.setNamFullSize}
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

      {/* Offline gate for internet-dependent actions (add / swap / login). */}
      <OfflineModal
        open={internetGate.offlineModalOpen}
        onRetry={internetGate.retry}
        onDismiss={internetGate.dismiss}
      />

      {/* Update available, below OAuth/offline (z 3000) so those always win. */}
      <UpdateNotice notice={updateNotice} onRemindLater={remindLater} />
    </div>
  );
};
