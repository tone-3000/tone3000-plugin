import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Volume2 } from 'lucide-react';
import type { AudioDevice } from '../hooks/useAudioDevice';
import { useAudioInputLevels } from '../hooks/useAudioDevice';
import type { AudioDeviceState, AudioInputChannel } from '../types/audioDevice';
import {
  AlertCard,
  ChoiceIndicator,
  FieldRow,
  SegmentedControl,
  SelectField,
  ToggleRow,
  captionStyle,
  outlinedFieldStyle,
} from './controls';
import { MidiInputsSection } from './MidiInputsSection';
import { bannerRuleById } from './AppBanner';
import { getGradientColor } from './meterColor';
import { MUTED, SUBTLE } from './theme';

/**
 * Re-renders a main-window banner's copy inline, next to the control that
 * fixes it — same words as the banner, but no action/ignore buttons (you're
 * already in the form). Placement gating is the caller's; this just draws.
 */
const InlineBannerAlert: React.FC<{ id: string; state: AudioDeviceState; show?: boolean }> = ({
  id,
  state,
  show,
}) => {
  const rule = bannerRuleById[id];
  if (!rule) return null;
  // Default to the banner's own trigger so the two never drift; callers pass an
  // explicit `show` only where the inline placement wants different gating than
  // the main-window banner (e.g. no-input lives in the channel picker).
  if (!(show ?? rule.when(state))) return null;
  return (
    <AlertCard variant={rule.variant} style={{ marginTop: '12px' }}>
      {rule.content(state)}
    </AlertCard>
  );
};

/**
 * System Settings tab: the bespoke replacement for JUCE's audio settings
 * dialog, driven entirely by the native device-state snapshot. Every branch
 * the stock AudioDeviceSelectorComponent handles is covered:
 *
 * - Driver picker only when the platform registers multiple device types.
 * - One device picker for linked-I/O backends (ASIO), separate input/output
 *   pickers everywhere else — with "No device" as a legitimate option.
 * - Input channel picker with live meters: radio behavior in mono, pick-any-
 *   two (oldest swapped out) in stereo — JUCE's flipBit window as an explicit
 *   Mono/Stereo control.
 * - Output stereo-pair picker for multi-out interfaces.
 * - Buffer/rate lists come from the device; the shown values are readback.
 *   Single-entry lists render locked with a caption instead of a fake picker.
 * - ASIO control panel + reset device, and inline (never modal) errors.
 */

interface SystemSettingsProps {
  device: AudioDevice;
}

/** Sentinel for the "no device" dropdown entry (JUCE's empty device name). */
const NO_DEVICE = '';

/** Device dropdown options: real devices, a "no device" entry, and — when a
    selected device vanished mid-session — its stale name so the picker shows
    what broke instead of going blank. */
const deviceOptions = (devices: string[], current: string, noneLabel: string) => {
  const options = devices.map((name) => ({ value: name, label: name }));
  if (current !== NO_DEVICE && !devices.includes(current))
    options.unshift({ value: current, label: `${current} (disconnected)` });
  options.push({ value: NO_DEVICE, label: noneLabel });
  return options;
};

//==============================================================================
// Input channel picker

const METER_LEDS = 18;
const METER_MIN_DB = -60;

/** Horizontal LED strip in the app's meter language (full ramp always
    visible dimmed, lit to the level). */
const ChannelMeter: React.FC<{ db: number }> = ({ db }) => (
  <span style={{ display: 'flex', gap: '3px', marginLeft: 'auto', flexShrink: 0 }}>
    {Array.from({ length: METER_LEDS }, (_, i) => {
      const position = i / (METER_LEDS - 1);
      const ledDb = METER_MIN_DB + position * -METER_MIN_DB;
      return (
        <span
          key={i}
          style={{
            width: '5px',
            height: '5px',
            borderRadius: '1.5px',
            backgroundColor: getGradientColor(position),
            opacity: db >= ledDb ? 1 : 0.18,
          }}
        />
      );
    })}
  </span>
);

const ChannelRow: React.FC<{
  channel: AudioInputChannel;
  selected: boolean;
  square: boolean;
  db: number;
  onClick: () => void;
}> = ({ channel, selected, square, db, onClick }) => (
  <button
    role={square ? 'checkbox' : 'radio'}
    aria-checked={selected}
    onClick={onClick}
    style={{
      display: 'flex',
      alignItems: 'center',
      gap: '12px',
      width: '100%',
      padding: '8px 4px',
      border: 'none',
      borderRadius: '8px',
      background: 'transparent',
      cursor: 'pointer',
      textAlign: 'left',
      color: 'inherit',
    }}
    onMouseEnter={(e) => (e.currentTarget.style.background = 'rgba(255, 255, 255, 0.06)')}
    onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
  >
    <ChoiceIndicator selected={selected} square={square} />
    <span
      style={{
        fontSize: '14px',
        fontWeight: 400,
        color: selected ? '#ffffff' : MUTED,
        minWidth: '16px',
      }}
    >
      {channel.index + 1}
    </span>
    <span
      style={{
        fontSize: '11px',
        color: SUBTLE,
        fontFamily: 'monospace',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
      }}
    >
      {channel.name}
    </span>
    <ChannelMeter db={db} />
  </button>
);

type InputMode = 'mono' | 'stereo';

/**
 * Input channel selection with JUCE's min-1/max-2 window surfaced as an
 * explicit Mono/Stereo control. Selection *order* lives here (native only
 * knows the mask) so stereo's third pick can swap out the oldest choice.
 */
const InputChannelPicker: React.FC<{
  state: AudioDeviceState;
  onApply: (indices: number[]) => void;
}> = ({ state, onApply }) => {
  const channels = state.inputChannels;
  const activeIndices = useMemo(
    () => channels.filter((c) => c.active).map((c) => c.index),
    [channels]
  );
  const mode: InputMode = activeIndices.length >= 2 ? 'stereo' : 'mono';

  // Oldest→newest selection order, reconciled against native truth: drop
  // entries that went inactive, append newly-active ones.
  const orderRef = useRef<number[]>([]);
  useEffect(() => {
    const kept = orderRef.current.filter((i) => activeIndices.includes(i));
    const added = activeIndices.filter((i) => !kept.includes(i));
    orderRef.current = [...kept, ...added];
  }, [activeIndices]);

  // Meters run only while this picker is on screen (native registers a raw
  // device tap, so signal shows even while Hear Yourself is muted).
  const levels = useAudioInputLevels(channels.length > 0);

  // The no-input banner, mirrored inline here. When it shows it replaces the
  // neutral "no channels" caption so the two never stack (they say the same
  // thing). Gated to a running device; a dead device is the top-of-form error.
  const showNoInput =
    state.deviceOpen && (state.inputDevice === NO_DEVICE || channels.every((c) => !c.active));

  const selectChannel = (index: number) => {
    if (activeIndices.includes(index)) return; // min 1: can't deselect the last pick
    const order = orderRef.current;
    const next =
      mode === 'stereo'
        ? [...order, index].slice(-2) // max 2: swap out the oldest pick
        : [index];
    onApply(next);
  };

  const switchMode = (next: InputMode) => {
    if (next === mode) return;
    const order = orderRef.current;
    if (next === 'mono') {
      onApply([order[order.length - 1] ?? channels[0]?.index ?? 0]);
    } else {
      const current = order[order.length - 1] ?? channels[0]?.index ?? 0;
      const partner = channels.map((c) => c.index).find((i) => i !== current);
      if (partner !== undefined) onApply([current, partner]);
    }
  };

  return (
    <FieldRow
      label={mode === 'stereo' ? 'Input Channels' : 'Input Channel'}
      help={
        channels.length === 0
          ? undefined
          : mode === 'stereo'
            ? 'Play your instrument and pick the two channels with signal.'
            : 'Play your instrument and select the channel with signal.'
      }
      labelExtra={
        channels.length > 1 ? (
          <SegmentedControl<InputMode>
            value={mode}
            options={[
              { value: 'mono', label: 'Mono' },
              { value: 'stereo', label: 'Stereo' },
            ]}
            onChange={switchMode}
            ariaLabel="Input mode"
          />
        ) : undefined
      }
    >
      {channels.length > 0 && (
        <>
          <div style={{ maxHeight: '296px', overflowY: 'auto', overscrollBehavior: 'contain' }}>
            {channels.map((channel) => (
              <ChannelRow
                key={channel.index}
                channel={channel}
                selected={channel.active}
                square={mode === 'stereo'}
                db={levels[channel.index] ?? -Infinity}
                onClick={() => selectChannel(channel.index)}
              />
            ))}
          </div>
          {mode === 'stereo' && (
            <p style={{ ...captionStyle, marginTop: '8px' }}>
              Pick any two. Selecting a third swaps out your oldest pick.
            </p>
          )}
        </>
      )}
      {channels.length === 0 && !showNoInput && (
        <p style={{ ...captionStyle, fontStyle: 'italic', margin: 0 }}>
          No input channels. Select an interface above.
        </p>
      )}
      <InlineBannerAlert id="no-input" show={showNoInput} state={state} />
    </FieldRow>
  );
};

//==============================================================================
// Copy helpers — captions vary by backend, per the UX spec.

const bufferCaption = (state: AudioDeviceState): string | null => {
  if (state.bufferSizes.length <= 1)
    return state.currentType === 'JACK'
      ? 'Set by the JACK server. Change it in qjackctl or PipeWire.'
      : 'Set by the audio driver.';
  if (state.currentType === 'ASIO')
    return "Some ASIO drivers override this. If it snaps back, set it in the driver's Control Panel below.";
  return null;
};

const rateCaption = (state: AudioDeviceState): string | null => {
  if (state.sampleRates.length > 1) return null;
  if (state.currentType === 'JACK') return 'Set by the JACK server.';
  if (state.currentType === 'Windows Audio')
    return 'Fixed by Windows in shared mode. Switch the Audio Driver to Exclusive Mode for other rates.';
  return 'Fixed by the current audio driver.';
};

const formatBufferOption = (samples: number, rate: number, recommendable: boolean): string => {
  const ms = rate > 0 ? ` (${((samples * 1000) / rate).toFixed(1)} ms)` : '';
  const tag = recommendable && samples === 128 ? ' (recommended)' : '';
  return `${samples} samples${ms}${tag}`;
};

//==============================================================================

export const SystemSettings: React.FC<SystemSettingsProps> = ({ device }) => {
  const { state, actions } = device;

  // Errors from mutations render inline (never a modal); Retry reopens the
  // device — the standard recovery for "device in use" and panel weirdness.
  const [error, setError] = useState<string>('');
  const apply = async (action: () => Promise<string>) => setError(await action());

  // Test tone feedback: brief "Playing…" state on the button.
  const [testPlaying, setTestPlaying] = useState(false);
  const playTest = async () => {
    setTestPlaying(true);
    await apply(actions.playTestTone);
    setTimeout(() => setTestPlaying(false), 1200);
  };

  if (!state) return null;

  // A configured-but-closed device (unplugged interface, exclusively-held
  // ALSA card) is the top-priority inline error; explicit "no device" is not
  // an error here (the main-window banner covers the no-input consequence).
  const hasSelection = state.inputDevice !== NO_DEVICE || state.outputDevice !== NO_DEVICE;
  const deviceFailed = hasSelection && !state.deviceOpen;
  const inlineError = error || (deviceFailed ? 'The audio device couldn’t be opened.' : '');

  const testDisabled = !state.deviceOpen || state.outputDevice === NO_DEVICE;

  // Inline mirrors of the main-window banners, gated to a running device (a
  // dead device is already covered by the top-of-form error above). Placed
  // under the control that resolves each one. (no-input lives in the channel
  // picker, next to its control.)
  const showNoOutput = state.separateIO && state.deviceOpen && state.outputDevice === NO_DEVICE;
  const showMuted =
    state.deviceOpen && !state.hearYourself && state.inputChannels.some((c) => c.active);

  return (
    <>
      <style>
        {`@keyframes t3kTestPulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }`}
      </style>
      {/* OS mic gate: the one form error whose fix lives outside the app, so
          (unlike the other inline mirrors) it keeps its action button. */}
      {state.micPermission === 'denied' && (
        <AlertCard
          variant="error"
          style={{ marginBottom: '32px' }}
          actions={[{ label: 'Allow Access', onClick: () => apply(actions.openMicSettings) }]}
        >
          <b>Microphone access is off.</b> TONE3000 can’t hear your instrument until you allow it in
          your privacy settings, then relaunch.
        </AlertCard>
      )}
      {inlineError && (
        <AlertCard
          variant="error"
          style={{ marginBottom: '32px' }}
          actions={[
            {
              label: 'Retry',
              onClick: () => apply(actions.restartDevice),
            },
          ]}
        >
          <b>{inlineError}</b> Plug the interface back in, hit Retry, or choose another device.
        </AlertCard>
      )}

      {/* Output monitoring — top of the tab; this is how sound starts. */}
      <ToggleRow
        label="Hear Yourself"
        description="Hear your instrument while playing."
        value={state.hearYourself}
        onChange={(hear) => apply(() => actions.setHearYourself(hear))}
      >
        <InlineBannerAlert
          id="feedback-risk"
          show={state.hearYourself && state.feedbackRisk}
          state={state}
        />
        <InlineBannerAlert id="input-muted" show={showMuted} state={state} />
      </ToggleRow>

      {/* Driver picker — only when the platform has more than one backend. */}
      {state.deviceTypes.length > 1 && (
        <FieldRow
          label="Audio Driver"
          help={
            state.deviceTypes.includes('ASIO')
              ? 'ASIO gives the lowest latency with a dedicated interface.'
              : 'Choose which audio system drives your devices.'
          }
        >
          <SelectField
            value={state.currentType}
            options={state.deviceTypes.map((t) => ({ value: t, label: t }))}
            onChange={(t) => apply(() => actions.setDeviceType(t))}
            ariaLabel="Audio driver"
          />
          <InlineBannerAlert id="asio-nudge" state={state} />
        </FieldRow>
      )}

      {/* Device picker(s): one for linked-I/O drivers, separate otherwise. */}
      {state.separateIO ? (
        <FieldRow
          label="Input Device"
          help="The interface or microphone your instrument is plugged into."
        >
          <SelectField
            value={state.inputDevice}
            options={deviceOptions(state.inputDevices, state.inputDevice, 'No input device')}
            onChange={(name) => apply(() => actions.setInputDevice(name))}
            ariaLabel="Input device"
          />
        </FieldRow>
      ) : (
        <FieldRow label="Device" help="This driver handles input and output together.">
          <SelectField
            value={state.outputDevice}
            options={deviceOptions(state.outputDevices, state.outputDevice, 'No device')}
            onChange={(name) => apply(() => actions.setLinkedDevice(name))}
            ariaLabel="Audio device"
          />
        </FieldRow>
      )}

      <InputChannelPicker
        state={state}
        onApply={(indices) => apply(() => actions.setInputChannels(indices))}
      />

      {/* Output device + test tone. */}
      <FieldRow label="Output Device" help="Select where you want to hear the sound.">
        <div style={{ display: 'flex', gap: '8px' }}>
          {state.separateIO && (
            <div style={{ flex: 1, minWidth: 0 }}>
              <SelectField
                value={state.outputDevice}
                options={deviceOptions(state.outputDevices, state.outputDevice, 'No output device')}
                onChange={(name) => apply(() => actions.setOutputDevice(name))}
                ariaLabel="Output device"
              />
            </div>
          )}
          {/* Fixed width + constant label so the active state never shifts
              layout; the icon turns green (and pulses) while the tone plays. */}
          <button
            onClick={playTest}
            disabled={testDisabled || testPlaying}
            style={{
              ...outlinedFieldStyle,
              width: '104px',
              flexShrink: 0,
              padding: '12px 0',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              gap: '7px',
              cursor: testDisabled || testPlaying ? 'default' : 'pointer',
              color: testPlaying ? '#00D13B' : testDisabled ? SUBTLE : '#ffffff',
            }}
          >
            <Volume2
              size={15}
              style={{ animation: testPlaying ? 't3kTestPulse 0.9s ease-in-out infinite' : 'none' }}
            />
            Test
          </button>
        </div>
        <InlineBannerAlert id="no-output" show={showNoOutput} state={state} />
      </FieldRow>

      {/* Stereo output pair — only for multi-out interfaces. */}
      {state.outputPairs.length > 1 && (
        <FieldRow label="Output Channels" help="This interface has more than one output pair.">
          <SelectField
            value={String(Math.max(state.activeOutputPair, 0))}
            options={state.outputPairs.map((label, i) => ({ value: String(i), label }))}
            onChange={(i) => apply(() => actions.setOutputPair(Number(i)))}
            ariaLabel="Output channels"
          />
        </FieldRow>
      )}

      {/* Buffer size — list and current value are device readback. */}
      {state.bufferSizes.length > 0 && (
        <FieldRow
          label="Buffer Size"
          help="Hearing a delay between picking and hearing the note? Choose a smaller size. Hearing crackles or pops? Choose a bigger one."
        >
          <SelectField
            value={String(state.bufferSize)}
            options={state.bufferSizes.map((samples) => ({
              value: String(samples),
              label: formatBufferOption(samples, state.sampleRate, state.bufferSizes.length > 1),
            }))}
            onChange={(samples) => apply(() => actions.setBufferSize(Number(samples)))}
            disabled={state.bufferSizes.length <= 1}
            ariaLabel="Buffer size"
          />
          {bufferCaption(state) && <p style={captionStyle}>{bufferCaption(state)}</p>}
          <InlineBannerAlert id="buffer-latency" state={state} />
        </FieldRow>
      )}

      {/* Sample rate. */}
      {state.sampleRates.length > 0 && (
        <FieldRow
          label="Sample Rate"
          help="48000 Hz is the sweet spot and uses the least CPU. Other rates work fine, pick one if your device needs it or you prefer it."
        >
          <SelectField
            value={String(state.sampleRate)}
            options={state.sampleRates.map((rate) => ({
              value: String(rate),
              label: `${rate} Hz${rate === 48000 && state.sampleRates.length > 1 ? ' (recommended)' : ''}`,
            }))}
            onChange={(rate) => apply(() => actions.setSampleRate(Number(rate)))}
            disabled={state.sampleRates.length <= 1}
            ariaLabel="Sample rate"
          />
          {rateCaption(state) && <p style={captionStyle}>{rateCaption(state)}</p>}
          <InlineBannerAlert id="rate-not-48k" state={state} />
        </FieldRow>
      )}

      {/* Vendor control panel (ASIO) — buffer/clock often live there. */}
      {state.hasControlPanel && (
        <FieldRow
          label="Driver Settings"
          help="Buffer and clock options live in the manufacturer's app for this driver."
        >
          <div style={{ display: 'flex', gap: '8px' }}>
            <button
              onClick={() => apply(actions.openControlPanel)}
              style={{ ...outlinedFieldStyle, padding: '12px 16px', cursor: 'pointer', flex: 1 }}
            >
              Open Control Panel
            </button>
            <button
              onClick={() => apply(actions.restartDevice)}
              style={{ ...outlinedFieldStyle, padding: '12px 16px', cursor: 'pointer', flex: 1 }}
            >
              Reset Device
            </button>
          </div>
        </FieldRow>
      )}

      {/* MIDI hardware — which devices feed the plugin. What each control
          does is mapped in Plugin Settings (MIDI Mapping). */}
      <MidiInputsSection device={device} />
    </>
  );
};

export default SystemSettings;
