import { useEffect, type RefObject } from 'react';

/**
 * Dismiss an open menu or floating panel on a press outside `ref` or on
 * Escape. Shared by every popover in the app so they all feel the same.
 *
 * `primaryOnly` ignores non-primary buttons, for panels toggled by
 * right-click (the toggle's own contextmenu event must not re-dismiss).
 */
export function useDismissable(
  open: boolean,
  ref: RefObject<HTMLElement | null>,
  onDismiss: () => void,
  { primaryOnly = false }: { primaryOnly?: boolean } = {}
): void {
  useEffect(() => {
    if (!open) return;
    const onPointerDown = (e: PointerEvent) => {
      if (primaryOnly && e.button !== 0) return;
      if (!ref.current?.contains(e.target as Node)) onDismiss();
    };
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onDismiss();
    };
    document.addEventListener('pointerdown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [open, ref, onDismiss, primaryOnly]);
}
