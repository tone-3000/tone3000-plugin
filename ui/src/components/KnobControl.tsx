import React, { useCallback, useEffect, useRef, useState } from 'react';
import { KnobHeadless } from 'react-knob-headless';
import { KnobInner } from './KnobInner';
import type { KnobVariant } from './KnobInner';
import type { KnobScale } from './knobScale';
import { percentScale } from './knobScale';
import { helpProps, pinHelp, unpinHelp } from './helpText';
import { SURFACE_RAISED } from './theme';

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
 */
interface KnobControlProps {
  label: string;
  value: number;
  onChange: (value: number) => void;
  size?: number;
  labelSize?: number;
  labelBottom?: boolean;
  innerColor?: string;
  /** Visual/geometry variant (see KnobInner). Bipolar knobs snap to exact
      center so the zero detent genuinely means zero. */
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
  /** One-line hint for the faceplate help readout, shown while hovered or
      dragging (see helpText.ts). */
  help?: string;
  /** Fires true on grab / false on release, so owners of optimistic values
      can pause external syncs mid-drag (a stale poll must not fight the
      pointer). */
  onDragStateChange?: (dragging: boolean) => void;
}

const BASE_SENSITIVITY = 0.006;
const FINE_FACTOR = 8;

/** Bipolar center detent: values within the snap window collapse to exactly
    0.5 so the DSP's "center = skip processing" branch is actually reachable
    by drag (not just by precise pixel luck). Fine mode narrows the window
    and quantum so Shift genuinely adds precision. */
const roundKnobValue = (x: number, snapCenter: boolean, fine: boolean) => {
  if (snapCenter && Math.abs(x - 0.5) < (fine ? 0.004 : 0.02)) return 0.5;
  const quantum = fine ? 10000 : 100;
  return Math.round(x * quantum) / quantum;
};

export const KnobControl: React.FC<KnobControlProps> = ({
  label,
  value,
  onChange,
  size = 64,
  labelSize = 14,
  labelBottom = true,
  innerColor = '#000000',
  variant = 'full',
  min = 0,
  max = 1,
  scale = percentScale,
  defaultValue,
  help,
  onDragStateChange,
}) => {
  const knobRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  // The mouseup listener lives on `document` (releases can land anywhere),
  // so it must ignore mouseups that don't belong to this knob's drag.
  const draggingRef = useRef(false);

  const [dragging, setDragging] = useState(false);
  const [fine, setFine] = useState(false);
  const [editText, setEditText] = useState<string | null>(null); // null = not editing
  const editing = editText !== null;

  // Latest callbacks/props for the mount-once listener effect.
  const dragStateRef = useRef(onDragStateChange);
  dragStateRef.current = onDragStateChange;
  const onChangeRef = useRef(onChange);
  onChangeRef.current = onChange;
  const defaultValueRef = useRef(defaultValue);
  defaultValueRef.current = defaultValue;

  useEffect(() => {
    const knobElement = knobRef.current;
    if (!knobElement) return;

    const preventSelection = (e: Event) => {
      e.preventDefault();
      return false;
    };

    // Shift toggles fine mode live, including mid-drag. Keydown/keyup alone
    // can't be trusted here: the plugin webview doesn't reliably deliver
    // bare-modifier key events (the native wrapper consumes them), which
    // silently killed mid-drag Shift. So the primary source is the modifier
    // state carried on the pointer events themselves (same approach as the
    // EQ editors); the key listeners stay as a bonus so fine mode can engage
    // while the pointer is stationary.
    const handleShift = (e: KeyboardEvent) => {
      if (e.key === 'Shift') setFine(e.type === 'keydown');
    };
    const handleDragPointerMove = (e: PointerEvent) => setFine(e.shiftKey);

    const handleMouseDown = (e: MouseEvent) => {
      // Alt/Option-click: reset to default. The drag still engages beneath,
      // which is harmless — releasing without moving stays at the default.
      if (e.altKey && defaultValueRef.current !== undefined) {
        onChangeRef.current(defaultValueRef.current);
      }

      draggingRef.current = true;
      setFine(e.shiftKey);
      setDragging(true);
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

    const handleMouseUp = () => {
      if (!draggingRef.current) return;
      draggingRef.current = false;
      setDragging(false);
      setFine(false);
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

    knobElement.addEventListener('selectstart', preventSelection);
    knobElement.addEventListener('dragstart', preventSelection);
    knobElement.addEventListener('mousedown', handleMouseDown);
    document.addEventListener('mouseup', handleMouseUp);

    return () => {
      knobElement.removeEventListener('selectstart', preventSelection);
      knobElement.removeEventListener('dragstart', preventSelection);
      knobElement.removeEventListener('mousedown', handleMouseDown);
      document.removeEventListener('mouseup', handleMouseUp);
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
    setEditText(scale.editText(value));
  }, [scale, value]);

  useEffect(() => {
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

  const showReadout = !editing && dragging;
  // Fixed-footprint label slot: readout/input can be wider than the label
  // but must never shift surrounding layout, so the slot is sized once and
  // its content overflows symmetrically.
  const slotHeight = Math.round(labelSize * 1.2);

  return (
    <div
      {...(help ? helpProps(help) : {})}
      style={{
        display: 'flex',
        flexDirection: labelBottom ? 'column' : 'column-reverse',
        justifyContent: 'center',
        alignItems: 'center',
        gap: labelSize === 14 ? '14px' : '10px',
      }}
    >
      <KnobHeadless
        ref={knobRef}
        aria-label={label}
        valueRaw={value}
        valueMin={min}
        valueMax={max}
        dragSensitivity={fine ? BASE_SENSITIVITY / FINE_FACTOR : BASE_SENSITIVITY}
        valueRawRoundFn={(x) => roundKnobValue(x, variant === 'bipolar', fine)}
        valueRawDisplayFn={(x) => scale.format(x)}
        onValueRawChange={onChange}
        onDoubleClick={openEditor}
        className="knob"
        style={{
          width: size,
          height: size,
          position: 'relative',
          userSelect: 'none',
          outline: 'none',
          boxShadow: 'none',
          WebkitTapHighlightColor: 'transparent',
          cursor: 'pointer',
        }}
      >
        <KnobInner value={value} size={size} innerColor={innerColor} variant={variant} />
      </KnobHeadless>

      <div
        style={{
          width: size,
          height: slotHeight,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          overflow: 'visible',
        }}
      >
        {editing ? (
          <input
            ref={inputRef}
            value={editText}
            onChange={(e) => setEditText(e.target.value)}
            onBlur={commitEdit}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === 'Enter') commitEdit();
              else if (e.key === 'Escape') setEditText(null);
            }}
            inputMode="decimal"
            style={{
              width: Math.max(size, 34),
              height: slotHeight + 4,
              flexShrink: 0,
              boxSizing: 'border-box',
              background: SURFACE_RAISED,
              border: '1px solid rgba(235, 235, 245, 0.3)',
              borderRadius: '4px',
              color: '#ffffff',
              fontSize: Math.min(labelSize, 11),
              textAlign: 'center',
              outline: 'none',
              padding: 0,
            }}
          />
        ) : (
          <span
            style={{
              fontSize: labelSize,
              fontWeight: 400,
              textAlign: 'center',
              color: '#ffffff',
              letterSpacing: showReadout ? 'normal' : '1px',
              whiteSpace: 'nowrap',
              fontVariantNumeric: 'tabular-nums',
            }}
          >
            {showReadout ? scale.format(value) : label}
          </span>
        )}
      </div>
    </div>
  );
};
