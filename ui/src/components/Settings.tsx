import React, { useCallback, useEffect, useState } from 'react';
import { X as XIcon, Laptop, Info, Gauge, Equal } from './icons';
import { useParameter } from '../hooks/useParameter';
import { useNativeFunction } from '../hooks/useFunction';
import { setHintsEnabled, useHintsEnabled } from './helpText';
import {
  setBlockNormalizeControlEnabled,
  setBlockSizeControlEnabled,
  useBlockNormalizeControlEnabled,
  useBlockSizeControlEnabled,
} from './uiPreferences';
import type { UpdateNoticeData } from '../hooks/useUpdateNotice';
import type { AudioDevice } from '../hooks/useAudioDevice';
import type { ChainItem } from '../types/chain';
import { isSlimSizeFull, SLIM_SIZE_FULL, SLIM_SIZE_LITE } from '../types/chain';
import {
  GRAY,
  LINK_BLUE,
  MUTED,
  SUBTLE,
  WHITE,
  segmentedCellStyle,
  segmentedGroupStyle,
} from './theme';
import {
  FIELD_BORDER,
  FieldRow,
  RadioOption,
  SECTION_GAP,
  SelectField,
  ToggleRow,
  ctaButtonStyle,
  descriptionStyle,
  outlinedFieldStyle,
  sectionLabelStyle,
} from './controls';
import { SystemSettings } from './SystemSettings';
import { MidiMapSettings } from './MidiMapSettings';

/** Inline LITE/FULL chrome matching the block-header toggle, for Settings
    copy that points at that control. Decorative only (not interactive). */
const LiteFullTogglePreview: React.FC = () => (
  <span
    aria-hidden
    style={{
      ...segmentedGroupStyle(),
      display: 'inline-flex',
      verticalAlign: 'middle',
      margin: '0 2rem',
      cursor: 'default',
    }}
  >
    {(['LITE', 'FULL'] as const).map((label) => (
      <span
        key={label}
        style={{
          ...segmentedCellStyle(),
          cursor: 'default',
          // Both muted: an example of the control, not a selection.
          color: MUTED,
        }}
      >
        <span className="cap-trim">{label}</span>
      </span>
    ))}
  </span>
);

// External docs: how to measure your rig's calibration levels.
const CALIBRATION_DOCS_URL =
  'https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/calibration.html';

/**
 * Settings: full-window takeover, tabbed between System Settings (the
 * bespoke audio device + MIDI hardware panel; first because setup is the
 * main abandon risk) and Plugin Settings (one scrollable screen of options,
 * including inline MIDI Mapping). The System tab only exists in the
 * standalone app (hosts own devices, sample rate and buffer size arrive as
 * facts from the DAW), and with one tab the tab bar drops away.
 */

export type SettingsTab = 'plugin' | 'system';

interface SettingsProps {
  /** Mounted only while open (see Plugin); closing unmounts, so screen
      state and parameter subscriptions reset for free. */
  onClose: () => void;
  /** True in the standalone app; enables the System Settings tab. */
  standalone: boolean;
  /** Shared audio device state/actions (also drives the app banner). */
  device: AudioDevice;
  /** Tab to open on (defaults to System; banner actions land there too). */
  initialTab?: SettingsTab;
  /** Running build version ("" outside the plugin). */
  version: string;
  /** Newer published build, if the startup check found one (even if the
      startup modal was dismissed); shows an update button in the footer. */
  update: UpdateNoticeData | null;
  /** Default NAM A2 size for newly added blocks (0 = lite, 1 = full; see
      ChainState.namSlimSizeDefault). */
  namSlimSizeDefault: number;
  onNamSlimSizeDefaultChange: (slimSize: number) => void;
  /** Multi-core processing (spreads stereo chains and oversampled NAM
      models across CPU cores). */
  multiCore: boolean;
  onMultiCoreChange: (enabled: boolean) => void;
  /** Chain lanes; MIDI Mapping names block-power targets after the tone
      currently in each slot. `chainRight` is null outside stereo. */
  chain: ChainItem[];
  chainRight: ChainItem[] | null;
}

// Oversampling rate choices. Values are the osFactor parameter's choice
// indices (as strings for SelectField); the DSP maps index i to 2^(i+1).
const OS_FACTOR_OPTIONS: { value: '0' | '1' | '2'; label: string }[] = [
  { value: '0', label: '2X - Default' },
  { value: '1', label: '4X' },
  { value: '2', label: '8X' },
];

// Default NAM A2 size for newly added blocks (machine-wide). Each block's
// own size lives in the chain state and rides presets; this only decides
// what a fresh block starts at.
const NAM_A2_SIZE_OPTIONS: { slimSize: number; label: string; description: string }[] = [
  { slimSize: SLIM_SIZE_LITE, label: 'A2-Lite', description: 'Sounds great and uses less CPU' },
  { slimSize: SLIM_SIZE_FULL, label: 'A2-Full', description: 'Maximum accuracy model' },
];

/** Full-width tab bar (mockup style: optional icon + label, active underline).
 *  System first: device setup is the main abandon risk. */
const TabBar: React.FC<{
  active: SettingsTab;
  onChange: (tab: SettingsTab) => void;
}> = ({ active, onChange }) => {
  const tabs: { id: SettingsTab; label: string; icon?: React.ReactNode }[] = [
    { id: 'system', label: 'System Settings', icon: <Laptop size={16} /> },
    { id: 'plugin', label: 'Plugin Settings' },
  ];
  return (
    <div
      role="tablist"
      style={{ display: 'flex', borderBottom: FIELD_BORDER, marginBottom: '28rem' }}
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
              gap: '8rem',
              padding: '12rem 0',
              background: 'transparent',
              border: 'none',
              borderBottom: `2rem solid ${selected ? '#ffffff' : 'transparent'}`,
              marginBottom: '-1rem',
              color: selected ? '#ffffff' : SUBTLE,
              fontSize: '14rem',
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
  initialTab = 'system',
  version,
  update,
  namSlimSizeDefault,
  onNamSlimSizeDefaultChange,
  multiCore,
  onMultiCoreChange,
  chain,
  chainRight,
}) => {
  const [tab, setTab] = useState<SettingsTab>(standalone ? initialTab : 'plugin');

  const hintsEnabled = useHintsEnabled();
  const blockNormalizeControlEnabled = useBlockNormalizeControlEnabled();
  const blockSizeControlEnabled = useBlockSizeControlEnabled();

  const [calibrationEnabled, setCalibrationEnabled] = useParameter('calibrateInput', 'toggle');
  const [dbuValueNormalized, setDbuValueNormalized] = useParameter(
    'inputCalibrationLevel',
    'slider'
  );

  const [loudnessNormalized, setLoudnessNormalized] = useParameter('targetLoudness', 'slider');

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

  // Same normalized<->display dance for the loudness target, whose native
  // range is -60..0 dB.
  const loudnessValue = loudnessNormalized * 60 - 60;
  const setLoudnessValue = (value: number) => {
    const normalized = (value + 60) / 60;
    setLoudnessNormalized(Math.max(0, Math.min(1, normalized)));
  };
  const [loudnessDraft, setLoudnessDraft] = useState<string | null>(null);
  const commitLoudnessDraft = () => {
    if (loudnessDraft !== null) {
      const parsed = parseFloat(loudnessDraft);
      if (!Number.isNaN(parsed)) setLoudnessValue(parsed);
    }
    setLoudnessDraft(null);
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

  // Web Inspector (macOS only; supported=false hides the toggle). The
  // preference is machine-wide and native applies it to the live webview,
  // so a plain optimistic local mirror is enough here.
  const getWebInspectorEnabled = useNativeFunction<{ supported: boolean; enabled: boolean }>(
    'getWebInspectorEnabled'
  );
  const setWebInspectorEnabled = useNativeFunction<boolean>('setWebInspectorEnabled');
  const [webInspector, setWebInspector] = useState<{
    supported: boolean;
    enabled: boolean;
  } | null>(null);
  useEffect(() => {
    getWebInspectorEnabled().then(setWebInspector);
  }, [getWebInspectorEnabled]);
  const handleWebInspectorChange = useCallback(
    (enabled: boolean) => {
      setWebInspector({ supported: true, enabled });
      void setWebInspectorEnabled(enabled);
    },
    [setWebInspectorEnabled]
  );

  const header = (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        marginBottom: '20rem',
      }}
    >
      <span style={{ fontSize: '22rem', fontWeight: 600, color: '#ffffff' }}>Settings</span>
      <button
        onClick={onClose}
        style={{
          background: 'transparent',
          border: 'none',
          color: '#ffffff',
          cursor: 'pointer',
          display: 'flex',
          alignItems: 'center',
          padding: '4rem',
        }}
      >
        <XIcon size={20} />
      </button>
    </div>
  );

  const pluginTab = (
    <>
      {/* Kill the webkit number-input chrome (spinners, focus ring) for every
          number field in this tab. */}
      <style>
        {`.settings-number-input::-webkit-outer-spin-button,
          .settings-number-input::-webkit-inner-spin-button {
            -webkit-appearance: none;
            margin: 0;
          }
          .settings-number-input:focus { outline: none; }`}
      </style>

      <ToggleRow
        label="Info Bar"
        description="Strip under the faceplate with hover tips and CPU load."
        value={hintsEnabled}
        onChange={setHintsEnabled}
      />

      <div style={{ marginBottom: `${SECTION_GAP}rem` }} role="radiogroup" aria-label="NAM A2 Size">
        <span style={sectionLabelStyle}>NAM A2 Size</span>
        <p style={descriptionStyle}>
          Default size for new NAM blocks. Existing blocks keep their own, so presets load as saved.
        </p>
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '16rem',
            marginTop: '16rem',
          }}
        >
          {NAM_A2_SIZE_OPTIONS.map((option) => (
            <RadioOption
              key={option.label}
              selected={isSlimSizeFull(namSlimSizeDefault) === isSlimSizeFull(option.slimSize)}
              label={option.label}
              description={option.description}
              onSelect={() => onNamSlimSizeDefaultChange(option.slimSize)}
            />
          ))}
        </div>
      </div>

      <ToggleRow
        label="Per-Block NAM Size"
        description={
          <>
            Adds a <LiteFullTogglePreview /> toggle to every NAM block. When off, a block only shows
            its size if it differs from your default.
          </>
        }
        value={blockSizeControlEnabled}
        onChange={setBlockSizeControlEnabled}
      >
        {blockSizeControlEnabled && (
          <p
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '16rem',
              margin: 0,
              fontSize: '14rem',
              fontWeight: 400,
              color: WHITE,
              lineHeight: 1.45,
            }}
          >
            <Info size={20} style={{ flexShrink: 0, color: WHITE }} aria-hidden />
            <span>
              Look for <LiteFullTogglePreview /> next to each block&apos;s power button.
            </span>
          </p>
        )}
      </ToggleRow>

      <ToggleRow
        label="Per-Block Normalization"
        description="Each block has normalization enabled, which levels output for consistent volume across signal blocks. Turning this on reveals an optional control that lets you disable normalization per block."
        value={blockNormalizeControlEnabled}
        onChange={setBlockNormalizeControlEnabled}
      >
        {blockNormalizeControlEnabled && (
          <p
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '16rem',
              margin: 0,
              fontSize: '14rem',
              fontWeight: 400,
              color: WHITE,
              lineHeight: 1.45,
            }}
          >
            <Info size={20} style={{ flexShrink: 0, color: WHITE }} aria-hidden />
            <span>
              Normalization is now controlled per block. Look for the{' '}
              <Equal
                size={12}
                style={{
                  display: 'inline',
                  verticalAlign: '-1rem',
                  margin: '0 2rem',
                  color: WHITE,
                }}
                aria-label="equals"
              />{' '}
              icon on each block, enabled by default.
            </span>
          </p>
        )}
      </ToggleRow>

      <ToggleRow
        label="Calibration"
        description="Matches your input level to the capture's original recording level, for accurate gain staging."
        value={calibrationEnabled}
        onChange={setCalibrationEnabled}
      >
        {calibrationEnabled && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '16rem' }}>
            <div style={{ position: 'relative', width: '100%' }}>
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
                  padding: '12rem 52rem 12rem 16rem',
                  appearance: 'none',
                  WebkitAppearance: 'none',
                }}
              />
              <span
                style={{
                  position: 'absolute',
                  right: '16rem',
                  top: '50%',
                  transform: 'translateY(-50%)',
                  color: GRAY,
                  fontSize: '14rem',
                  fontWeight: 700,
                  pointerEvents: 'none',
                }}
              >
                dBu
              </span>
            </div>
            <p style={{ ...descriptionStyle, margin: 0 }}>
              Set the dBu level that matches your DAW's max digital level. Typical values: +12 dBu
              (professional gear), +4 dBu (semi-pro).{' '}
              <a
                href={CALIBRATION_DOCS_URL}
                target="_blank"
                rel="noopener noreferrer"
                style={{ color: LINK_BLUE, textDecoration: 'none' }}
              >
                Learn More
              </a>
            </p>
            <p
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '16rem',
                margin: 0,
                fontSize: '14rem',
                fontWeight: 400,
                color: WHITE,
                lineHeight: 1.45,
              }}
            >
              <Info size={20} style={{ flexShrink: 0, color: WHITE }} aria-hidden />
              <span>
                Every NAM block carries a{' '}
                <Gauge
                  size={12}
                  style={{
                    display: 'inline',
                    verticalAlign: '-1rem',
                    margin: '0 2rem',
                    color: WHITE,
                  }}
                  aria-label="gauge"
                />{' '}
                icon: white when the capture includes calibration data, gray when it doesn’t.
                Calibration does nothing for a gray block — that capture never recorded the level it
                was played at.
              </span>
            </p>
            <p style={{ ...descriptionStyle, margin: 0 }}>
              When one calibrated NAM feeds another, output calibration data also sets the handoff
              level between them.
            </p>
          </div>
        )}
      </ToggleRow>

      <FieldRow
        label="Normalization Target"
        help="The loudness every normalized block is brought to. Raise it if the app is too quiet before you reach for the Output knob: normalization sits after the model, so it adds level without driving the captures harder."
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '16rem' }}>
          <div style={{ position: 'relative', width: '100%' }}>
            <input
              type="number"
              className="settings-number-input"
              value={loudnessDraft ?? loudnessValue.toFixed(1)}
              onFocus={() => setLoudnessDraft(loudnessValue.toFixed(1))}
              onChange={(e) => setLoudnessDraft(e.target.value)}
              onBlur={commitLoudnessDraft}
              onKeyDown={(e) => {
                if (e.key === 'Enter') e.currentTarget.blur();
              }}
              step="0.1"
              min="-60"
              max="0"
              placeholder="Value"
              style={{
                ...outlinedFieldStyle,
                width: '100%',
                padding: '12rem 52rem 12rem 16rem',
                appearance: 'none',
                WebkitAppearance: 'none',
              }}
            />
            <span
              style={{
                position: 'absolute',
                right: '16rem',
                top: '50%',
                transform: 'translateY(-50%)',
                color: GRAY,
                fontSize: '14rem',
                fontWeight: 700,
                pointerEvents: 'none',
              }}
            >
              dB
            </span>
          </div>
          <p style={{ ...descriptionStyle, margin: 0 }}>
            Default -18 dB, which leaves headroom for a DAW track fader. A block is moved at most 12
            dB from its capture&rsquo;s own measured loudness, so an unusually quiet or loud capture
            may not reach the target.
          </p>
        </div>
      </FieldRow>

      <ToggleRow
        label="Oversampling"
        description="Reduces aliasing. Higher rates improve quality but use more CPU."
        value={osEnabled}
        onChange={setOsEnabled}
      >
        {osEnabled && (
          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              gap: '8rem',
            }}
          >
            <span
              style={{
                fontSize: '14rem',
                fontWeight: 400,
                color: MUTED,
              }}
            >
              Rate
            </span>
            <SelectField
              value={String(osFactorIndex) as '0' | '1' | '2'}
              options={OS_FACTOR_OPTIONS}
              onChange={(v) => setOsFactorIndex(Number(v))}
              ariaLabel="Oversampling rate"
            />
          </div>
        )}
      </ToggleRow>

      <ToggleRow
        label="Multi-Core Processing"
        description="Spreads the work across CPU cores for more headroom: stereo chains process in parallel, and oversampled NAM models split across cores. Doesn't change the sound."
        value={multiCore}
        onChange={onMultiCoreChange}
      />

      {/* MIDI Learn/mapping is plugin-level (reads the processor's MIDI
          buffer), so it belongs here and works in DAW builds too. */}
      <div style={{ marginBottom: `${SECTION_GAP}rem` }}>
        <span style={sectionLabelStyle}>MIDI Mapping</span>
        <p style={descriptionStyle}>
          Control the plugin from pedals and knobs. Mappings are saved with the plugin and work in
          your DAW too.
        </p>
        <div style={{ marginTop: '16rem' }}>
          <MidiMapSettings chain={chain} chainRight={chainRight} />
        </div>
      </div>

      <div style={{ marginBottom: `${SECTION_GAP}rem` }}>
        <span style={sectionLabelStyle}>Diagnostics</span>
        <p style={descriptionStyle}>
          Copy recent diagnostic logs to the clipboard and paste them into a bug report.
        </p>
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '16rem',
            marginTop: '16rem',
          }}
        >
          <button onClick={handleCopyLogs} style={ctaButtonStyle}>
            Copy Logs
          </button>
          <button
            onClick={handleRevealLogs}
            style={{
              background: 'transparent',
              border: 'none',
              color: SUBTLE,
              fontSize: '12rem',
              cursor: 'pointer',
              padding: 0,
              textAlign: 'left',
            }}
          >
            Reveal log file on disk
          </button>
          {logStatus && (
            <p style={{ ...descriptionStyle, fontSize: '12rem', margin: 0 }}>{logStatus}</p>
          )}
        </div>
      </div>

      {webInspector?.supported && (
        <ToggleRow
          label="Inspector"
          description="Debugging aid: right-click the plugin UI and choose Inspect Element. Also restores the webview Reload menu. Leave off unless support asks for it."
          value={webInspector.enabled}
          onChange={handleWebInspectorChange}
        />
      )}

      {/* Version / update sit last so diagnostics stay above the footer. */}
      {(version || update) && (
        <div>
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
                marginBottom: version ? '16rem' : 0,
              }}
            >
              Update to v{update.version}
            </a>
          )}
          {version && (
            <p style={{ ...descriptionStyle, fontSize: '12rem', color: SUBTLE, margin: 0 }}>
              TONE3000 v{version}
            </p>
          )}
        </div>
      )}
    </>
  );

  return (
    <div
      className="hide-scrollbar"
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
          maxWidth: '480rem',
          margin: '0 auto',
          padding: '28rem 24rem 40rem',
          color: '#ffffff',
          boxSizing: 'border-box',
        }}
      >
        {header}
        {/* One tab (hosted) = no tab bar. */}
        {standalone && <TabBar active={tab} onChange={setTab} />}
        {tab === 'system' && standalone ? <SystemSettings device={device} /> : pluginTab}
      </div>
    </div>
  );
};

export default Settings;
