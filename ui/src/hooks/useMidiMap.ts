import { useCallback, useEffect, useMemo, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { MidiMapState } from '../types/midiMap';

/**
 * Single owner of the MIDI mapping state on the JS side, mirroring
 * useAudioDevice's sync model: native is the source of truth, the UI holds a
 * snapshot. Native pushes a `midiMapChanged` event on every map change (learn
 * commits, which happen when the user moves a hardware control and not from
 * any UI action, plus removals and state restores) and the hook re-pulls.
 * Mutations also re-pull immediately for snappy readback.
 *
 * Unlike the device state this works in hosted builds too: mapping lives in
 * the processor and reads the host-delivered MIDI buffer.
 */

export interface MidiMapActions {
  setChannel: (channel: number) => Promise<void>;
  startLearn: (targetId: string) => Promise<void>;
  cancelLearn: () => Promise<void>;
  removeMapping: (targetId: string) => Promise<void>;
}

export interface MidiMap {
  /** null while loading and in dev without the bridge. */
  state: MidiMapState | null;
  actions: MidiMapActions;
}

export function useMidiMap(enabled: boolean): MidiMap {
  const backend = useAudioBackend();

  const native = useMemo(
    () => ({
      getMidiMapState: backend.getPluginFunction('getMidiMapState'),
      setMidiChannelFilter: backend.getPluginFunction('setMidiChannelFilter'),
      startMidiLearn: backend.getPluginFunction('startMidiLearn'),
      cancelMidiLearn: backend.getPluginFunction('cancelMidiLearn'),
      removeMidiMapping: backend.getPluginFunction('removeMidiMapping'),
    }),
    [backend]
  );

  const [state, setState] = useState<MidiMapState | null>(null);

  const refresh = useCallback(async () => {
    try {
      const res = (await native.getMidiMapState()) as MidiMapState | null;
      // The mock backend returns a string; shape-check before trusting it.
      setState(res && Array.isArray(res.mappings) ? res : null);
    } catch (error) {
      console.error('Error loading MIDI map state:', error);
    }
  }, [native]);

  useEffect(() => {
    if (!enabled) return;
    refresh();
    return backend.addEventListener('midiMapChanged', () => refresh());
  }, [backend, enabled, refresh]);

  const actions = useMemo<MidiMapActions>(() => {
    const run = async (fn: () => Promise<unknown>): Promise<void> => {
      try {
        await fn();
      } catch (error) {
        console.error('MIDI map mutation failed:', error);
      }
      await refresh();
    };

    return {
      setChannel: (channel) => run(() => native.setMidiChannelFilter(channel)),
      startLearn: (targetId) => run(() => native.startMidiLearn(targetId)),
      cancelLearn: () => run(() => native.cancelMidiLearn()),
      removeMapping: (targetId) => run(() => native.removeMidiMapping(targetId)),
    };
  }, [native, refresh]);

  return { state, actions };
}
