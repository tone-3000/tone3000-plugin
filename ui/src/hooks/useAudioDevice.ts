import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { AudioDeviceState, AudioDeviceResult } from '../types/audioDevice';

/**
 * Single owner of the standalone audio device state on the JS side.
 *
 * Sync model mirrors useChainState: native is the source of truth, the UI
 * holds a snapshot. Native pushes an `audioDeviceChanged` event on every
 * device-manager change (our own setters, hot-plugs, disconnects, vendor
 * control panel edits) and the hook re-pulls. Mutations also re-pull
 * immediately so the settings panel reflects readback without waiting for
 * the event round trip.
 *
 * Every action resolves to an error string ('' on success) — errors are the
 * human-readable strings JUCE's setAudioDeviceSetup reports ("device in
 * use", locked ALSA device…), surfaced inline by the caller.
 */

const noError = '';

export interface AudioDeviceActions {
  setDeviceType: (typeName: string) => Promise<string>;
  setInputDevice: (name: string) => Promise<string>;
  setOutputDevice: (name: string) => Promise<string>;
  setLinkedDevice: (name: string) => Promise<string>;
  setInputChannels: (indices: number[]) => Promise<string>;
  setOutputPair: (pairIndex: number) => Promise<string>;
  setSampleRate: (rate: number) => Promise<string>;
  setBufferSize: (samples: number) => Promise<string>;
  setHearYourself: (hear: boolean) => Promise<string>;
  playTestTone: () => Promise<string>;
  openControlPanel: () => Promise<string>;
  restartDevice: () => Promise<string>;
  openMicSettings: () => Promise<string>;
  setMidiInputEnabled: (id: string, enabled: boolean) => Promise<string>;
  openBluetoothMidiPairing: () => Promise<string>;
}

export interface AudioDevice {
  /** null while loading, in hosted builds, and in dev without the bridge. */
  state: AudioDeviceState | null;
  refresh: () => Promise<void>;
  actions: AudioDeviceActions;
}

export function useAudioDevice(enabled: boolean): AudioDevice {
  const backend = useAudioBackend();

  const native = useMemo(
    () => ({
      getAudioDeviceState: backend.getPluginFunction('getAudioDeviceState'),
      setAudioDeviceType: backend.getPluginFunction('setAudioDeviceType'),
      setAudioDevice: backend.getPluginFunction('setAudioDevice'),
      setAudioInputChannels: backend.getPluginFunction('setAudioInputChannels'),
      setAudioOutputPair: backend.getPluginFunction('setAudioOutputPair'),
      setAudioSampleRate: backend.getPluginFunction('setAudioSampleRate'),
      setAudioBufferSize: backend.getPluginFunction('setAudioBufferSize'),
      setHearYourself: backend.getPluginFunction('setHearYourself'),
      playTestTone: backend.getPluginFunction('playTestTone'),
      openAudioControlPanel: backend.getPluginFunction('openAudioControlPanel'),
      restartAudioDevice: backend.getPluginFunction('restartAudioDevice'),
      openMicSettings: backend.getPluginFunction('openMicSettings'),
      setMidiInputEnabled: backend.getPluginFunction('setMidiInputEnabled'),
      openBluetoothMidiPairing: backend.getPluginFunction('openBluetoothMidiPairing'),
    }),
    [backend]
  );

  const [state, setState] = useState<AudioDeviceState | null>(null);

  const refresh = useCallback(async () => {
    try {
      const res = (await native.getAudioDeviceState()) as AudioDeviceState | null;
      // Hosted builds (and the mock backend) return nothing useful.
      setState(res && Array.isArray(res.deviceTypes) ? res : null);
    } catch (error) {
      console.error('Error loading audio device state:', error);
    }
  }, [native]);

  useEffect(() => {
    if (!enabled) return;
    refresh();
    return backend.addEventListener('audioDeviceChanged', () => refresh());
  }, [backend, enabled, refresh]);

  const actions = useMemo<AudioDeviceActions>(() => {
    /** Run a mutation, re-pull state, and normalize to an error string. */
    const run = async (fn: () => Promise<unknown>): Promise<string> => {
      let error = noError;
      try {
        const res = (await fn()) as AudioDeviceResult | null;
        if (!res || typeof res.ok !== 'boolean') error = 'Audio settings are unavailable.';
        else if (!res.ok) error = res.error || "Couldn't apply the audio settings.";
      } catch (err) {
        console.error('Audio device mutation failed:', err);
        error = "Couldn't apply the audio settings.";
      }
      await refresh();
      return error;
    };

    return {
      setDeviceType: (typeName) => run(() => native.setAudioDeviceType(typeName)),
      setInputDevice: (name) => run(() => native.setAudioDevice('input', name)),
      setOutputDevice: (name) => run(() => native.setAudioDevice('output', name)),
      setLinkedDevice: (name) => run(() => native.setAudioDevice('linked', name)),
      setInputChannels: (indices) => run(() => native.setAudioInputChannels(indices)),
      setOutputPair: (pairIndex) => run(() => native.setAudioOutputPair(pairIndex)),
      setSampleRate: (rate) => run(() => native.setAudioSampleRate(rate)),
      setBufferSize: (samples) => run(() => native.setAudioBufferSize(samples)),
      setHearYourself: (hear) => run(() => native.setHearYourself(hear)),
      playTestTone: () => run(() => native.playTestTone()),
      openControlPanel: () => run(() => native.openAudioControlPanel()),
      restartDevice: () => run(() => native.restartAudioDevice()),
      openMicSettings: () => run(() => native.openMicSettings()),
      setMidiInputEnabled: (id, enabled) => run(() => native.setMidiInputEnabled(id, enabled)),
      openBluetoothMidiPairing: () => run(() => native.openBluetoothMidiPairing()),
    };
  }, [native, refresh]);

  return { state, refresh, actions };
}

/**
 * Per-channel input peak levels (dB by device channel index) for the channel
 * picker's meters. Enables native metering (a raw device tap that sees input
 * even while Hear Yourself mutes the plugin's feed) while mounted, polls at
 * ~30 Hz, and applies a simple falloff so short peaks stay readable.
 */
const METER_POLL_MS = 33;
const METER_FLOOR_DB = -120;
/** dB the displayed level falls per poll when the signal drops. */
const METER_FALLOFF_DB = 4;

export function useAudioInputLevels(enabled: boolean): number[] {
  const backend = useAudioBackend();
  const [levels, setLevels] = useState<number[]>([]);
  const displayed = useRef<number[]>([]);

  useEffect(() => {
    if (!enabled) {
      displayed.current = [];
      setLevels([]);
      return;
    }

    const setMetering = backend.getPluginFunction('setAudioInputMetering');
    const getLevels = backend.getPluginFunction('getAudioInputLevels');
    let cancelled = false;

    setMetering(true).catch(() => {});
    const interval = setInterval(async () => {
      try {
        const raw = (await getLevels()) as number[] | null;
        if (cancelled || !Array.isArray(raw)) return;
        displayed.current = raw.map((db, i) => {
          const previous = displayed.current[i] ?? METER_FLOOR_DB;
          return Math.max(db, previous - METER_FALLOFF_DB, METER_FLOOR_DB);
        });
        setLevels(displayed.current);
      } catch {
        // Backend not ready; keep polling.
      }
    }, METER_POLL_MS);

    return () => {
      cancelled = true;
      clearInterval(interval);
      setMetering(false).catch(() => {});
    };
  }, [backend, enabled]);

  return levels;
}
