import React, { useCallback, useState } from 'react';
import {
  X as XIcon,
  Settings as SettingsIcon,
  Volume2,
  ClipboardCopy,
  FolderOpen,
} from 'lucide-react';
import { ToggleControl } from './ToggleControl';
import { useParameter } from '../hooks/useParameter';
import { useFunction } from '../hooks/useFunction';
import type { InputMode } from '../types/chain';

interface SettingsProps {
  isOpen: boolean;
  onClose: () => void;
  /** True in the standalone app — shows standalone-only settings. */
  standalone: boolean;
  inputMode: InputMode;
  onSetInputMode: (mode: InputMode) => void;
}

const INPUT_MODE_OPTIONS: { value: InputMode; label: string }[] = [
  { value: 'input1', label: 'INPUT 1' },
  { value: 'input2', label: 'INPUT 2' },
  { value: 'stereo', label: 'STEREO' },
];

export const Settings: React.FC<SettingsProps> = ({
  isOpen,
  onClose,
  standalone,
  inputMode,
  onSetInputMode,
}) => {
  // Use JUCE parameters for all settings
  const [normalizationEnabled, setNormalizationEnabled] = useParameter('normalize', 'toggle');
  const [calibrationEnabled, setCalibrationEnabled] = useParameter('calibrateInput', 'toggle');
  const [dbuValueNormalized, setDbuValueNormalized] = useParameter(
    'inputCalibrationLevel',
    'slider'
  );

  // Convert between normalized (0-1) and actual dBu values (-60 to +60 dBu)
  // JUCE WebView normalizes all slider parameters to 0-1 regardless of their defined range
  const dbuValue = dbuValueNormalized * 120 - 60; // Convert 0-1 to -60 to +60
  const setDbuValue = (value: number) => {
    const normalized = (value + 60) / 120; // Convert -60 to +60 to 0-1
    setDbuValueNormalized(Math.max(0, Math.min(1, normalized))); // Clamp to 0-1
  };

  // State to track if we're in standalone mode
  const [isStandalone, setIsStandalone] = useState<boolean | null>(null);
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

  // Open native audio settings dialog (standalone only)
  const handleOpenAudioSettings = useCallback(async () => {
    try {
      const result = await showAudioSettings.invoke();
      // If result is false, we're not in standalone mode
      if (result === false) {
        setIsStandalone(false);
      } else {
        setIsStandalone(true);
      }
    } catch (error) {
      console.error('Failed to open audio settings:', error);
      setIsStandalone(false);
    }
  }, [showAudioSettings]);

  if (!isOpen) return null;

  return (
    <div
      style={{
        position: 'absolute',
        top: 0,
        left: 0,
        right: 0,
        bottom: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.9)',
        backdropFilter: 'blur(4px)',
        zIndex: 2000,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}
    >
      <div
        style={{
          backgroundColor: '#000000',
          border: '2px solid #ffffff',
          borderRadius: '8px',
          width: '400px',
          maxWidth: '90vw',
          maxHeight: '80vh',
          overflow: 'auto',
          color: '#ffffff',
        }}
      >
        {/* Header */}
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
            padding: '20px',
            borderBottom: '1px solid #ffffff',
          }}
        >
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '10px',
              fontSize: '18px',
              fontWeight: 'bold',
            }}
          >
            <SettingsIcon size={20} />
            Settings
          </div>
          <button
            onClick={onClose}
            style={{
              background: 'transparent',
              border: '1px solid #ffffff',
              color: '#ffffff',
              width: '32px',
              height: '32px',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: 'pointer',
              borderRadius: '4px',
            }}
          >
            <XIcon size={18} />
          </button>
        </div>

        {/* Settings Content */}
        <div style={{ padding: '20px' }}>
          {/* Input channel mode (standalone only): interfaces expose stereo
              pairs even when only one jack is plugged in, so the user picks
              what actually carries signal. */}
          {standalone && (
            <div style={{ marginBottom: '30px' }}>
              <div
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'space-between',
                  marginBottom: '8px',
                }}
              >
                <label
                  style={{
                    fontSize: '16px',
                    fontWeight: '500',
                    color: '#ffffff',
                  }}
                >
                  Input
                </label>
                <div
                  style={{
                    display: 'flex',
                    flexDirection: 'row',
                    borderRadius: '4px',
                    border: '1px solid #555555',
                    overflow: 'hidden',
                  }}
                >
                  {INPUT_MODE_OPTIONS.map((option, i) => (
                    <button
                      key={option.value}
                      onClick={() => onSetInputMode(option.value)}
                      style={{
                        padding: '6px 12px',
                        fontSize: '12px',
                        border: 'none',
                        borderLeft: i > 0 ? '1px solid #555555' : 'none',
                        cursor: 'pointer',
                        color: '#ffffff',
                        letterSpacing: '0.04em',
                        backgroundColor:
                          inputMode === option.value ? 'rgba(235, 235, 245, 0.18)' : '#222222',
                      }}
                    >
                      {option.label}
                    </button>
                  ))}
                </div>
              </div>
              <p
                style={{
                  fontSize: '12px',
                  color: '#cccccc',
                  margin: 0,
                  lineHeight: '1.4',
                }}
              >
                Which input channel carries your signal. Use a mono input for a single
                instrument cable (e.g. guitar into input 1); stereo uses both channels.
              </p>
            </div>
          )}

          {/* Normalization Setting */}
          <div style={{ marginBottom: '30px' }}>
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                marginBottom: '8px',
              }}
            >
              <label
                style={{
                  fontSize: '16px',
                  fontWeight: '500',
                  color: '#ffffff',
                }}
              >
                Normalization
              </label>
              <ToggleControl
                label=""
                value={normalizationEnabled}
                onChange={setNormalizationEnabled}
                accentColor="#0000ff"
              />
            </div>
            <p
              style={{
                fontSize: '12px',
                color: '#cccccc',
                margin: 0,
                lineHeight: '1.4',
              }}
            >
              Automatically normalize audio levels for consistent output across different models.
            </p>
          </div>

          {/* Calibration Setting */}
          <div style={{ marginBottom: '20px' }}>
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                marginBottom: '8px',
              }}
            >
              <label
                style={{
                  fontSize: '16px',
                  fontWeight: '500',
                  color: '#ffffff',
                }}
              >
                Calibration
              </label>
              <ToggleControl
                label=""
                value={calibrationEnabled}
                onChange={setCalibrationEnabled}
                accentColor="#ff0000"
              />
            </div>
            <p
              style={{
                fontSize: '12px',
                color: '#cccccc',
                margin: '0 0 12px 0',
                lineHeight: '1.4',
              }}
            >
              Enable to match analog signal strength for accurate gain staging.
              <br />
              Set the dBu value that corresponds to your DAW's maximum digital level.
            </p>

            {/* dBu Input Field */}
            {calibrationEnabled && (
              <div
                style={{
                  marginTop: '12px',
                  padding: '12px',
                  backgroundColor: '#111111',
                  border: '1px solid #333333',
                  borderRadius: '4px',
                }}
              >
                <div
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: '10px',
                    marginBottom: '8px',
                  }}
                >
                  <label
                    style={{
                      fontSize: '14px',
                      color: '#ffffff',
                      minWidth: '80px',
                    }}
                  >
                    dBu Value:
                  </label>
                  <input
                    type="number"
                    value={dbuValue.toFixed(1)}
                    onChange={(e) => setDbuValue(parseFloat(e.target.value) || 0)}
                    step="0.1"
                    min="-60"
                    max="60"
                    style={{
                      backgroundColor: '#222222',
                      border: '1px solid #555555',
                      color: '#ffffff',
                      padding: '6px 10px',
                      borderRadius: '4px',
                      fontSize: '14px',
                      width: '80px',
                    }}
                  />
                  <span
                    style={{
                      fontSize: '14px',
                      color: '#cccccc',
                    }}
                  >
                    dBu
                  </span>
                </div>
                <p
                  style={{
                    fontSize: '11px',
                    color: '#999999',
                    margin: 0,
                    lineHeight: '1.3',
                  }}
                >
                  Typical values: +12 dBu for professional gear, +4 dBu for semi-pro.
                  <br />
                  See NAM documentation for measurement instructions.
                </p>
              </div>
            )}
          </div>

          {/* Audio Settings (Standalone only) */}
          <div
            style={{
              paddingTop: '20px',
              borderTop: '1px solid #333333',
            }}
          >
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                marginBottom: '8px',
              }}
            >
              <label
                style={{
                  fontSize: '16px',
                  fontWeight: '500',
                  color: '#ffffff',
                }}
              >
                Audio Settings
              </label>
              <button
                onClick={handleOpenAudioSettings}
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: '8px',
                  padding: '8px 16px',
                  backgroundColor: '#222222',
                  border: '1px solid #555555',
                  borderRadius: '4px',
                  color: '#ffffff',
                  fontSize: '14px',
                  cursor: 'pointer',
                }}
              >
                <Volume2 size={16} />
                Open
              </button>
            </div>
            <p
              style={{
                fontSize: '12px',
                color: '#cccccc',
                margin: 0,
                lineHeight: '1.4',
              }}
            >
              Configure audio device, sample rate, and buffer size.
              {isStandalone === true && (
                <span style={{ color: '#88ff88', display: 'block', marginTop: '4px' }}>
                  Use the menu bar: Options → Audio/MIDI Settings
                </span>
              )}
              {isStandalone === false && (
                <span style={{ color: '#ff6666', display: 'block', marginTop: '4px' }}>
                  Only available in standalone mode. Your DAW manages audio settings.
                </span>
              )}
            </p>
          </div>

          {/* Diagnostics */}
          <div
            style={{
              paddingTop: '20px',
              marginTop: '20px',
              borderTop: '1px solid #333333',
            }}
          >
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                marginBottom: '8px',
                gap: '8px',
              }}
            >
              <label
                style={{
                  fontSize: '16px',
                  fontWeight: '500',
                  color: '#ffffff',
                }}
              >
                Diagnostics
              </label>
              <div style={{ display: 'flex', gap: '8px' }}>
                <button
                  onClick={handleCopyLogs}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: '8px',
                    padding: '8px 16px',
                    backgroundColor: '#222222',
                    border: '1px solid #555555',
                    borderRadius: '4px',
                    color: '#ffffff',
                    fontSize: '14px',
                    cursor: 'pointer',
                  }}
                >
                  <ClipboardCopy size={16} />
                  Copy logs
                </button>
                <button
                  onClick={handleRevealLogs}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: '8px',
                    padding: '8px 16px',
                    backgroundColor: '#222222',
                    border: '1px solid #555555',
                    borderRadius: '4px',
                    color: '#ffffff',
                    fontSize: '14px',
                    cursor: 'pointer',
                  }}
                >
                  <FolderOpen size={16} />
                  Reveal
                </button>
              </div>
            </div>
            <p
              style={{
                fontSize: '12px',
                color: '#cccccc',
                margin: 0,
                lineHeight: '1.4',
              }}
            >
              Copy recent diagnostic logs to the clipboard (paste them into a bug report) or reveal
              the log file on disk.
              {logStatus && (
                <span style={{ color: '#88ff88', display: 'block', marginTop: '4px' }}>
                  {logStatus}
                </span>
              )}
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};

export default Settings;
