import { useCallback, useRef, useState } from 'react';

/**
 * Instant connectivity check via `navigator.onLine`. `false` means the OS has
 * no network interface up (the "internet not set up" case we're guarding
 * against); `true` doesn't guarantee the wider internet is reachable, which
 * is what the recovery paths are for (failed-navigation recovery, block retry).
 * We deliberately skip any network probe to keep the + button instant.
 */
export function checkInternet(): boolean {
  return typeof navigator === 'undefined' || navigator.onLine !== false;
}

/**
 * First line of defence for internet-dependent actions (add / swap / login):
 *
 *   const gate = useInternetGate();
 *   const onAdd = () => gate.requireInternet(() => startSelectFlow());
 *   ...
 *   <OfflineModal open={gate.offlineModalOpen}
 *                 onRetry={gate.retry} onDismiss={gate.dismiss} />
 *
 * `requireInternet` runs the action when the connectivity check passes and
 * opens the offline modal when it doesn't. "Try again" in the modal re-checks
 * and runs the original action on success.
 */
export function useInternetGate() {
  const [offlineModalOpen, setOfflineModalOpen] = useState(false);
  const pendingActionRef = useRef<(() => void) | null>(null);

  const requireInternet = useCallback((action: () => void) => {
    if (checkInternet()) {
      pendingActionRef.current = null;
      setOfflineModalOpen(false);
      action();
    } else {
      pendingActionRef.current = action;
      setOfflineModalOpen(true);
    }
  }, []);

  const retry = useCallback(() => {
    const action = pendingActionRef.current;
    if (action) requireInternet(action);
  }, [requireInternet]);

  const dismiss = useCallback(() => {
    pendingActionRef.current = null;
    setOfflineModalOpen(false);
  }, []);

  return { requireInternet, offlineModalOpen, retry, dismiss };
}
