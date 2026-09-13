import { useCallback, useEffect, useRef } from 'react';
import type { MouseEvent, PointerEvent } from 'react';
import { getUiScale, IS_IOS } from './useUiScale';

/**
 * Touch and hold on a control that answers a right-click on desktop.
 *
 * WKWebView does not deliver a `contextmenu` event for a long press, so every
 * desktop affordance built on `onContextMenu` is simply unreachable on iOS.
 * This hook is the touch half: it spreads onto an element around the control,
 * fires the callback after the system's own long-press delay, and renders
 * nothing.
 *
 * Capture-phase handlers on purpose. The Spread/Align Offset knob owns its
 * pointer stream (KnobControl stops propagation on pointerdown), so a bubble
 * handler on an ancestor never sees the press. Capture runs before the knob's
 * own listener, and keeps seeing moves after the knob takes pointer capture,
 * because the wrapper is an ancestor of the capture target.
 *
 * It is deliberately not the gallery tile's gesture. A tile competes with
 * dnd-kit's lift, so its hold lives next to the drag slop it must respect
 * (see GalleryBlock).
 *
 * Gated on `pointerType === 'touch'` and on iOS, so mouse, pen and every
 * desktop build behave exactly as before.
 */

/** Matches the system's own touch-and-hold delay for a context menu. */
const HOLD_MS = 500;
/** Design px of travel that turns the hold into a drag (the knob under the
    finger owns the gesture from that point on). */
const HOLD_SLOP_DESIGN_PX = 8;

export interface TouchHoldProps {
  onPointerDownCapture?: (e: PointerEvent) => void;
  onPointerMoveCapture?: (e: PointerEvent) => void;
  onPointerUpCapture?: (e: PointerEvent) => void;
  onPointerCancelCapture?: (e: PointerEvent) => void;
  onClickCapture?: (e: MouseEvent) => void;
}

export const useTouchHold = (onHold: () => void): TouchHoldProps => {
  const timer = useRef<number | undefined>(undefined);
  const start = useRef<{ x: number; y: number; id: number } | null>(null);
  // A hold that fired must not also be read as a tap by whatever sits under
  // the finger (the knob's label tap opens the type-in editor on touch).
  const suppressClick = useRef(false);
  const onHoldRef = useRef(onHold);
  onHoldRef.current = onHold;

  const cancel = useCallback(() => {
    if (timer.current !== undefined) window.clearTimeout(timer.current);
    timer.current = undefined;
    start.current = null;
  }, []);

  useEffect(() => cancel, [cancel]);

  if (!IS_IOS) return {};

  return {
    onPointerDownCapture: (e: PointerEvent) => {
      if (e.pointerType !== 'touch') return;
      cancel();
      start.current = { x: e.clientX, y: e.clientY, id: e.pointerId };
      timer.current = window.setTimeout(() => {
        cancel();
        suppressClick.current = true;
        onHoldRef.current();
      }, HOLD_MS);
    },
    onPointerMoveCapture: (e: PointerEvent) => {
      const from = start.current;
      if (from == null || e.pointerId !== from.id) return;
      const slop = HOLD_SLOP_DESIGN_PX * getUiScale();
      if (Math.abs(e.clientX - from.x) > slop || Math.abs(e.clientY - from.y) > slop) cancel();
    },
    onPointerUpCapture: cancel,
    onPointerCancelCapture: cancel,
    onClickCapture: (e: MouseEvent) => {
      if (!suppressClick.current) return;
      suppressClick.current = false;
      e.preventDefault();
      e.stopPropagation();
    },
  };
};
