import React from 'react';
import { Undo2, Redo2 } from 'lucide-react';
import { AccountMenu } from './AccountMenu';
import { IconButton } from './IconButton';
import { PresetBar } from './PresetBar';
import { StereoModeToggle } from './StereoModeToggle';
import { HELP } from './helpText';
import { BORDER } from './theme';
import type { usePresets } from '../hooks/usePresets';
import type { ActivePreset } from '../types/chain';
import type { User } from '../types/tone';

type PresetStore = ReturnType<typeof usePresets>;

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

interface PluginHeaderProps {
  presetStore: PresetStore;
  activePreset: ActivePreset | null;
  stereoEnabled: boolean;
  onStereoToggle: (enabled: boolean) => void;
  showTuner: boolean;
  onToggleTuner: (show: boolean) => void;
  canUndo: boolean;
  canRedo: boolean;
  onUndo: () => void;
  onRedo: () => void;
  user: User | null;
  authenticated: boolean;
  onOpenSettings: () => void;
  onLogin: () => void;
  onLogout: () => void;
}

/**
 * Full-width top bar: logo, preset controls, stereo toggle, tuner, undo/redo
 * and the account menu. Memoized because Plugin re-renders on every chain
 * poll tick while nothing up here changes.
 */
export const PluginHeader = React.memo(function PluginHeader({
  presetStore,
  activePreset,
  stereoEnabled,
  onStereoToggle,
  showTuner,
  onToggleTuner,
  canUndo,
  canRedo,
  onUndo,
  onRedo,
  user,
  authenticated,
  onOpenSettings,
  onLogin,
  onLogout,
}: PluginHeaderProps) {
  return (
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
        <img src="/t3k.svg" alt="T3K" style={{ width: '160px' }} />
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
        <StereoModeToggle stereoEnabled={stereoEnabled} onToggle={onStereoToggle} />
        <IconButton
          onClick={() => onToggleTuner(!showTuner)}
          help={HELP.tuner}
          active={showTuner}
          fillWhenActive
          size={28}
        >
          <TuningForkIcon size={18} />
        </IconButton>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
          <IconButton onClick={onUndo} disabled={!canUndo} help={HELP.undo} size={28}>
            <Undo2 size={18} />
          </IconButton>
          <IconButton onClick={onRedo} disabled={!canRedo} help={HELP.redo} size={28}>
            <Redo2 size={18} />
          </IconButton>
        </div>
        <AccountMenu
          user={user}
          authenticated={authenticated}
          onOpenSettings={onOpenSettings}
          onLogin={onLogin}
          onLogout={onLogout}
        />
      </div>
    </div>
  );
});
