import React from 'react';
import { X } from 'lucide-react';
import { HELP, helpProps, setHintsEnabled, useHelpText, useHintsEnabled } from './helpText';
import { useCpuPercent } from '../hooks/useMeters';
import {
  BORDER,
  MUTED,
  WHITE,
  segmentedCellStyle,
  segmentedGroupStyle,
} from './theme';

/** Chrome height added below the plugin when hints are enabled (see Plugin). */
export const HINT_HEIGHT = 36;

interface HintBarProps {
  /** Global NAM A2 size (false = lite, true = full) — see useChainState. */
  namFullSize: boolean;
  onNamFullSizeChange: (full: boolean) => void;
}

/** Secondary home of the NAM A2 size setting (the primary lives in Settings →
    Advanced): one machine-wide LITE/FULL preference for every NAM block. */
const NamSizeToggle: React.FC<HintBarProps> = ({ namFullSize, onNamFullSizeChange }) => (
  <div {...helpProps(HELP.namSize)} style={segmentedGroupStyle()}>
    {([false, true] as const).map((full) => (
      <button
        key={String(full)}
        type="button"
        onClick={() => onNamFullSizeChange(full)}
        style={{
          ...segmentedCellStyle(false),
          color: namFullSize === full ? '#ffffff' : MUTED,
          transition: 'color 0.15s ease',
        }}
      >
        {full ? 'FULL' : 'LITE'}
      </button>
    ))}
  </div>
);

/** Audio-callback load. Tabular numerals + a fixed-width value slot so the
    row doesn't shimmy as digits change. */
const CpuReadout: React.FC = () => {
  const cpu = useCpuPercent();
  return (
    <span
      {...helpProps(HELP.cpuLoad)}
      style={{
        display: 'flex',
        alignItems: 'baseline',
        gap: '6px',
        fontSize: '12px',
        fontWeight: 400,
        color: MUTED,
        fontVariantNumeric: 'tabular-nums',
        flexShrink: 0,
        cursor: 'default',
      }}
    >
      <span>CPU</span>
      <span style={{ minWidth: '38px', textAlign: 'right' }}>{cpu.toFixed(1)}%</span>
    </span>
  );
};

/**
 * Dedicated hint strip under the faceplate: black (so it reads as chrome, not
 * part of the plate) and always present while hints are enabled, so showing a
 * hint never shifts layout. Like the banner, it grows the window rather than
 * eating into the plugin — Plugin adds HINT_HEIGHT to the window height.
 * The right side carries the machine-wide NAM A2 size toggle and the CPU
 * readout; the × hides the bar entirely — the Settings "Info Bar" toggle
 * brings it back.
 */
export const HintBar: React.FC<HintBarProps> = ({ namFullSize, onNamFullSizeChange }) => {
  const enabled = useHintsEnabled();
  const text = useHelpText();
  if (!enabled) return null;

  return (
    <div
      style={{
        width: '100%',
        height: `${HINT_HEIGHT}px`,
        display: 'flex',
        alignItems: 'center',
        gap: '16px',
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
          fontSize: '13px',
          // Hint sentences are body text: reset the global 600 default.
          fontWeight: 400,
          lineHeight: 1.35,
          color: MUTED,
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}
      >
        {text ?? ''}
      </span>
      <NamSizeToggle namFullSize={namFullSize} onNamFullSizeChange={onNamFullSizeChange} />
      <CpuReadout />
      <button
        onClick={() => setHintsEnabled(false)}
        {...helpProps(HELP.hideHints)}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          background: 'transparent',
          border: 'none',
          color: WHITE,
          cursor: 'pointer',
          padding: '2px',
          flexShrink: 0,
        }}
      >
        <X size={16} />
      </button>
    </div>
  );
};
