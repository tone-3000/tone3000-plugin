import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { KnobHeadless } from 'react-knob-headless';
import { KnobInner } from './KnobInner';
import type { KnobThumb, KnobVariant } from './KnobInner';
import type { KnobScale } from './knobScale';
import { percentScale } from './knobScale';
import { helpProps, pinHelp, unpinHelp } from './helpText';
import { GRAY, KNOB_LABEL_GAP, SURFACE_RAISED, WHITE } from './theme';
import { getUiScale, rem } from '../hooks/useUiScale';

/**
 * Knob interaction conventions (matching typical plugin UX):
 * - Drag vertically to adjust; hold Shift for 8x finer control (works
 *   mid-drag).
 * - The label swaps to a live value readout while dragging; it snaps back to
 *   the label the instant the pointer releases.
 * - Double-click opens inline text entry in real units (Enter commits,
 *   Escape cancels, blur commits).
 * - Alt/Option-click resets to the default value (when one is declared).
 * No scroll-wheel support on purpose: knobs sit inside the horizontally
 * scrolling chain view, and hijacking wheel events there hurts more than it
 * helps.
 *
 * On a touch screen the two mouse-only gestures are replaced rather than
 * dropped:
 * - Double tap resets to the default (there is no Alt key). Detected from
 *   the pointer stream, not from `dblclick`, which WKWebView ties to its
 *   own double-tap handling.
 * - Tapping the label under the knob opens the type-in editor (double tap
 *   is taken by the reset).
 * Both key off the gesture's own pointerType, so a mouse keeps desktop
 * behavior even on a hybrid device.
 */
interface KnobControlProps {
  label: string;
  value: number;
  onChange: (value: number) => void;
  size?: number;
  labelBottom?: boolean;
  /** Visual tone (see KnobInner): primary = a section's headline knob (the
      default), secondary = its darker companion trim. */
  thumb?: KnobThumb;
  /** Geometry variant (see KnobInner). Bipolar knobs snap to exact center so
      the zero detent genuinely means zero. */
  variant?: KnobVariant;
  /** Drag range (defaults 0..1). Pan halves pass 0..0.5 / 0.5..1 so the
      param keeps absolute positions while the knob covers its half track. */
  min?: number;
  max?: number;
  /** Normalized-to-units mapping for the readout and text entry. Defaults
      to a plain percentage. */
  scale?: KnobScale;
  /** Normalized default; enables Alt/Option-click reset. */
  defaultValue?: number;
  /** Extra work on Alt/Option-click reset (after writing defaultValue). Used
      by Spread/Align Offset to also restore the advanced deck defaults. */
  onReset?: () => void;
  /** One-line hint for the faceplate help readout, shown while hovered or
      dragging (see helpText.ts). */
  help?: string;
  /** Idle label in white instead of muted gray (pan rail: labels read as
      section titles above the [S|Ø] chips). Readout/edit stay white either
      way. */
  labelBright?: boolean;
  /** Fires true on grab / false on release, so owners of optimistic values
      can pause external syncs mid-drag (a stale poll must not fight the
      pointer). */
  onDragStateChange?: (dragging: boolean) => void;
}

/** Every knob label is 14px; faceplate chrome lift and secondary-knob
    centerlines are built around that slot height. */
const LABEL_SIZE = 14;

const BASE_SENSITIVITY = 0.006;
const FINE_FACTOR = 8;

/** Label → value readout swap is debounced on press so a quick tap (e.g.
    half of a double-tap heading into the type-in editor) never flashes the
    value. */
const READOUT_SHOW_MS = 150;
/** The readout also lingers briefly after release (same hold as the EQ
    faders) instead of snapping back to the label. */
const READOUT_HOLD_MS = 250;

/** Bipolar center detent (coarse drag only): values within the snap window
    collapse to exactly 0.5 so the DSP's "center = skip processing" branch
    is reachable by drag. Shift/fine skips the magnet: a 40-step dead zone
    at 0.1 ms resolution is what made Offset feel sticky. */
const roundKnobValue = (x: number, snapCenter: boolean, fine: boolean) => {
  if (snapCenter && !fine && Math.abs(x - 0.5) < 0.02) return 0.5;
  const quantum = fine ? 10000 : 100;
  return Math.round(x * quantum) / quantum;
};

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

/** Touch double tap: the usual recognizer window, and a slop wide enough
    for two taps by the same finger without being a drag. */
const DOUBLE_TAP_MS = 300;
const DOUBLE_TAP_SLOP_PX = 24;

export const KnobControl: React.FC<KnobControlProps> = ({
  label,
  value,
  onChange,
  size = 64,
  labelBottom = true,
  thumb = 'primary',
  variant = 'full',
  min = 0,
  max = 1,
  scale = percentScale,
  defaultValue,
  help,
  labelBright = false,
  onReset,
  onDragStateChange,
}) => {
  const knobRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  // The pointerup listener lives on `document` (releases can land anywhere),
  // so it must ignore releases that don't belong to this knob's drag.
  const draggingRef = useRef(false);
  // Accumulated drag value, deliberately UN-snapped. react-knob-headless
  // applies each event as `props.value + thisDelta`, so any native echo (or
  // a second move before React re-renders) drops prior deltas: the knob
  // sticks, then jumps. We own the math with this ref so every pixel counts.
  // The bipolar detent must never feed back into this accumulator: snapping
  // the accumulator itself discards each move's progress across the noon
  // window, so only a single fast event could ever escape it (a slow drag
  // would pin at center and then jump).
  const liveRef = useRef(value);
  // Last value actually emitted/shown (the detent-snapped one).
  const emittedRef = useRef(value);
  const lastYRef = useRef(0);
  const fineRef = useRef(false);
  // Touch double-tap recognizer state (time + position of the previous tap).
  const lastTapRef = useRef<{ at: number; x: number; y: number } | null>(null);
  // Where the current press went down, so a gesture that turns into a drag
  // can withdraw its tap candidate (see handleDragPointerMove).
  const pressOriginRef = useRef<{ x: number; y: number } | null>(null);
  // Pointer type of the last press anywhere in the control, recorded in the
  // capture phase. The dblclick guard and the label tap read it to tell a
  // finger from a mouse.
  const lastPointerTypeRef = useRef('');

  const [dragging, setDragging] = useState(false);
  const [fine, setFine] = useState(false);
  const [liveValue, setLiveValue] = useState(value);
  const [editText, setEditText] = useState<string | null>(null); // null = not editing
  const editing = editText !== null;
  const shownValue = dragging ? liveValue : value;

  // Latest callbacks/props for the mount-once listener effect.
  const dragStateRef = useRef(onDragStateChange);
  dragStateRef.current = onDragStateChange;
  const onChangeRef = useRef(onChange);
  onChangeRef.current = onChange;
  const defaultValueRef = useRef(defaultValue);
  defaultValueRef.current = defaultValue;
  const onResetRef = useRef(onReset);
  onResetRef.current = onReset;
  const valueRef = useRef(value);
  valueRef.current = value;
  const minRef = useRef(min);
  minRef.current = min;
  const maxRef = useRef(max);
  maxRef.current = max;
  const variantRef = useRef(variant);
  variantRef.current = variant;

  useEffect(() => {
    if (!draggingRef.current) {
      liveRef.current = value;
      emittedRef.current = value;
      setLiveValue(value);
    }
  }, [value]);

  useEffect(() => {
    const knobElement = knobRef.current;
    if (!knobElement) return;

    const preventSelection = (e: Event) => {
      e.preventDefault();
      return false;
    };

    const applyLive = (next: number) => {
      // Accumulate raw: the detent is applied to the emitted value only, so
      // drag progress keeps counting while the readout rests on center and
      // the knob glides out the far side of the window.
      const raw = clamp(next, minRef.current, maxRef.current);
      liveRef.current = raw;
      // Coarse bipolar detent only; Shift skips it so Offset can land on
      // sub-millisecond values next to zero. No 0.01 quantum here: that
      // was aria rounding, and applying it live is the "stuck then jump"
      // the faceplate knobs had.
      const v =
        variantRef.current === 'bipolar' && !fineRef.current && Math.abs(raw - 0.5) < 0.02
          ? 0.5
          : raw;
      if (v === emittedRef.current) return;
      emittedRef.current = v;
      setLiveValue(v);
      onChangeRef.current(v);
    };

    const setFineMode = (on: boolean) => {
      fineRef.current = on;
      setFine(on);
    };

    // Shift toggles fine mode live, including mid-drag. Keydown/keyup alone
    // can't be trusted here: the plugin webview doesn't reliably deliver
    // bare-modifier key events (the native wrapper consumes them), which
    // silently killed mid-drag Shift. So the primary source is the modifier
    // state carried on the pointer events themselves (same approach as the
    // EQ editors); the key listeners stay as a bonus so fine mode can engage
    // while the pointer is stationary.
    const handleShift = (e: KeyboardEvent) => {
      if (e.key === 'Shift') setFineMode(e.type === 'keydown');
    };
    const handleDragPointerMove = (e: PointerEvent) => {
      if (!draggingRef.current) return;
      // A press that travels is a drag, not the first half of a double tap.
      // Without this, dragging a knob and then tapping it inside the
      // recognizer window read as a pair and threw the drag away: the knob
      // snapped back to its default the moment you touched it again.
      const origin = pressOriginRef.current;
      if (
        origin !== null &&
        (Math.abs(e.clientX - origin.x) > DOUBLE_TAP_SLOP_PX ||
          Math.abs(e.clientY - origin.y) > DOUBLE_TAP_SLOP_PX)
      ) {
        pressOriginRef.current = null;
        lastTapRef.current = null;
      }
      const shift = e.shiftKey || e.getModifierState?.('Shift');
      if (shift !== fineRef.current) setFineMode(shift);
      // clientY is real px; divide by the UI scale so sensitivity stays
      // constant in design px (the same drag distance relative to the knob's
      // rendered size always covers the same value range).
      applyLive(
        liveRef.current +
          ((lastYRef.current - e.clientY) / getUiScale()) *
            (fineRef.current ? BASE_SENSITIVITY / FINE_FACTOR : BASE_SENSITIVITY)
      );
      lastYRef.current = e.clientY;
    };

    const resetToDefault = () => {
      const fallback = defaultValueRef.current;
      if (fallback === undefined) return false;
      onChangeRef.current(fallback);
      onResetRef.current?.();
      liveRef.current = fallback;
      emittedRef.current = fallback;
      setLiveValue(fallback);
      return true;
    };

    const handlePointerDown = (e: PointerEvent) => {
      if (e.button !== 0 && e.pointerType === 'mouse') return;
      // Own the gesture so react-knob-headless's useDrag (value + thisDelta)
      // never starts; that path is what fought the native echo.
      e.stopPropagation();
      knobElement.focus();

      // Touch: second tap of a double tap resets, and ends the gesture there.
      // Engaging the drag as well would let the few pixels of finger travel
      // between the two taps move the value straight back off the default.
      if (e.pointerType === 'touch') {
        const previous = lastTapRef.current;
        const isDoubleTap =
          previous !== null &&
          e.timeStamp - previous.at < DOUBLE_TAP_MS &&
          Math.abs(e.clientX - previous.x) < DOUBLE_TAP_SLOP_PX &&
          Math.abs(e.clientY - previous.y) < DOUBLE_TAP_SLOP_PX;
        lastTapRef.current = isDoubleTap
          ? null // a third tap starts a fresh pair, it is not another reset
          : { at: e.timeStamp, x: e.clientX, y: e.clientY };
        if (isDoubleTap && resetToDefault()) return;
      }
      // Alt/Option-click: reset to default. The drag still engages beneath,
      // which is harmless: releasing without moving stays at the default.
      // onReset runs after so owners can restore sibling defaults (e.g. the
      // Spread/Align advanced deck) in the same gesture.
      if (!(e.altKey && resetToDefault())) {
        liveRef.current = valueRef.current;
        emittedRef.current = valueRef.current;
        setLiveValue(valueRef.current);
      }

      draggingRef.current = true;
      pressOriginRef.current = { x: e.clientX, y: e.clientY };
      lastYRef.current = e.clientY;
      setFineMode(e.shiftKey || e.getModifierState?.('Shift'));
      setDragging(true);
      try {
        knobElement.setPointerCapture(e.pointerId);
      } catch {
        /* capture is best-effort; document listeners still cover the drag */
      }
      window.addEventListener('keydown', handleShift);
      window.addEventListener('keyup', handleShift);
      window.addEventListener('pointermove', handleDragPointerMove);

      // Prevent text selection during drag
      const bodyStyle = document.body.style as CSSStyleDeclaration & Record<string, string>;
      bodyStyle.userSelect = 'none';
      bodyStyle.webkitUserSelect = 'none';

      // Add class for CSS targeting
      document.body.classList.add('dragging');
      dragStateRef.current?.(true);
    };

    const handlePointerUp = () => {
      if (!draggingRef.current) return;
      draggingRef.current = false;
      pressOriginRef.current = null;
      setDragging(false);
      setFineMode(false);
      window.removeEventListener('keydown', handleShift);
      window.removeEventListener('keyup', handleShift);
      window.removeEventListener('pointermove', handleDragPointerMove);

      // Restore text selection
      const bodyStyle = document.body.style as CSSStyleDeclaration & Record<string, string>;
      bodyStyle.userSelect = '';
      bodyStyle.webkitUserSelect = '';

      // Remove class
      document.body.classList.remove('dragging');
      dragStateRef.current?.(false);
    };

    // Pointer events (not mouse events) so the drag state, and with it the
    // value readout and pinned hint, also engages for touch drags, which
    // never synthesize mouse events while moving.
    knobElement.addEventListener('selectstart', preventSelection);
    knobElement.addEventListener('dragstart', preventSelection);
    knobElement.addEventListener('pointerdown', handlePointerDown);
    document.addEventListener('pointerup', handlePointerUp);
    document.addEventListener('pointercancel', handlePointerUp);

    return () => {
      knobElement.removeEventListener('selectstart', preventSelection);
      knobElement.removeEventListener('dragstart', preventSelection);
      knobElement.removeEventListener('pointerdown', handlePointerDown);
      document.removeEventListener('pointerup', handlePointerUp);
      document.removeEventListener('pointercancel', handlePointerUp);
      window.removeEventListener('keydown', handleShift);
      window.removeEventListener('keyup', handleShift);
      window.removeEventListener('pointermove', handleDragPointerMove);

      // Ensure body styles are reset
      const bodyStyle = document.body.style as CSSStyleDeclaration & Record<string, string>;
      bodyStyle.userSelect = '';
      bodyStyle.webkitUserSelect = '';
      document.body.classList.remove('dragging');
    };
  }, []);

  // Hover is handled by the data-help attribute (see helpText.ts); pinning
  // keeps the hint up mid-drag, when the pointer can wander off the knob
  // without releasing.
  const helpRef = useRef(help);
  helpRef.current = help;
  useEffect(() => {
    const text = helpRef.current;
    if (!text || !dragging) return;
    pinHelp(text);
    return () => unpinHelp(text);
  }, [dragging]);

  const openEditor = useCallback(() => {
    setEditText(scale.editText(shownValue));
  }, [scale, shownValue]);

  // Layout effect so the focus lands in the same call stack as the tap or
  // double-click that opened the editor; WKWebView only raises the on-screen
  // keyboard for focus inside a user gesture.
  useLayoutEffect(() => {
    if (editing) {
      inputRef.current?.focus();
      inputRef.current?.select();
    }
  }, [editing]);

  const commitEdit = useCallback(() => {
    if (editText !== null) {
      const parsed = Number.parseFloat(editText.replace(',', '.'));
      if (Number.isFinite(parsed)) {
        const norm = Math.min(max, Math.max(min, scale.fromDisplay(parsed)));
        onChangeRef.current(roundKnobValue(norm, variant === 'bipolar', true));
      }
    }
    setEditText(null);
  }, [editText, max, min, scale, variant]);

  // Debounced readout visibility: the show timer outlasts a quick tap (so
  // double-tapping into the editor can't flash the value), and the hide
  // timer lets the final value linger for a beat after release. A change of
  // `dragging` cancels whichever timer is pending.
  const [readoutVisible, setReadoutVisible] = useState(false);
  useEffect(() => {
    const timer = window.setTimeout(
      () => setReadoutVisible(dragging),
      dragging ? READOUT_SHOW_MS : READOUT_HOLD_MS
    );
    return () => window.clearTimeout(timer);
  }, [dragging]);

  const showReadout = !editing && readoutVisible;
  const slotHeight = Math.round(LABEL_SIZE * 1.2);

  const labelText = (
    <span
      style={{
        fontSize: rem(LABEL_SIZE),
        fontWeight: 400,
        textAlign: 'center',
        // Idle labels are muted by default; pan-rail labels pass
        // labelBright to read as section titles. Readout is always white.
        color: showReadout || labelBright ? WHITE : GRAY,
        letterSpacing: 'normal',
        whiteSpace: 'nowrap',
        fontVariantNumeric: 'tabular-nums',
      }}
    >
      {showReadout ? scale.format(shownValue) : label}
    </span>
  );

  const editInput = (
    <input
      ref={inputRef}
      value={editText ?? ''}
      onChange={(e) => setEditText(e.target.value)}
      onBlur={commitEdit}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === 'Enter') commitEdit();
        else if (e.key === 'Escape') setEditText(null);
      }}
      inputMode="decimal"
      style={{
        width: '100%',
        height: rem(slotHeight + 4),
        boxSizing: 'border-box',
        background: SURFACE_RAISED,
        border: '1rem solid rgba(235, 235, 245, 0.3)',
        borderRadius: '4rem',
        color: '#ffffff',
        fontSize: '11rem',
        textAlign: 'center',
        outline: 'none',
        padding: 0,
      }}
    />
  );

  return (
    <div
      {...(help ? helpProps(help) : {})}
      onPointerDownCapture={(e) => (lastPointerTypeRef.current = e.pointerType)}
      style={{
        display: 'flex',
        flexDirection: labelBottom ? 'column' : 'column-reverse',
        justifyContent: 'center',
        alignItems: 'center',
        gap: `${KNOB_LABEL_GAP}rem`,
      }}
    >
      <KnobHeadless
        ref={knobRef}
        aria-label={label}
        valueRaw={shownValue}
        valueMin={min}
        valueMax={max}
        // Drag math lives in the pointer listeners above. The library applies
        // each event as `valueRaw + thisDelta`, which fights native echoes
        // and drops moves that land before the next render.
        dragSensitivity={0}
        valueRawRoundFn={(x) => roundKnobValue(x, variant === 'bipolar', fine)}
        valueRawDisplayFn={(x) => scale.format(x)}
        onValueRawChange={() => {}}
        // A touch double tap resets (see handlePointerDown); the dblclick
        // some engines synthesize for it must not also open the editor.
        onDoubleClick={() => {
          if (lastPointerTypeRef.current !== 'touch') openEditor();
        }}
        className="knob"
        style={{
          width: rem(size),
          height: rem(size),
          position: 'relative',
          userSelect: 'none',
          outline: 'none',
          boxShadow: 'none',
          WebkitTapHighlightColor: 'transparent',
          cursor: 'pointer',
        }}
      >
        <KnobInner value={shownValue} size={size} variant={variant} thumb={thumb} />
      </KnobHeadless>

      <div
        // Touch route into the type-in editor: tap the label. Desktop keeps
        // the double-click on the knob face and ignores clicks here.
        onClick={() => {
          if (lastPointerTypeRef.current === 'touch' && !editing) openEditor();
        }}
        style={{
          width: rem(size),
          height: rem(slotHeight),
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          overflow: 'visible',
        }}
      >
        {editing ? (
          <span style={{ width: rem(size), flexShrink: 0, display: 'inline-block' }}>
            {editInput}
          </span>
        ) : (
          labelText
        )}
      </div>
    </div>
  );
};
