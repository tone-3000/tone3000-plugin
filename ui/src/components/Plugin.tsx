import React, { useState, useEffect, useCallback } from 'react';
import { Settings as SettingsIcon } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';
import { useFunction } from '../hooks/useFunction';
import { ChainView } from './ChainView';
import type { ChainItem, Model, Tone } from '../types/tone';
import Settings from './Settings';
import { DbMeter } from './DbMeter';
import { useT3kSelect } from '../hooks/useT3kSelect';
import type { T3KTokens } from '../t3k/tone3000-client';
import { OAuthOverlay } from './OAuthOverlay';

export const Plugin: React.FC = () => {
  // Plugin Parameters
  const [inputLevel, setInputLevel] = useParameter('inputLevel', 'slider');
  const [outputLevel, setOutputLevel] = useParameter('outputLevel', 'slider');
  const [toneBass, setToneBass] = useParameter('toneBass', 'slider');
  const [toneMid, setToneMid] = useParameter('toneMid', 'slider');
  const [toneTreble, setToneTreble] = useParameter('toneTreble', 'slider');
  const [noiseGate, setNoiseGate] = useParameter('gateThreshold', 'slider');

  // Chain state (full order from backend, includes insert block)
  const [chain, setChain] = useState<ChainItem[]>([]);
  const [showSettings, setShowSettings] = useState(false);

  // Native functions
  const getChainStatus = useFunction<any>('getChainStatus');
  const loadTone = useFunction<string>('loadTone');
  const switchModel = useFunction<boolean>('switchModel');
  const removeChainBlock = useFunction<string>('removeChainBlock');
  const reorderChainBlocks = useFunction<boolean>('reorderChainBlocks');
  const setAccessToken = useFunction<boolean>('setAccessToken');

  // Load chain status from backend (includes insert block position)
  const loadChainStatus = async () => {
    try {
      const status = await getChainStatus.invoke();
      if (status && status.chain) {
        setChain(status.chain as ChainItem[]);
      }
    } catch (error) {
      console.error('Error loading chain status:', error);
    }
  };

  // Remove a block from the chain
  const removeBlock = async (id: string) => {
    try {
      await removeChainBlock.invoke(id);
      await loadChainStatus();
    } catch (error) {
      console.error('Error removing chain block:', error);
    }
  };

  // Reorder items (full order including insert block - backend is source of truth)
  const handleReorderItems = async (orderedIds: string[]) => {
    try {
      const currentOrder = chain.map((item) => item.blockId);
      const orderChanged =
        currentOrder.length !== orderedIds.length ||
        currentOrder.some((id, i) => id !== orderedIds[i]);

      if (orderChanged) {
        await reorderChainBlocks.invoke(orderedIds);
        await loadChainStatus();
      }
    } catch (error) {
      console.error('Error reordering chain items:', error);
      await loadChainStatus();
    }
  };

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
  // Select flow — push it through native so it joins the chain.
  const handleToneSelected = useCallback(
    async (tone: Tone & { models: Model[] }, tokens: T3KTokens) => {
      if (!tone.models || tone.models.length === 0) {
        console.error('Tone has no models');
        return;
      }

      // Make sure native has the freshest access token before it tries to
      // download the model from `model_url` (which now requires Bearer auth).
      await pushAccessTokenToNative(tokens.access_token);

      console.log('Loading tone:', tone.title);

      try {
        const blockId = await loadTone.invoke(JSON.stringify(tone));

        if (blockId) {
          console.log('Tone loaded successfully, block ID:', blockId);
          await loadChainStatus();
        } else {
          console.error('Failed to load tone');
        }
      } catch (error) {
        console.error('Error loading tone:', error);
      }
    },
    [loadTone, loadChainStatus, pushAccessTokenToNative]
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
    try {
      const success = await switchModel.invoke(blockId, modelId);
      if (success) {
        await loadChainStatus();
      } else {
        console.error('Failed to switch model');
      }
    } catch (error) {
      console.error('Error switching model:', error);
    }
  };

  const handleAddModel = () => {
    startSelectFlow();
  };

  useEffect(() => {
    loadChainStatus();
    const interval = setInterval(loadChainStatus, 2000);
    return () => clearInterval(interval);
  }, []);

  return (
    <div
      style={{
        position: 'relative',
        width: '100%',
        maxWidth: '100%',
        height: '710px',
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
        <button
          onClick={() => setShowSettings(true)}
          title="Settings"
          style={{
            background: 'transparent',
            // border: '1px solid #ffffff',
            border: 'none',
            color: '#ffffff',
            // width: '32px',
            // height: '32px',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            cursor: 'pointer',
            borderRadius: '4px',
          }}
        >
          <SettingsIcon size={18} />
        </button>
      </div>

      {/* Middle Section: Meters + Chain View */}
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
          <DbMeter type="input" height={450} />
        </div>

        {/* Chain View - Center */}
        <div
          className="hide-scrollbar"
          style={{
            flex: 1,
            height: '100%',
            overflow: 'auto',
            minHeight: 0,
            padding: '24px 0',
          }}
        >
          <ChainView
            chain={chain}
            onAddModel={handleAddModel}
            onRemoveBlock={removeBlock}
            onReorderItems={handleReorderItems}
            onSwitchModel={handleSwitchModel}
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
          <DbMeter type="output" height={450} labelsPosition="right" />
        </div>
      </div>

      {/* Pinned Knobs Row at Bottom - Full Width */}
      <div
        style={{
          width: '100%',
          height: '142px',
          display: 'flex',
          justifyContent: 'space-between',
          flexShrink: 0,
          borderTop: '1px solid rgba(84, 84, 88, 0.65)',
          background: '#1C1C1E',
          padding: '0 24px',
        }}
      >
        <KnobControl label="Input" value={inputLevel} onChange={setInputLevel} />
        <KnobControl label="Gate" value={noiseGate} onChange={setNoiseGate} />
        <div style={{ display: 'flex', flexDirection: 'row', gap: 32 }}>
          <KnobControl label="Bass" value={toneBass} onChange={setToneBass} />
          <KnobControl label="Middle" value={toneMid} onChange={setToneMid} />
          <KnobControl label="Treble" value={toneTreble} onChange={setToneTreble} />
        </div>
        <KnobControl label="Output" value={outputLevel} onChange={setOutputLevel} />
      </div>

      {/* Settings Modal */}
      <Settings isOpen={showSettings} onClose={() => setShowSettings(false)} />

      {/* OAuth callback overlay — covers the chain UI while we resolve the
          tokens + tone after returning from tone3000.com, and surfaces any
          OAuth error with a retry affordance. */}
      <OAuthOverlay
        phase={oauthPhase}
        error={oauthError}
        onRetry={startSelectFlow}
        onDismiss={clearOauthError}
      />
    </div>
  );
};
