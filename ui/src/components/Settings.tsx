import React, { useCallback, useEffect, useRef, useState } from 'react';
import { X as XIcon, Info, ChevronDown } from 'lucide-react';
import { useParameter } from '../hooks/useParameter';
import { useNativeFunction } from '../hooks/useFunction';
import { setHintsEnabled, useHintsEnabled } from './helpText';
import type { InputMode } from '../types/chain';
import { MUTED, SUBTLE } from './theme';

/**
 * Settings: full-window takeover with a main screen (section cards that open
 * the JUCE audio settings or the Advanced sub-screen) and an Advanced screen
 * (normalization / calibration / diagnostics). Audio settings — including
 * the input channel picker — only exist in the standalone app; hosts manage
 * routing and buffers themselves.
 */

interface SettingsProps {
  /** Mounted only while open (see Plugin) — closing unmounts, so screen
      state and parameter subscriptions reset for free. */
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

// Only headers carry weight; everything else is regular (the app's global
// stylesheet defaults heavier, so body copy sets 400 explicitly).
const sectionLabelStyle: React.CSSProperties = {
  fontSize: '15px',
  fontWeight: 600,
  color: '#ffffff',
};

const descriptionStyle: React.CSSProperties = {
  fontSize: '13px',
  fontWeight: 400,
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
  fontWeight: 400,
  cursor: 'pointer',
  textAlign: 'center',
};

/** Green pill switch mirroring the web ToggleSimple: 40×24 track (zinc-500
    off, #00D13B on), 16px white knob with a 4px inset, 300ms ease. */
const PillToggle: React.FC<{ value: boolean; onChange: (value: boolean) => void }> = ({
  value,
  onChange,
}) => (
  <button
    role="switch"
    aria-checked={value}
    onClick={() => onChange(!value)}
    style={{
      position: 'relative',
      width: '40px',
      height: '24px',
      borderRadius: '12px',
      border: 'none',
      padding: 0,
      cursor: 'pointer',
      backgroundColor: value ? '#00D13B' : '#71717a',
      boxShadow: 'inset 0 2px 4px rgba(0, 0, 0, 0.15)',
      flexShrink: 0,
      transition: 'background-color 0.3s ease-in-out',
    }}
  >
    <span
      style={{
        position: 'absolute',
        top: '4px',
        left: '4px',
        width: '16px',
        height: '16px',
        borderRadius: '50%',
        backgroundColor: '#ffffff',
        boxShadow: '0 1px 2px rgba(0, 0, 0, 0.3)',
        transform: value ? 'translateX(16px)' : 'translateX(0)',
        transition: 'transform 0.3s ease-in-out',
        display: 'block',
      }}
    />
  </button>
);

/** Custom dropdown select styled like the plugin's other pickers (model
    select dropdown): grey trigger, dark panel, hover-highlight rows. */
const SelectField: React.FC<{
  value: InputMode;
  options: { value: InputMode; label: string }[];
  onChange: (value: InputMode) => void;
}> = ({ value, options, onChange }) => {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    if (!open) return;
    const onPointerDown = (e: PointerEvent) => {
      if (!rootRef.current?.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [open]);

  const selected = options.find((option) => option.value === value);

  return (
    <div ref={rootRef} style={{ position: 'relative' }}>
      <button
        onClick={() => setOpen((prev) => !prev)}
        style={{
          width: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          gap: '10px',
          padding: '12px 14px 12px 16px',
          borderRadius: '8px',
          border: 'none',
          backgroundColor: 'rgba(120, 120, 128, 0.36)',
          color: '#ffffff',
          fontSize: '14px',
          fontWeight: 400,
          cursor: 'pointer',
          boxSizing: 'border-box',
        }}
      >
        {selected?.label}
        <ChevronDown
          size={16}
          style={{
            color: MUTED,
            flexShrink: 0,
            transform: open ? 'rotate(180deg)' : 'none',
            transition: 'transform 0.15s ease',
          }}
        />
      </button>

      {open && (
        <div
          style={{
            position: 'absolute',
            top: 'calc(100% + 4px)',
            left: 0,
            right: 0,
            borderRadius: '8px',
            background: '#39393D',
            overflow: 'hidden',
            zIndex: 100,
          }}
        >
          {options.map((option, index) => (
            <div
              key={option.value}
              onClick={() => {
                onChange(option.value);
                setOpen(false);
              }}
              onMouseEnter={(e) => {
                if (option.value !== value)
                  e.currentTarget.style.background = 'rgba(255, 255, 255, 0.1)';
              }}
              onMouseLeave={(e) => {
                if (option.value !== value) e.currentTarget.style.background = 'transparent';
              }}
              style={{
                padding: '12px 16px',
                cursor: 'pointer',
                color: '#ffffff',
                fontSize: '14px',
                fontWeight: 400,
                background: option.value === value ? 'rgba(255, 255, 255, 0.1)' : 'transparent',
                borderBottom:
                  index < options.length - 1 ? '1px solid rgba(84, 84, 88, 0.65)' : 'none',
              }}
            >
              {option.label}
            </div>
          ))}
        </div>
      )}
    </div>
  );
};

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
  onClose,
  standalone,
  inputMode,
  onSetInputMode,
}) => {
  const [screen, setScreen] = useState<'main' | 'advanced'>('main');

  const hintsEnabled = useHintsEnabled();

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

  const showAudioSettings = useNativeFunction<boolean>('showAudioSettings');

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
        marginBottom: '28px',
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
            <SelectField value={inputMode} options={INPUT_MODE_OPTIONS} onChange={onSetInputMode} />
            <p style={{ ...descriptionStyle, fontSize: '12px' }}>
              Use mono for a single instrument cable (e.g. guitar in input 1).
            </p>
          </div>

          <button onClick={() => showAudioSettings()} style={ctaButtonStyle}>
            Audio Settings
          </button>
        </div>
      )}

      <ToggleRow
        label="Hints"
        description="Shows a help bar under the faceplate describing the control under your pointer, with its shortcuts."
        value={hintsEnabled}
        onChange={setHintsEnabled}
      />

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
                width: '100%',
                boxSizing: 'border-box',
                padding: '12px 16px',
                borderRadius: '8px',
                border: 'none',
                backgroundColor: 'rgba(120, 120, 128, 0.36)',
                color: '#ffffff',
                fontSize: '14px',
                fontWeight: 400,
                appearance: 'none',
                WebkitAppearance: 'none',
                outline: 'none',
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
          maxWidth: '600px',
          margin: '0 auto',
          padding: '28px 24px 40px',
          color: '#ffffff',
          boxSizing: 'border-box',
        }}
      >
        {header}
        {screen === 'advanced' ? advancedScreen : mainScreen}
      </div>
    </div>
  );
};

export default Settings;
