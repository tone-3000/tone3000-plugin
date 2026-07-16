import React from 'react';
import { X } from 'lucide-react';
import { HELP, helpProps, setHintsEnabled, useHelpText, useHintsEnabled } from './helpText';
import { BORDER, GRAY, SUBTLE } from './theme';

const BAR_HEIGHT = 22;

/**
 * Dedicated hint strip under the faceplate: black (so it reads as chrome, not
 * part of the plate) and always present while hints are enabled, so showing a
 * hint never shifts layout. The × disables hints entirely — the Settings
 * "Hints" toggle brings the bar back.
 */
export const HintBar: React.FC = () => {
  const enabled = useHintsEnabled();
  const text = useHelpText();
  if (!enabled) return null;

  return (
    <div
      style={{
        width: '100%',
        height: `${BAR_HEIGHT}px`,
        display: 'flex',
        alignItems: 'center',
        gap: '12px',
        flexShrink: 0,
        borderTop: BORDER,
        background: '#000000',
        padding: '0 24px',
        boxSizing: 'border-box',
      }}
    >
      <span
        style={{
          flex: 1,
          minWidth: 0,
          fontSize: '10px',
          lineHeight: 1,
          color: GRAY,
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}
      >
        {text ?? ''}
      </span>
      <button
        onClick={() => setHintsEnabled(false)}
        {...helpProps(HELP.hideHints)}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          background: 'transparent',
          border: 'none',
          color: SUBTLE,
          cursor: 'pointer',
          padding: '2px',
          flexShrink: 0,
        }}
      >
        <X size={11} />
      </button>
    </div>
  );
};
