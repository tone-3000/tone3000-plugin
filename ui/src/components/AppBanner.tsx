import React, { useCallback, useState } from 'react';
import type { AudioDeviceState } from '../types/audioDevice';
import { AlertIcon, type AlertVariant } from './controls';
import { BORDER, MUTED } from './theme';

/**
 * Main-window banner: at most ONE banner, chosen by evaluating rules in
 * priority order against the native device-state snapshot. Standalone only:
 * every rule is device-domain, and nagging users about their DAW's setup
 * inside the plugin window would be hostile (the caller simply doesn't
 * evaluate without a state).
 *
 * The banner adds real window height (the editor grows via
 * setExtraContentHeight) so it never squishes the plugin UI; the caller owns
 * that coupling via BANNER_HEIGHT.
 *
 * "Ignore" hides a banner for 24h (persisted per machine in localStorage),
 * after which the rule may resurface. Errors and the auto-mute warning are not
 * ignorable: they describe a state where the plugin can't be heard at all.
 */

export const BANNER_HEIGHT = 44;

const DISMISSED_STORAGE_KEY = 't3k.dismissedBanners';
/** "Ignore" hides a banner for this long, then it may resurface. */
const IGNORE_DURATION_MS = 24 * 60 * 60 * 1000;

export type BannerAction = 'openSettings' | 'switchToAsio' | 'openMicSettings';

export interface AppBannerSpec {
  id: string;
  variant: AlertVariant;
  content: React.ReactNode;
  /** Primary action button. */
  action: { label: string; kind: BannerAction };
  dismissable: boolean;
}

interface BannerRule {
  id: string;
  variant: AlertVariant;
  dismissable: boolean;
  when: (state: AudioDeviceState) => boolean;
  content: (state: AudioDeviceState) => React.ReactNode;
  action: { label: string; kind: BannerAction };
}

const noActiveInput = (state: AudioDeviceState) =>
  !state.inputChannels.some((channel) => channel.active);

/** Priority-ordered rules; the first that fires (and isn't ignored) wins. */
const BANNER_RULES: BannerRule[] = [
  {
    // The OS is blocking mic access (macOS privacy). This gates ALL audio
    // input, built-in mic and interfaces alike, so the device opens but the
    // stream is silent and nothing else explains why. Highest priority: it's
    // the root cause behind an apparent no-input, and the only fix is the OS
    // privacy page + a relaunch, so it isn't ignorable.
    id: 'mic-denied',
    variant: 'error',
    dismissable: false,
    when: (state) => state.micPermission === 'denied',
    content: () => (
      <>
        <b>Microphone access is off.</b> TONE3000 can’t hear your instrument until you allow it in
        your privacy settings, then relaunch.
      </>
    ),
    action: { label: 'Allow Access', kind: 'openMicSettings' },
  },
  {
    // The plugin can't hear the instrument: no input selected, or the device
    // itself failed to open / was disconnected.
    id: 'no-input',
    variant: 'error',
    dismissable: false,
    when: (state) =>
      (state.inputDevice !== '' || state.outputDevice !== '') &&
      (!state.deviceOpen || state.inputDevice === '' || noActiveInput(state)),
    content: (state) => (
      <>
        <b>No audio input.</b>{' '}
        {!state.deviceOpen
          ? 'Your audio device was disconnected or couldn’t be opened.'
          : 'No input device is selected.'}{' '}
        The plugin can’t hear your instrument.
      </>
    ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
  {
    // Device is open but nothing is routed out, so the user won't hear the
    // plugin. Ranks just below no-input (which owns the disconnected case and
    // takes priority when input is also missing). In linked-I/O backends the
    // single device sets both names, so '' here only means "no device", which
    // no-input already covers.
    id: 'no-output',
    variant: 'error',
    dismissable: false,
    when: (state) => state.deviceOpen && state.outputDevice === '',
    content: () => (
      <>
        <b>No audio output.</b> No output device is selected, so you won’t hear the plugin.
      </>
    ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
  {
    // Muting feeds the amp sim silence, so the user hears nothing at all;
    // always surface it, not just on feedback risk (e.g. an interface where
    // you can't work out why there's no sound). Clears the instant Hear
    // Yourself goes back on, so it needn't be dismissable. Wording depends on
    // whether we muted for safety or the user did it themselves.
    id: 'input-muted',
    variant: 'warn',
    dismissable: false,
    when: (state) =>
      state.deviceOpen && !state.hearYourself && state.inputChannels.some((c) => c.active),
    content: (state) =>
      state.feedbackRisk ? (
        <>
          <b>Input muted to prevent feedback.</b> You’re using a microphone with speakers. Plug in
          headphones or an interface, then turn Hear Yourself on.
        </>
      ) : (
        <>
          <b>Input is muted.</b> Turn Hear Yourself on so the plugin can hear your instrument.
        </>
      ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
  {
    // The user is monitoring (Hear Yourself on) with a mic-into-speakers setup,
    // so nothing is muted but a squeal is one gain bump away. Dismissable:
    // some rooms are fine, and it's the user's call once warned.
    id: 'feedback-risk',
    variant: 'warn',
    dismissable: true,
    when: (state) => state.feedbackRisk && state.hearYourself,
    content: () => (
      <>
        <b>Feedback risk.</b> Your mic can hear your speakers. Use headphones, or turn Hear Yourself
        off.
      </>
    ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
  {
    // Requires ASIO to actually report devices, not just that the type exists.
    id: 'asio-nudge',
    variant: 'info',
    dismissable: true,
    when: (state) => state.asioAvailable && state.currentType !== 'ASIO',
    content: () => (
      <>
        <b>Lower latency available.</b> Your interface has an ASIO driver, which is faster than
        Windows Audio.
      </>
    ),
    action: { label: 'Switch to ASIO', kind: 'switchToAsio' },
  },
  {
    // Skipped when no ≤512 option exists or the driver owns the buffer;
    // never nag about something the user can't fix here. Evaluates readback.
    id: 'buffer-latency',
    variant: 'info',
    dismissable: true,
    when: (state) =>
      state.bufferSize > 512 &&
      state.bufferSizes.length > 1 &&
      state.bufferSizes.some((size) => size <= 512),
    content: (state) => (
      <>
        <b>Noticeable delay?</b> Your buffer is {state.bufferSize} samples (
        {((state.bufferSize * 1000) / (state.sampleRate || 48000)).toFixed(1)} ms). Lowering it to
        256 or less will feel more responsive.
      </>
    ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
  {
    // Off 48 kHz the chain resamples, which costs a little CPU. Purely
    // informational, ignorable, and lowest priority.
    id: 'rate-not-48k',
    variant: 'info',
    dismissable: true,
    when: (state) =>
      state.deviceOpen && state.sampleRate > 0 && Math.round(state.sampleRate) !== 48000,
    content: (state) => (
      <>
        <b>Running at {(state.sampleRate / 1000).toFixed(state.sampleRate % 1000 === 0 ? 0 : 1)} kHz.</b>{' '}
        TONE3000 runs lightest at 48 kHz. Any rate works fine, this one just uses a bit more CPU.
      </>
    ),
    action: { label: 'Open Settings', kind: 'openSettings' },
  },
];

/**
 * Rules keyed by id, so the System Settings form can re-render the same
 * warning/error copy inline next to the relevant control (banners only live on
 * the main screen). The form supplies its own placement gating and drops the
 * action/ignore buttons; it reuses only `variant` + `content` for one source
 * of truth on wording.
 */
export const bannerRuleById: Record<string, BannerRule> = Object.fromEntries(
  BANNER_RULES.map((rule) => [rule.id, rule])
);

/** id → epoch-ms when the ignore expires (banner may resurface after). */
type DismissedMap = Record<string, number>;

const readDismissed = (): DismissedMap => {
  try {
    const parsed = JSON.parse(localStorage.getItem(DISMISSED_STORAGE_KEY) ?? '{}');
    return parsed && typeof parsed === 'object' ? (parsed as DismissedMap) : {};
  } catch {
    return {};
  }
};

/** Evaluate the rules; returns the active banner (or null) and a dismisser. */
export function useAppBanner(state: AudioDeviceState | null): {
  banner: AppBannerSpec | null;
  dismiss: (id: string) => void;
} {
  const [dismissed, setDismissed] = useState<DismissedMap>(readDismissed);

  // Ignoring a banner hides it for 24h; after that the rule may fire again.
  const dismiss = useCallback((id: string) => {
    setDismissed((previous) => {
      const next = { ...previous, [id]: Date.now() + IGNORE_DURATION_MS };
      try {
        localStorage.setItem(DISMISSED_STORAGE_KEY, JSON.stringify(next));
      } catch {
        // Persistence is a nicety; the in-memory dismissal still applies.
      }
      return next;
    });
  }, []);

  if (!state) return { banner: null, dismiss };

  const now = Date.now();
  const isIgnored = (id: string) => (dismissed[id] ?? 0) > now;
  const rule = BANNER_RULES.find(
    (candidate) => candidate.when(state) && !(candidate.dismissable && isIgnored(candidate.id))
  );
  return {
    banner: rule
      ? {
          id: rule.id,
          variant: rule.variant,
          content: rule.content(state),
          action: rule.action,
          dismissable: rule.dismissable,
        }
      : null,
    dismiss,
  };
}

/** Primary = outlined pill; ignore = borderless muted text button. */
const actionButtonStyle = (secondary: boolean): React.CSSProperties => ({
  background: 'none',
  border: secondary ? 'none' : '1px solid #ffffff',
  color: secondary ? MUTED : '#ffffff',
  borderRadius: '7px',
  fontSize: '11.5px',
  fontWeight: 600,
  padding: secondary ? '4px 4px' : '4px 11px',
  cursor: 'pointer',
  whiteSpace: 'nowrap',
  flexShrink: 0,
});

/** The fixed-height banner bar above the plugin header. */
export const AppBanner: React.FC<{
  banner: AppBannerSpec;
  onAction: (kind: BannerAction) => void;
  onDismiss: (id: string) => void;
}> = ({ banner, onAction, onDismiss }) => (
  <div
    role="alert"
    style={{
      height: `${BANNER_HEIGHT}px`,
      boxSizing: 'border-box',
      display: 'flex',
      alignItems: 'center',
      gap: '10px',
      padding: '0 24px',
      borderBottom: BORDER,
      backgroundColor: '#000000',
      color: '#ffffff',
      fontSize: '12.5px',
      lineHeight: 1.4,
      flexShrink: 0,
    }}
  >
    <AlertIcon variant={banner.variant} />
    <span
      style={{
        flex: 1,
        minWidth: 0,
        fontWeight: 400,
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
      }}
    >
      {banner.content}
    </span>
    <button onClick={() => onAction(banner.action.kind)} style={actionButtonStyle(false)}>
      {banner.action.label}
    </button>
    {banner.dismissable && (
      <button onClick={() => onDismiss(banner.id)} style={actionButtonStyle(true)}>
        Ignore
      </button>
    )}
  </div>
);
