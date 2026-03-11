import { useState, useCallback } from 'react';
import { useAudioBackend } from './useAudioBackend';

export function useFunction<T = any>(name: string) {
  const backend = useAudioBackend();
  const [result, setResult] = useState<T | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<unknown>(null);

  const invoke = useCallback(
    async (...args: any[]): Promise<T | null> => {
      setLoading(true);
      setError(null);

      try {
        const fn = backend.getPluginFunction(name);

        // Timeout after 2 seconds
        const timeoutPromise = new Promise<null>((_, reject) =>
          setTimeout(() => reject(new Error(`No response from ${name}`)), 2000)
        );

        const resultPromise = fn(...args);
        const res = (await Promise.race([resultPromise, timeoutPromise])) as T;

        setResult(res);
        return res;
      } catch (err) {
        console.error(`Error invoking native function "${name}"`, err);
        setError(err);
        return null;
      } finally {
        setLoading(false);
      }
    },
    [backend, name]
  );

  return {
    result,
    loading,
    error,
    invoke,
  };
}
