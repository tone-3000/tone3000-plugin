import { useCallback, useRef, useState } from 'react';
import { T3K_API } from '../t3k/config';

/**
 * Probe timeout. Nothing user-visible ever waits on a probe, so this only
 * bounds how long the background check can stay in flight.
 */
const PROBE_TIMEOUT_MS = 8000;

/** Minimum gap between background probes; retry bypasses it. */
const PROBE_TTL_MS = 30_000;

/**
 * Gap before the confirmation probe. One transient failure (DAW startup
 * contention, Wi-Fi renegotiating after wake) must not raise the alarm.
 */
const CONFIRM_DELAY_MS = 2000;

/**
 * Instant connectivity check via `navigator.onLine`. `false` means the OS has
 * no network interface up (the "internet not set up" case we're guarding
 * against); `true` doesn't guarantee the wider internet is reachable, which
 * is what the recovery paths are for (failed-navigation recovery, block retry).
 * Gating stays probe-free so the + button is instant.
 */
function checkInternet(): boolean {
  return typeof navigator === 'undefined' || navigator.onLine !== false;
}

/**
 * - ok: the TLS handshake with the TONE3000 origin completed.
 * - insecure: the fetch failed at the network layer (DNS, refused, TLS).
 * - inconclusive: timeout or an unexpected error; no evidence either way.
 */
type ProbeResult = 'ok' | 'insecure' | 'inconclusive';

/**
 * One HTTPS reachability probe against the TONE3000 origin. OAuth navigates
 * this same webview to tone3000.com, and when TLS is broken (wrong system
 * clock, intercepting proxy or security software) that navigation strands the
 * webview on a dead page. The probe exists to explain that failure, not to
 * prevent it. `no-cors` because only the TLS handshake matters, not the
 * response.
 *
 * The timeout is built from AbortController + setTimeout instead of
 * `AbortSignal.timeout()`: the system WebKit on older macOS lacks the latter,
 * and a TypeError thrown by a missing API must never read as a broken
 * connection. Never throws.
 */
async function probeSecureConnection(): Promise<ProbeResult> {
  const controller = typeof AbortController === 'undefined' ? undefined : new AbortController();
  const timer = controller ? setTimeout(() => controller.abort(), PROBE_TIMEOUT_MS) : undefined;
  try {
    await fetch(T3K_API, {
      method: 'HEAD',
      mode: 'no-cors',
      cache: 'no-store',
      signal: controller?.signal,
    });
    return 'ok';
  } catch (err) {
    // Our own timeout firing: a slow network, not evidence about TLS.
    if ((err as { name?: string } | null)?.name === 'AbortError') return 'inconclusive';
    // fetch reports network-layer failures (DNS, connection refused, TLS
    // rejection) as TypeError. Anything else is an environment/programming
    // error and must stay silent.
    return err instanceof TypeError ? 'insecure' : 'inconclusive';
  } finally {
    if (timer !== undefined) clearTimeout(timer);
  }
}

export type ConnectionProblem = 'offline' | 'insecure';

/**
 * First line of defence for network-dependent actions (add / swap / login /
 * select):
 *
 *   const gate = useConnectionGate();
 *   const onAdd = () => gate.requireConnection(() => startSelectFlow());
 *   ...
 *   <ConnectionModal problem={gate.problem}
 *                    onRetry={gate.retry} onDismiss={gate.dismiss} />
 *
 * `requireConnection` is non-blocking by design. If the OS reports no
 * connection at all, the offline modal opens and the action is queued for
 * "Try again" (instant, no probe). Otherwise the action runs immediately and
 * a throttled background probe verifies HTTPS to TONE3000 on the side;
 * nothing ever waits on a probe.
 *
 * `isOnline` is the same instant OS check, for the callers that have
 * something better to do offline than open a modal (the + button falls back
 * to the on-disk tone library; see useToneLoadFlow).
 *
 * The insecure modal is diagnostic, not a gate: it appears only after two
 * consecutive network-layer failures while the OS still reports a
 * connection, by which point the triggering action has already failed on its
 * own recovery paths and the modal explains why. Timeouts count as
 * inconclusive and never alarm, and a later successful probe (or retry)
 * closes the modal, so a transient blip can't leave a stale warning behind.
 */
export function useConnectionGate() {
  const [problem, setProblem] = useState<ConnectionProblem | null>(null);
  // Queued action for the offline modal only; "Try again" re-runs it.
  const pendingActionRef = useRef<(() => void | Promise<void>) | null>(null);
  const probingRef = useRef(false);
  const lastProbeAtRef = useRef(0);

  /**
   * Fire-and-forget verification. Throttled unless forced (retry button);
   * a first "insecure" result is re-checked before surfacing the modal.
   */
  const verifyInBackground = useCallback(async (force = false) => {
    if (probingRef.current) return;
    if (!force && Date.now() - lastProbeAtRef.current < PROBE_TTL_MS) return;
    probingRef.current = true;
    try {
      let result = await probeSecureConnection();
      if (result === 'insecure') {
        await new Promise((resolve) => setTimeout(resolve, CONFIRM_DELAY_MS));
        result = await probeSecureConnection();
      }
      if (result === 'ok') {
        // Auto-heal: never leave a stale warning up once TLS works again.
        setProblem((p) => (p === 'insecure' ? null : p));
      } else if (result === 'insecure' && checkInternet()) {
        // Don't replace an open offline modal (and its queued action).
        setProblem((p) => (p === null ? 'insecure' : p));
      }
      // Inconclusive results stay silent: no evidence, no alarm.
    } finally {
      probingRef.current = false;
      lastProbeAtRef.current = Date.now();
    }
  }, []);

  const requireConnection = useCallback(
    (action: () => void | Promise<void>) => {
      if (!checkInternet()) {
        pendingActionRef.current = action;
        setProblem('offline');
        return;
      }
      pendingActionRef.current = null;
      void action();
      void verifyInBackground();
    },
    [verifyInBackground]
  );

  const retry = useCallback(async () => {
    const action = pendingActionRef.current;
    if (action) {
      // Offline modal: re-check the OS flag and release the queued action.
      if (checkInternet()) {
        pendingActionRef.current = null;
        setProblem(null);
        void action();
        void verifyInBackground();
      }
      return;
    }
    // Insecure modal: force a fresh probe; auto-heal closes it on success.
    await verifyInBackground(true);
  }, [verifyInBackground]);

  const dismiss = useCallback(() => {
    pendingActionRef.current = null;
    setProblem(null);
  }, []);

  return { requireConnection, isOnline: checkInternet, problem, retry, dismiss };
}
