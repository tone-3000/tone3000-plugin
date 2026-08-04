import { useCallback } from 'react';
import { useAudioBackend } from './useAudioBackend';

/**
 * Binds a native (C++) function by name as a stable async callback.
 * Errors resolve to null so callers on timers never throw per tick.
 */
export function useNativeFunction<T = unknown>(
  name: string
): (...args: unknown[]) => Promise<T | null> {
  const backend = useAudioBackend();
  return useCallback(
    async (...args: unknown[]): Promise<T | null> => {
      try {
        return (await backend.getPluginFunction(name)(...args)) as T;
      } catch (err) {
        console.error(`Error invoking native function "${name}"`, err);
        return null;
      }
    },
    [backend, name]
  );
}
