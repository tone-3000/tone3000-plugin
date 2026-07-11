import React, { useCallback, useEffect, useState } from 'react';
import { X as XIcon, ArrowLeft, Info, ChevronDown } from 'lucide-react';
import { useParameter } from '../hooks/useParameter';
import { useFunction } from '../hooks/useFunction';
import type { InputMode } from '../types/chain';

/**
 * Settings: full-window takeover with a main screen (section cards that open
 * the JUCE audio settings or the Advanced sub-screen) and an Advanced screen
 * (normalization / calibration / diagnostics). Audio settings — including
 * the input channel picker — only exist in the standalone app; hosts manage
 * routing and buffers themselves.
 */

interface SettingsProps {
  isOpen: boolean;
  onClose: () => void;
  /** True in the standalone app — shows standalone-only settings. */
  standalone: boolean;
  inputMode: InputMode;
  onSetInputMode: (mode: InputMode) => void;
}

const INPUT_MODE_OPTIONS: { value: InputMode; label: string }[] = [
  { value: 'input1', label: 'Input 1 (mono)' },
  { value: 'input2', label: 'Input 2 (mono)' },
  { value: 'stereo', label: 'Stereo (inputs 1 + 2)' },
];

const MUTED = 'rgba(235, 235, 245, 0.60)';
const SUBTLE = 'rgba(235, 235, 245, 0.40)';
const FIELD_BORDER = '1px solid rgba(84, 84, 88, 0.65)';

const sectionLabelStyle: React.CSSProperties = {
  fontSize: '15px',
  fontWeight: 600,
  color: '#ffffff',
};

const descriptionStyle: React.CSSProperties = {
  fontSize: '13px',
  color: MUTED,
  margin: '6px 0 0',
  lineHeight: 1.45,
};

const ctaButtonStyle: React.CSSProperties = {
  width: '100%',
  padding: '12px 16px',
  borderRadius: '10px',
  border: '1px solid #ffffff',
  backgroundColor: 'transparent',
  color: '#ffffff',
  fontSize: '15px',
  cursor: 'pointer',
  textAlign: 'center',
};

const fieldLabelStyle: React.CSSProperties = {
  fontSize: '12px',
  color: SUBTLE,
  display: 'block',
  marginBottom: '6px',
};

/** Grayscale pill switch (iOS-style, on brand: black/white/gray only). */
const PillToggle: React.FC<{ value: boolean; onChange: (value: boolean) => void }> = ({
  value,
  onChange,
}) => (
  <button
    role="switch"
    aria-checked={value}
    onClick={() => onChange(!value)}
    style={{
      width: '44px',
      height: '26px',
      borderRadius: '13px',
      border: 'none',
      padding: '2px',
      cursor: 'pointer',
      backgroundColor: value ? '#ffffff' : '#2C2C2E',
      display: 'flex',
      justifyContent: value ? 'flex-end' : 'flex-start',
      flexShrink: 0,
      transition: 'background-color 0.15s ease',
    }}
  >
    <span
      style={{
        width: '22px',
        height: '22px',
        borderRadius: '50%',
        backgroundColor: value ? '#000000' : '#8D8D93',
        transition: 'background-color 0.15s ease',
      }}
    />
  </button>
);

/** Section label with a pill toggle on the right, description underneath. */
const ToggleRow: React.FC<{
  label: string;
  description: React.ReactNode;
  value: boolean;
  onChange: (value: boolean) => void;
  children?: React.ReactNode;
}> = ({ label, description, value, onChange, children }) => (
  <div style={{ marginBottom: '32px' }}>
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: '16px',
      }}
    >
      <span style={sectionLabelStyle}>{label}</span>
      <PillToggle value={value} onChange={onChange} />
    </div>
    <p style={descriptionStyle}>{description}</p>
    {children}
  </div>
);

export const Settings: React.FC<SettingsProps> = ({
  isOpen,
  onClose,
  standalone,
  inputMode,
  onSetInputMode,
}) => {
  const [screen, setScreen] = useState<'main' | 'advanced'>('main');

  const [normalizationEnabled, setNormalizationEnabled] = useParameter('normalize', 'toggle');
  const [calibrationEnabled, setCalibrationEnabled] = useParameter('calibrateInput', 'toggle');
  const [dbuValueNormalized, setDbuValueNormalized] = useParameter(
    'inputCalibrationLevel',
    'slider'
  );

  // Convert between normalized (0-1) and actual dBu values (-60 to +60 dBu):
  // JUCE WebView normalizes all slider parameters to 0-1 regardless of range.
  const dbuValue = dbuValueNormalized * 120 - 60;
  const setDbuValue = (value: number) => {
    const normalized = (value + 60) / 120;
    setDbuValueNormalized(Math.max(0, Math.min(1, normalized)));
  };

  const showAudioSettings = useFunction<boolean>('showAudioSettings');

  // Diagnostics: forward the on-disk log so users can share it for debugging.
  const copyLogs = useFunction<boolean>('copyLogs');
  const revealLogs = useFunction<string>('revealLogs');
  const [logStatus, setLogStatus] = useState<string | null>(null);

  const handleCopyLogs = useCallback(async () => {
    const ok = await copyLogs.invoke();
    setLogStatus(ok ? 'Logs copied to clipboard' : 'No log file found yet');
    setTimeout(() => setLogStatus(null), 3000);
  }, [copyLogs]);

  const handleRevealLogs = useCallback(async () => {
    const path = await revealLogs.invoke();
    setLogStatus(path ? 'Revealed log file' : 'No log file found yet');
    setTimeout(() => setLogStatus(null), 3000);
  }, [revealLogs]);

  // Always land on the main screen when (re)opened.
  useEffect(() => {
    if (isOpen) setScreen('main');
  }, [isOpen]);

  if (!isOpen) return null;

  const header = (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        marginBottom: '28px',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
        {screen === 'advanced' && (
          <button
            onClick={() => setScreen('main')}
            title="Back to settings"
            style={{
              background: 'transparent',
              border: 'none',
              color: '#ffffff',
              cursor: 'pointer',
              display: 'flex',
              alignItems: 'center',
              padding: 0,
            }}
          >
            <ArrowLeft size={20} />
          </button>
        )}
        <span style={{ fontSize: '22px', fontWeight: 700, color: '#ffffff' }}>
          {screen === 'advanced' ? 'Advanced' : 'Settings'}
        </span>
      </div>
      <button
        onClick={onClose}
        title="Close settings"
        style={{
          background: 'transparent',
          border: 'none',
          color: MUTED,
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

  const mainScreen = (
    <>
      {standalone && (
        <div
          style={{
            display: 'flex',
            alignItems: 'flex-start',
            gap: '10px',
            marginBottom: '32px',
          }}
        >
          <Info size={16} style={{ color: MUTED, flexShrink: 0, marginTop: '1px' }} />
          <p style={{ ...descriptionStyle, margin: 0 }}>
            Experiencing latency? Lower your "Audio Buffer Size" to 256 samples or less in Audio
            Settings.
          </p>
        </div>
      )}

      {standalone && (
        <div style={{ marginBottom: '36px' }}>
          <span style={sectionLabelStyle}>Audio Settings</span>
          <p style={{ ...descriptionStyle, marginBottom: '16px' }}>
            Configure your interface, sample rate, buffer size and more.
          </p>

          {/* Input channel picker: interfaces expose stereo pairs even when
              only one jack is plugged in, so pick what carries signal. */}
          <div style={{ marginBottom: '16px' }}>
            <label style={fieldLabelStyle}>Input Channels</label>
            <div style={{ position: 'relative' }}>
              <select
                value={inputMode}
                onChange={(e) => onSetInputMode(e.target.value as InputMode)}
                style={{
                  width: '100%',
                  padding: '12px 40px 12px 16px',
                  borderRadius: '10px',
                  border: FIELD_BORDER,
                  backgroundColor: '#111111',
                  color: '#ffffff',
                  fontSize: '14px',
                  cursor: 'pointer',
                  appearance: 'none',
                  WebkitAppearance: 'none',
                }}
              >
                {INPUT_MODE_OPTIONS.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <ChevronDown
                size={16}
                style={{
                  position: 'absolute',
                  right: '14px',
                  top: '50%',
                  transform: 'translateY(-50%)',
                  color: MUTED,
                  pointerEvents: 'none',
                }}
              />
            </div>
            <p style={{ ...descriptionStyle, fontSize: '12px' }}>
              Use a mono input for a single instrument cable (e.g. guitar into input 1).
            </p>
          </div>

          <button onClick={() => showAudioSettings.invoke()} style={ctaButtonStyle}>
            Audio Settings
          </button>
        </div>
      )}

      <div style={{ marginBottom: '36px' }}>
        <span style={sectionLabelStyle}>Advanced</span>
        <p style={{ ...descriptionStyle, marginBottom: '16px' }}>
          Normalization, calibration and diagnostics.
        </p>
        <button onClick={() => setScreen('advanced')} style={ctaButtonStyle}>
          Advanced
        </button>
      </div>
    </>
  );

  const advancedScreen = (
    <>
      <ToggleRow
        label="Normalization"
        description="Levels output across different NAM captures for consistent volume. Recommended on."
        value={normalizationEnabled}
        onChange={setNormalizationEnabled}
      />

      <ToggleRow
        label="Calibration"
        description="Matches your input level to the level the capture was made at, for accurate gain staging."
        value={calibrationEnabled}
        onChange={setCalibrationEnabled}
      >
        {calibrationEnabled && (
          <div style={{ marginTop: '14px' }}>
            <input
              type="number"
              value={dbuValue.toFixed(1)}
              onChange={(e) => setDbuValue(parseFloat(e.target.value) || 0)}
              step="0.1"
              min="-60"
              max="60"
              placeholder="Value"
              style={{
                width: '100%',
                boxSizing: 'border-box',
                padding: '12px 16px',
                borderRadius: '10px',
                border: FIELD_BORDER,
                backgroundColor: '#111111',
                color: '#ffffff',
                fontSize: '14px',
              }}
            />
            <p style={{ ...descriptionStyle, fontSize: '12px', marginTop: '8px' }}>
              Set the dBu level that matches your DAW's max digital level. Typical values: +12 dBu
              (professional gear), +4 dBu (semi-pro).
            </p>
          </div>
        )}
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
          maxWidth: '460px',
          margin: '0 auto',
          padding: '48px 24px 40px',
          color: '#ffffff',
        }}
      >
        {header}
        {screen === 'advanced' ? advancedScreen : mainScreen}
      </div>
    </div>
  );
};

export default Settings;
