import { useCallback, useEffect, useState } from 'react';
import { useNativeFunction } from './useFunction';

/**
 * Drives a one-shot native "listening" measurement (auto balance, auto
 * offset): toggle() arms or cancels, and while armed we poll until the
 * native state machine leaves 'listening' (done, timeout or cancel).
 */
export function useAutoMeasure(startFn: string, cancelFn: string, pollFn: string) {
  const start = useNativeFunction<boolean>(startFn);
  const cancel = useNativeFunction<boolean>(cancelFn);
  const poll = useNativeFunction<{ state: string }>(pollFn);
  const [listening, setListening] = useState(false);

  useEffect(() => {
    if (!listening) return;
    const id = setInterval(async () => {
      const res = await poll();
      if (res && res.state !== 'listening') setListening(false);
    }, 200);
    return () => clearInterval(id);
  }, [listening, poll]);

  const toggle = useCallback(async () => {
    if (listening) {
      await cancel();
      setListening(false);
    } else {
      await start();
      setListening(true);
    }
  }, [listening, start, cancel]);

  return { listening, toggle };
}
