import React, { useState, useCallback } from 'react';
import { Settings as SettingsIcon, Undo2, Redo2 } from 'lucide-react';
import { useFunction } from '../hooks/useFunction';
import { useChainState } from '../hooks/useChainState';
import { usePresets } from '../hooks/usePresets';
import { ChainView } from './ChainView';
import { Faceplate } from './Faceplate';
import { PresetBar } from './PresetBar';
import { StereoModeToggle } from './StereoControls';
import { useParameter } from '../hooks/useParameter';
import type { Model, Tone } from '../types/tone';
import type { ChainSide, ToneBlock } from '../types/chain';
import Settings from './Settings';
import { DbMeter } from './DbMeter';
import { TunerView } from './TunerView';
import { useT3kSelect } from '../hooks/useT3kSelect';
import { useInternetGate } from '../hooks/useInternetGate';
import { T3K_API } from '../t3k/config';
import type { T3KTokens } from '../t3k/tone3000-client';
import { OAuthOverlay } from './OAuthOverlay';
import { OfflineModal } from './OfflineModal';

// Swap targets must survive the Select flow's full-page OAuth redirect (the
// webview navigates to tone3000.com and back, remounting React), so the
// pending swap block id lives in sessionStorage rather than component state.
const SWAP_STORAGE_KEY = 't3k.pendingSwapBlockId';

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
  const [showTuner, setShowTuner] = useState(false);

  // Chain state: revision-gated polling + mutation actions, owned by one hook.
  const {
    chain,
    chainRight,
    canUndo,
    canRedo,
    activePreset,
    stereoEnabled,
    stereoInput,
    standalone,
    inputMode,
    sampleRate,
    refresh,
    actions,
  } = useChainState();

  // Internal presets. Mutations resync the chain state immediately (loading a
  // preset replaces the chain; saving/renaming changes the active preset).
  const presetStore = usePresets(refresh);

  // Mono-mode spread: while it's on the output stage is effectively stereo,
  // so the output meter + balance knob switch to their stereo forms.
  const [spreadEnabled] = useParameter('spreadEnabled', 'toggle');
  const stereoOutput = stereoEnabled || spreadEnabled;

  // One-shot native functions
  const setAccessToken = useFunction<boolean>('setAccessToken');
  const setTunerEnabled = useFunction<boolean>('setTunerEnabled');
  const copyToClipboard = useFunction<boolean>('copyToClipboard');

  // Toggle the tuner screen; native only feeds the pitch detector while it's on.
  const handleToggleTuner = async (show: boolean) => {
    setShowTuner(show);
    try {
      await setTunerEnabled.invoke(show);
    } catch (error) {
      console.error('Error toggling tuner:', error);
    }
  };

  // Reorder one lane (full order including its insert slot). Native infers
  // which lane the ids belong to; the gallery only calls this on real moves.
  const handleReorderItems = async (orderedIds: string[]) => {
    await actions.reorderBlocks(orderedIds);
  };

  // Share: copy the tone's public TONE3000 page URL. Clipboard writes go
  // through native (webview clipboard APIs are unreliable in JUCE), with the
  // browser API as a dev-server fallback.
  const handleShareBlock = useCallback(
    async (block: ToneBlock): Promise<boolean> => {
      const url = `${T3K_API}/tones/${block.tone.id}`;
      const ok = await copyToClipboard.invoke(url);
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
      try {
        await setAccessToken.invoke(accessToken);
      } catch (error) {
        console.error('Failed to push access token to native:', error);
      }
    },
    [setAccessToken]
  );

  // Handle a fully-resolved tone (with embedded models) coming out of the
  // Select flow. If a swap was pending (user hit the swap button on a block
  // before the redirect), replace that block in place; otherwise add the tone
  // at the insert slot.
  const handleToneSelected = useCallback(
    async (tone: Tone & { models: Model[] }, tokens: T3KTokens) => {
      if (!tone.models || tone.models.length === 0) {
        console.error('Tone has no models');
        return;
      }

      // Consume the pending swap target up front so it can never leak into a
      // later selection. (Add flows clear it before starting; see below.)
      const swapBlockId = sessionStorage.getItem(SWAP_STORAGE_KEY);
      sessionStorage.removeItem(SWAP_STORAGE_KEY);

      // Make sure native has the freshest access token before it tries to
      // download the model from `model_url` (which now requires Bearer auth).
      await pushAccessTokenToNative(tokens.access_token);

      const toneJson = JSON.stringify(tone);

      if (swapBlockId) {
        console.log('Swapping tone into block', swapBlockId, ':', tone.title);
        const swapped = await actions.swapTone(swapBlockId, toneJson);
        if (swapped) return;
        console.warn('Swap target no longer exists; adding tone as a new block');
      }

      console.log('Loading tone:', tone.title);
      const blockId = await actions.loadTone(toneJson);
      if (!blockId) console.error('Failed to load tone');
    },
    [actions, pushAccessTokenToNative]
  );

  // TONE3000 Select integration. Single-webview redirect flow: clicking + on
  // the chain navigates the main webview to tone3000.com; TONE3000 redirects
  // back to index.html?code=…&tone_id=… and useT3kSelect resolves the rest.
  const { startSelectFlow, oauthPhase, oauthError, clearOauthError } =
    useT3kSelect({
      onToneSelected: handleToneSelected,
      onAccessTokenUpdated: pushAccessTokenToNative,
    });

  // Handle switching models within a block
  const handleSwitchModel = async (blockId: string, modelId: number) => {
    const success = await actions.switchModel(blockId, modelId);
    if (!success) console.error('Failed to switch model');
  };

  // Gate internet-dependent actions: the Select flow navigates the webview to
  // tone3000.com, and doing that offline strands the user on a browser error
  // page. Check connectivity first and show a modal if we're offline.
  const internetGate = useInternetGate();

  // Adding routes to a lane via the native active-edit side (it has to
  // survive the OAuth redirect, so it lives in native state, not React's).
  const handleAddModel = (side: ChainSide) => {
    internetGate.requireInternet(async () => {
      sessionStorage.removeItem(SWAP_STORAGE_KEY);
      if (stereoEnabled) await actions.setActiveSide(side);
      startSelectFlow();
    });
  };

  // Swap: remember the target block, then run the same Select flow as add.
  const handleSwapBlock = (blockId: string) => {
    internetGate.requireInternet(() => {
      sessionStorage.setItem(SWAP_STORAGE_KEY, blockId);
      startSelectFlow();
    });
  };

  return (
    <div
      style={{
        position: 'relative',
        width: '100%',
        maxWidth: '100%',
        height: '600px',
        display: 'flex',
        flexDirection: 'column',
        backgroundColor: '#000000',
        boxSizing: 'border-box',
        overflow: 'hidden',
        color: '#ffffff',
      }}
    >
      {/* Header with T3K Logo and Settings - Full Width */}
      <div
        style={{
          width: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          backgroundColor: '#000000',
          padding: '12px 24px',
          flexShrink: 0,
          borderBottom: '1px solid rgba(84, 84, 88, 0.65)',
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
              height: '30px',
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
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <PresetBar
            active={activePreset}
            presets={presetStore.presets}
            onSave={presetStore.actions.save}
            onLoad={presetStore.actions.load}
            onRename={presetStore.actions.rename}
            onDelete={presetStore.actions.remove}
          />
          <StereoModeToggle
            stereoEnabled={stereoEnabled}
            onToggle={(enabled) => actions.setStereoMode(enabled)}
          />
          <button
            onClick={() => handleToggleTuner(!showTuner)}
            title="Tuner"
            style={{
              background: showTuner ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
              border: 'none',
              color: '#ffffff',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: 'pointer',
              borderRadius: '4px',
              padding: '5px',
            }}
          >
            <TuningForkIcon size={18} />
          </button>
          <button
            onClick={() => actions.undo()}
            disabled={!canUndo}
            title="Undo"
            style={{
              background: 'transparent',
              border: 'none',
              color: '#ffffff',
              opacity: canUndo ? 1 : 0.3,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: canUndo ? 'pointer' : 'default',
              borderRadius: '4px',
              padding: '5px',
            }}
          >
            <Undo2 size={18} />
          </button>
          <button
            onClick={() => actions.redo()}
            disabled={!canRedo}
            title="Redo"
            style={{
              background: 'transparent',
              border: 'none',
              color: '#ffffff',
              opacity: canRedo ? 1 : 0.3,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: canRedo ? 'pointer' : 'default',
              borderRadius: '4px',
              padding: '5px',
            }}
          >
            <Redo2 size={18} />
          </button>
          <button
            onClick={() => setShowSettings(true)}
            title="Settings"
            style={{
              background: 'transparent',
              border: 'none',
              color: '#ffffff',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: 'pointer',
              borderRadius: '4px',
              padding: '5px',
            }}
          >
            <SettingsIcon size={18} />
          </button>
        </div>
      </div>

      {/* Middle Section: Tuner (when toggled on) or Meters + Chain View */}
      {showTuner ? (
        <TunerView />
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
          <DbMeter type="input" stereo={stereoInput} height={400} />
        </div>

        {/* Chain View - Center (gallery lanes scroll horizontally inside) */}
        <div
          style={{
            flex: 1,
            height: '100%',
            overflow: 'hidden',
            minHeight: 0,
            minWidth: 0,
            padding: '24px 0',
            boxSizing: 'border-box',
          }}
        >
          <ChainView
            chain={chain}
            chainRight={stereoEnabled ? chainRight ?? [] : null}
            onAddModel={handleAddModel}
            onRemoveBlock={(id) => actions.removeBlock(id)}
            onSwapBlock={handleSwapBlock}
            onShareBlock={handleShareBlock}
            onReorderItems={handleReorderItems}
            onMoveBlock={(blockId, side, index) => actions.moveBlockToChain(blockId, side, index)}
            onSwapChains={() => actions.swapChains()}
            onSwitchModel={handleSwitchModel}
            onSetBlockParam={actions.setBlockParam}
            onSetBlockEqBand={actions.setBlockEqBand}
            onSetBlockEqEnabled={(id, enabled) => actions.setBlockEqEnabled(id, enabled)}
            onResetBlockEq={(id) => actions.resetBlockEq(id)}
            sampleRate={sampleRate}
          />
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
          <DbMeter
            type="output"
            stereo={stereoOutput}
            height={400}
            labelsPosition="right"
          />
        </div>
      </div>
      )}

      {/* Pinned faceplate at the bottom (gains, gate, tone stack) */}
      <Faceplate
        stereoOutput={stereoOutput}
        stereoInput={stereoInput}
        stereoChains={stereoEnabled}
      />

      {/* Settings Modal */}
      <Settings
        isOpen={showSettings}
        onClose={() => setShowSettings(false)}
        standalone={standalone}
        inputMode={inputMode}
        onSetInputMode={(mode) => actions.setInputMode(mode)}
      />

      {/* OAuth callback overlay — covers the chain UI while we resolve the
          tokens + tone after returning from tone3000.com, and surfaces any
          OAuth error with a retry affordance. */}
      <OAuthOverlay
        phase={oauthPhase}
        error={oauthError}
        onRetry={startSelectFlow}
        onDismiss={clearOauthError}
      />

      {/* Offline gate for internet-dependent actions (e.g. the + button). */}
      <OfflineModal
        open={internetGate.offlineModalOpen}
        onRetry={internetGate.retry}
        onDismiss={internetGate.dismiss}
      />
    </div>
  );
};
