import React, { useEffect, useRef } from 'react';
import { KnobHeadless } from 'react-knob-headless';
import { KnobInner } from './KnobInner';
import type { KnobVariant } from './KnobInner';

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
  /** Fires true on grab / false on release, so owners of optimistic values
      can pause external syncs mid-drag (a stale poll must not fight the
      pointer). */
  onDragStateChange?: (dragging: boolean) => void;
}

/** Bipolar center detent: values within the snap window collapse to exactly
    0.5 so the DSP's "center = skip processing" branch is actually reachable
    by drag (not just by precise pixel luck). */
const roundKnobValue = (x: number, snapCenter: boolean) => {
  if (snapCenter && Math.abs(x - 0.5) < 0.02) return 0.5;
  return Math.round(x * 100) / 100;
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
  onDragStateChange,
}) => {
  const knobRef = useRef<HTMLDivElement>(null);
  // Latest callback without retriggering the listener effect.
  const dragStateRef = useRef(onDragStateChange);
  dragStateRef.current = onDragStateChange;

  useEffect(() => {
    const knobElement = knobRef.current;
    if (!knobElement) return;

    const preventSelection = (e: Event) => {
      e.preventDefault();
      return false;
    };

    const handleMouseDown = () => {
      // Prevent text selection during drag
      const bodyStyle = document.body.style as any;
      bodyStyle.userSelect = 'none';
      bodyStyle.webkitUserSelect = 'none';
      bodyStyle.mozUserSelect = 'none';
      bodyStyle.msUserSelect = 'none';

      // Add class for CSS targeting
      document.body.classList.add('dragging');
      dragStateRef.current?.(true);
    };

    const handleMouseUp = () => {
      // Restore text selection
      const bodyStyle = document.body.style as any;
      bodyStyle.userSelect = '';
      bodyStyle.webkitUserSelect = '';
      bodyStyle.mozUserSelect = '';
      bodyStyle.msUserSelect = '';

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

      // Ensure body styles are reset
      const bodyStyle = document.body.style as any;
      bodyStyle.userSelect = '';
      bodyStyle.webkitUserSelect = '';
      bodyStyle.mozUserSelect = '';
      bodyStyle.msUserSelect = '';
      document.body.classList.remove('dragging');
    };
  }, []);

  return (
    <div
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
        dragSensitivity={0.006}
        valueRawRoundFn={(x) => roundKnobValue(x, variant === 'bipolar')}
        valueRawDisplayFn={(x) => `${x.toFixed(2)}`}
        onValueRawChange={onChange}
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

      <span
        style={{
          fontSize: labelSize,
          fontWeight: 400,
          textAlign: 'center',
          color: '#ffffff',
          letterSpacing: '1px',
        }}
      >
        {label}
      </span>
    </div>
  );
};
