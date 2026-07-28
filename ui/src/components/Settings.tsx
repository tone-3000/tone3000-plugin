import React, { useCallback, useState } from 'react';
import { X as XIcon, Plug, MonitorSpeaker } from 'lucide-react';
import { useParameter } from '../hooks/useParameter';
import { useNativeFunction } from '../hooks/useFunction';
import { setHintsEnabled, useHintsEnabled } from './helpText';
import {
  setBlockNormalizeControlEnabled,
  useBlockNormalizeControlEnabled,
  setDefaultNamA2Size,
  useDefaultNamA2Size,
  setNamA2SizeMode,
  useNamA2SizeMode,
  type NamA2Size,
  type NamA2SizeMode,
} from './uiPreferences';
import type { UpdateNoticeData } from '../hooks/useUpdateNotice';
import type { AudioDevice } from '../hooks/useAudioDevice';
import { GRAY, MUTED, SUBTLE } from './theme';
import {
  FIELD_BORDER,
  RadioOption,
  SelectField,
  ToggleRow,
  ctaButtonStyle,
  descriptionStyle,
  outlinedFieldStyle,
  sectionLabelStyle,
} from './controls';
import { SystemSettings } from './SystemSettings';
import { MidiMapSettings } from './MidiMapSettings';

// External docs: how to measure your rig's calibration levels.
const CALIBRATION_DOCS_URL =
  'https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/calibration.html';

/**
 * Settings: full-window takeover, tabbed between Plugin Settings (hints,
 * MIDI mapping, NAM size, normalization, calibration, diagnostics) and
 * System Settings (the bespoke audio device + MIDI hardware panel). The
 * System tab only exists in the standalone app — hosts own devices, sample
 * rate and buffer size arrive as facts from the DAW — and with one tab the
 * tab bar drops away entirely!
 */

export type SettingsTab = 'plugin' | 'system';

interface SettingsProps {
  /** Mounted only while open (see Plugin) — closing unmounts, so screen
      state and parameter subscriptions reset for free. */
  onClose: () => void;
  /** True in the standalone app — enables the System Settings tab. */
  standalone: boolean;
  /** Shared audio device state/actions (also drives the app banner). */
  device: AudioDevice;
  /** Tab to open on (banner actions land directly on System). */
  initialTab?: SettingsTab;
  /** Running build version ("" outside the plugin). */
  version: string;
  /** Newer published build, if the startup check found one (even if the
      startup modal was dismissed) — shows an update button in the footer. */
  update: UpdateNoticeData | null;
}

const NAM_A2_SIZE_OPTIONS: { value: NamA2Size; label: string }[] = [
  { value: 'lite', label: 'A2-Lite' },
  { value: 'full', label: 'A2-Full' },
];

// Oversampling rate choices. Values are the osFactor parameter's choice
// indices (as strings for SelectField); the DSP maps index i to 2^(i+1).
const OS_FACTOR_OPTIONS: { value: '0' | '1' | '2'; label: string }[] = [
  { value: '0', label: '2X - Default' },
  { value: '1', label: '4X' },
  { value: '2', label: '8X' },
];

const NAM_A2_MODE_OPTIONS: {
  value: NamA2SizeMode;
  label: string;
  description: React.ReactNode;
}[] = [
  {
    value: 'lite',
    label: 'A2-Lite',
    description: 'Sounds great and uses less CPU',
  },
  {
    value: 'full',
    label: 'A2-Full',
    description: 'Maximum accuracy model',
  },
  {
    value: 'perTone',
    label: 'Choose per tone',
    description: (
      <>
        A{' '}
        <span
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            height: '18px',
            borderRadius: '4px',
            backgroundColor: 'rgba(120, 120, 128, 0.36)',
            padding: '0 6px',
            gap: '4px',
            verticalAlign: 'middle',
            fontFamily: 'monospace',
            fontSize: '10px',
            fontWeight: 400,
            color: MUTED,
          }}
        >
          <span>LITE</span>
          <span>FULL</span>
        </span>{' '}
        switch appears on each tone
      </>
    ),
  },
];

/** Full-width tab bar (mockup style: icon + label, active underline). */
const TabBar: React.FC<{
  active: SettingsTab;
  onChange: (tab: SettingsTab) => void;
}> = ({ active, onChange }) => {
  const tabs: { id: SettingsTab; label: string; icon: React.ReactNode }[] = [
    { id: 'plugin', label: 'Plugin Settings', icon: <Plug size={16} /> },
    { id: 'system', label: 'System Settings', icon: <MonitorSpeaker size={16} /> },
  ];
  return (
    <div
      role="tablist"
      style={{ display: 'flex', borderBottom: FIELD_BORDER, marginBottom: '28px' }}
    >
      {tabs.map((tab) => {
        const selected = tab.id === active;
        return (
          <button
            key={tab.id}
            role="tab"
            aria-selected={selected}
            onClick={() => onChange(tab.id)}
            style={{
              flex: 1,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              gap: '8px',
              padding: '12px 0',
              background: 'transparent',
              border: 'none',
              borderBottom: `2px solid ${selected ? '#ffffff' : 'transparent'}`,
              marginBottom: '-1px',
              color: selected ? '#ffffff' : SUBTLE,
              fontSize: '14px',
              fontWeight: 600,
              cursor: 'pointer',
            }}
          >
            {tab.icon}
            {tab.label}
          </button>
        );
      })}
    </div>
  );
};

export const Settings: React.FC<SettingsProps> = ({
  onClose,
  standalone,
  device,
  initialTab = 'plugin',
  version,
  update,
}) => {
  const [tab, setTab] = useState<SettingsTab>(standalone ? initialTab : 'plugin');
  const [screen, setScreen] = useState<'main' | 'advanced'>('main');

  const hintsEnabled = useHintsEnabled();
  const blockNormalizeControlEnabled = useBlockNormalizeControlEnabled();
  const namA2SizeMode = useNamA2SizeMode();
  const defaultNamA2Size = useDefaultNamA2Size();

  const [calibrationEnabled, setCalibrationEnabled] = useParameter('calibrateInput', 'toggle');
  const [dbuValueNormalized, setDbuValueNormalized] = useParameter(
    'inputCalibrationLevel',
    'slider'
  );

  const [osEnabled, setOsEnabled] = useParameter('osEnabled', 'toggle');
  const [osFactorIndex, setOsFactorIndex] = useParameter('osFactor', 'comboBox');

  // Convert between normalized (0-1) and actual dBu values (-60 to +60 dBu):
  // JUCE WebView normalizes all slider parameters to 0-1 regardless of range.
  const dbuValue = dbuValueNormalized * 120 - 60;
  const setDbuValue = (value: number) => {
    const normalized = (value + 60) / 120;
    setDbuValueNormalized(Math.max(0, Math.min(1, normalized)));
  };

  // Draft while the calibration field is focused: committing every keystroke
  // would reformat the value under the cursor and make typing impossible
  // (can't enter "-", clear the field, or finish "12.5").
  const [dbuDraft, setDbuDraft] = useState<string | null>(null);
  const commitDbuDraft = () => {
    if (dbuDraft !== null) {
      const parsed = parseFloat(dbuDraft);
      if (!Number.isNaN(parsed)) setDbuValue(parsed);
    }
    setDbuDraft(null);
  };

  // Diagnostics: forward the on-disk log so users can share it for debugging.
  const copyLogs = useNativeFunction<boolean>('copyLogs');
  const revealLogs = useNativeFunction<string>('revealLogs');
  const [logStatus, setLogStatus] = useState<string | null>(null);

  const handleCopyLogs = useCallback(async () => {
    const ok = await copyLogs();
    setLogStatus(ok ? 'Logs copied to clipboard' : 'No log file found yet');
    setTimeout(() => setLogStatus(null), 3000);
  }, [copyLogs]);

  const handleRevealLogs = useCallback(async () => {
    const path = await revealLogs();
    setLogStatus(path ? 'Revealed log file' : 'No log file found yet');
    setTimeout(() => setLogStatus(null), 3000);
  }, [revealLogs]);

  // One control: the X steps Advanced back to the main screen, and closes
  // from there — no separate back button.
  const handleHeaderClose = useCallback(() => {
    if (screen === 'advanced') setScreen('main');
    else onClose();
  }, [onClose, screen]);

  const header = (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        marginBottom: '20px',
      }}
    >
      <span style={{ fontSize: '22px', fontWeight: 600, color: '#ffffff' }}>
        {screen === 'advanced' ? 'Advanced' : 'Settings'}
      </span>
      <button
        onClick={handleHeaderClose}
        style={{
          background: 'transparent',
          border: 'none',
          color: '#ffffff',
          cursor: 'pointer',
          display: 'flex',
          alignItems: 'center',
          padding: '4px',
        }}
      >
        <XIcon size={20} />
      </button>
    </div>
  );

  const pluginMainScreen = (
    <>
      <ToggleRow
        label="Hints"
        description="Shows a help bar under the faceplate describing the control under your pointer, with its shortcuts."
        value={hintsEnabled}
        onChange={setHintsEnabled}
      />

      {/* MIDI Learn/mapping — plugin-level (reads the processor's MIDI
          buffer), so it belongs here and works in DAW builds too. */}
      <MidiMapSettings />

      <div style={{ marginBottom: '36px' }}>
        <span style={sectionLabelStyle}>Advanced</span>
        <p style={{ ...descriptionStyle, marginBottom: '16px' }}>
          Normalization, calibration and diagnostics.
        </p>
        <button onClick={() => setScreen('advanced')} style={ctaButtonStyle}>
          Advanced
        </button>
      </div>

      {/* Version footer. When the startup check found a newer build (even if
          its modal was dismissed), offer the update here too. */}
      {(version || update) && (
        <div style={{ marginTop: '8px' }}>
          {update && (
            <a
              href={update.url}
              target="_blank"
              rel="noopener noreferrer"
              style={{
                ...ctaButtonStyle,
                display: 'block',
                boxSizing: 'border-box',
                textDecoration: 'none',
                marginBottom: '12px',
              }}
            >
              Update to v{update.version}
            </a>
          )}
          {version && (
            <p style={{ ...descriptionStyle, fontSize: '12px', color: SUBTLE, margin: 0 }}>
              TONE3000 v{version}
            </p>
          )}
        </div>
      )}
    </>
  );

  const advancedScreen = (
    <>
      <div style={{ marginBottom: '32px' }} role="radiogroup" aria-label="Default NAM A2 Size">
        <span style={sectionLabelStyle}>Default NAM A2 Size</span>
        <p style={{ ...descriptionStyle, marginBottom: '18px' }}>
          This setting is for the default size when selecting a tone.
        </p>
        {NAM_A2_MODE_OPTIONS.map((option) => (
          <RadioOption
            key={option.value}
            selected={namA2SizeMode === option.value}
            label={option.label}
            description={option.description}
            onSelect={() => setNamA2SizeMode(option.value)}
          >
            {option.value === 'perTone' && namA2SizeMode === 'perTone' && (
              <div
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: '12px',
                  marginTop: '12px',
                  marginLeft: '30px',
                }}
              >
                <span
                  style={{
                    fontSize: '13px',
                    fontWeight: 400,
                    color: '#ffffff',
                    flexShrink: 0,
                  }}
                >
                  Set Default
                </span>
                <div style={{ flex: 1, minWidth: 0 }}>
                  <SelectField
                    value={defaultNamA2Size}
                    options={NAM_A2_SIZE_OPTIONS}
                    onChange={setDefaultNamA2Size}
                  />
                </div>
              </div>
            )}
          </RadioOption>
        ))}
      </div>

      <ToggleRow
        label="Per-Block Normalization"
        description="Each block has normalization enabled, which levels output for consistent volume across signal blocks. Turning this on reveals an optional control that lets you disable normalization per block."
        value={blockNormalizeControlEnabled}
        onChange={setBlockNormalizeControlEnabled}
      />

      <ToggleRow
        label="Calibration"
        description="Matches your input level to the level the capture was made at, for accurate gain staging. Also passes true-to-life levels between captures in the chain when the capture data allows."
        value={calibrationEnabled}
        onChange={setCalibrationEnabled}
      >
        {calibrationEnabled && (
          <div style={{ marginTop: '14px' }}>
            {/* Kill the webkit number-input chrome (spinners, focus ring). */}
            <style>
              {`.settings-number-input::-webkit-outer-spin-button,
                .settings-number-input::-webkit-inner-spin-button {
                  -webkit-appearance: none;
                  margin: 0;
                }
                .settings-number-input:focus { outline: none; }`}
            </style>
            {/* 232px field with "dBu" pinned to the right; padding-right
                leaves room so typed values never run under the unit. */}
            <div style={{ position: 'relative', width: '232px' }}>
              <input
                type="number"
                className="settings-number-input"
                value={dbuDraft ?? dbuValue.toFixed(1)}
                onFocus={() => setDbuDraft(dbuValue.toFixed(1))}
                onChange={(e) => setDbuDraft(e.target.value)}
                onBlur={commitDbuDraft}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') e.currentTarget.blur();
                }}
                step="0.1"
                min="-60"
                max="60"
                placeholder="Value"
                style={{
                  ...outlinedFieldStyle,
                  width: '100%',
                  padding: '12px 52px 12px 16px',
                  appearance: 'none',
                  WebkitAppearance: 'none',
                }}
              />
              <span
                style={{
                  position: 'absolute',
                  right: '16px',
                  top: '50%',
                  transform: 'translateY(-50%)',
                  color: GRAY,
                  fontSize: '14px',
                  fontWeight: 700,
                  pointerEvents: 'none',
                }}
              >
                dBu
              </span>
            </div>
            <p style={{ ...descriptionStyle, fontSize: '12px', marginTop: '8px' }}>
              Set the dBu level that matches your DAW's max digital level. Typical values: +12 dBu
              (professional gear), +4 dBu (semi-pro).{' '}
              <a
                href={CALIBRATION_DOCS_URL}
                target="_blank"
                rel="noopener noreferrer"
                style={{ color: '#ffffff', textDecoration: 'underline' }}
              >
                Learn More
              </a>
            </p>
          </div>
        )}
      </ToggleRow>

      <ToggleRow
        label="Oversampling"
        description="Reduces aliasing. Higher rates improve quality but use more CPU."
        value={osEnabled}
        onChange={setOsEnabled}
      >
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: '12px',
            marginTop: '4px',
          }}
        >
          <span
            style={{
              fontSize: '13px',
              fontWeight: 400,
              color: osEnabled ? '#ffffff' : SUBTLE,
              flexShrink: 0,
            }}
          >
            Rate
          </span>
          <div style={{ flex: 1, minWidth: 0 }}>
            <SelectField
              value={String(osFactorIndex) as '0' | '1' | '2'}
              options={OS_FACTOR_OPTIONS}
              onChange={(v) => setOsFactorIndex(Number(v))}
              disabled={!osEnabled}
              ariaLabel="Oversampling rate"
            />
          </div>
        </div>
      </ToggleRow>

      <div>
        <span style={sectionLabelStyle}>Diagnostics</span>
        <p style={{ ...descriptionStyle, marginBottom: '16px' }}>
          Copy recent diagnostic logs to the clipboard and paste them into a bug report.
        </p>
        <button onClick={handleCopyLogs} style={ctaButtonStyle}>
          Copy Logs
        </button>
        <button
          onClick={handleRevealLogs}
          style={{
            background: 'transparent',
            border: 'none',
            color: SUBTLE,
            fontSize: '12px',
            cursor: 'pointer',
            padding: '10px 0 0',
          }}
        >
          Reveal log file on disk
        </button>
        {logStatus && (
          <p style={{ ...descriptionStyle, fontSize: '12px', marginTop: '8px' }}>{logStatus}</p>
        )}
      </div>
    </>
  );

  const pluginTab = screen === 'advanced' ? advancedScreen : pluginMainScreen;

  return (
    <div
      style={{
        position: 'absolute',
        top: 0,
        left: 0,
        right: 0,
        bottom: 0,
        backgroundColor: '#000000',
        zIndex: 2000,
        overflow: 'auto',
      }}
    >
      <div
        style={{
          maxWidth: '480px',
          margin: '0 auto',
          padding: '28px 24px 40px',
          color: '#ffffff',
          boxSizing: 'border-box',
        }}
      >
        {header}
        {/* One tab (hosted) = no tab bar. Advanced hides it too — it's a
            sub-screen of the plugin tab, and the header X steps back. */}
        {standalone && screen === 'main' && <TabBar active={tab} onChange={setTab} />}
        {tab === 'system' && standalone && screen === 'main' ? (
          <SystemSettings device={device} />
        ) : (
          pluginTab
        )}
      </div>
    </div>
  );
};

export default Settings;
