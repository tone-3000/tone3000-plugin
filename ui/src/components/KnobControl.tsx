import React, { useEffect, useRef } from 'react';
import { KnobHeadless } from 'react-knob-headless';
import { KnobInner } from './KnobInner';

interface KnobControlProps {
  label: string;
  value: number;
  onChange: (value: number) => void;
  size?: number;
  labelSize?: number;
  labelBottom?: boolean;
  innerColor?: string;
}

export const KnobControl: React.FC<KnobControlProps> = ({
  label,
  value,
  onChange,
  size = 64,
  labelSize = 14,
  labelBottom = true,
  innerColor = '#1C1C1E',
}) => {
  // const angleDeg = value * 270 - 135; // -135..+135
  const knobRef = useRef<HTMLDivElement>(null);

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
    };

    // Add event listeners
    knobElement.addEventListener('selectstart', preventSelection);
    knobElement.addEventListener('dragstart', preventSelection);
    knobElement.addEventListener('mousedown', handleMouseDown);
    document.addEventListener('mouseup', handleMouseUp);

    return () => {
      // Cleanup
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
      {/* <span style={{
        fontSize: '12px',
        fontWeight: 'bold',
        textAlign: 'center',
        color: '#808080',
        fontFamily: 'Courier New, monospace'
      }}>
        {value.toFixed(2)}
      </span> */}

      <KnobHeadless
        ref={knobRef}
        aria-label={label}
        valueRaw={value}
        valueMin={0}
        valueMax={1}
        dragSensitivity={0.006}
        valueRawRoundFn={(x) => Math.round(x * 100) / 100}
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
        {/* New gradient knob design */}
        <KnobInner value={value} size={size} innerColor={innerColor} />

        {/* Old knob image with rotation - commented out for easy revert */}
        {/* <img
          src={knobImage}
          alt={label}
          draggable={false}
          style={{
            width: '100%',
            height: '100%',
            transform: `rotate(${angleDeg.toFixed(2)}deg)`,
            transition: 'none',
            pointerEvents: 'none',
            userSelect: 'none'
          }}
        /> */}
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
