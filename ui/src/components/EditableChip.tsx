import React, { useEffect, useRef, useState } from 'react';
import { helpProps } from './helpText';
import { FONT_MONO, SUBTLE } from './theme';

/** Readout chip that doubles as text entry: click to type, Enter commits,
    Escape cancels, blur commits (same conventions as the knobs). The value
    area is a fixed width (sized to the longest possible reading) so the chip
    never resizes while values change or while editing. Shared by BlockEqView
    (Freq/Gain/Q) and ChainBlock's IR envelope row (Init/A Len/A Crv/D Len/
    D Lvl/D Crv) - the "graph/sliders for by-ear, chip for exact value"
    split used by both editors. */
export const EditableChip: React.FC<{
  label: string;
  text: string;
  /** Prefill for the editor (number only, unit-free where possible). */
  editText: string;
  /** Fixed width of the value area in px: the widest reading the chip shows. */
  valueWidth: number;
  onCommit: (raw: string) => void;
  disabled?: boolean;
  /** One-line hint for the faceplate help readout (see helpText.ts). */
  help?: string;
  style?: React.CSSProperties;
  /** Label/value font size in rem units. Defaults to the EQ chips' 12; the
      IR envelope row (six chips, longer labels than EQ's Freq/Gain/Q) passes
      a smaller size so the row fits the card width. */
  fontSize?: number;
}> = ({
  label,
  text,
  editText,
  valueWidth,
  onCommit,
  disabled = false,
  help,
  style,
  fontSize = 12,
}) => {
  const [draft, setDraft] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const editing = draft !== null;

  useEffect(() => {
    if (editing) inputRef.current?.focus();
  }, [editing]);

  const commit = () => {
    if (draft !== null && draft.trim() !== '') onCommit(draft);
    setDraft(null);
  };

  return (
    <div
      {...(help && !disabled ? helpProps(help) : {})}
      onClick={() => {
        // Editing starts from an empty box (caret at the left) with the
        // current value as placeholder; committing empty is a cancel.
        if (!disabled && !editing) setDraft('');
      }}
      style={{ ...style, cursor: disabled || editing ? undefined : 'text' }}
    >
      <span style={{ fontSize: `${fontSize}rem`, fontFamily: FONT_MONO, color: SUBTLE }}>
        {label}
      </span>
      {editing ? (
        <input
          ref={inputRef}
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            e.stopPropagation();
            if (e.key === 'Enter') commit();
            else if (e.key === 'Escape') setDraft(null);
          }}
          inputMode="decimal"
          placeholder={editText}
          style={{
            width: `${valueWidth}rem`,
            background: 'transparent',
            border: 'none',
            color: '#ffffff',
            fontSize: `${fontSize}rem`,
            fontFamily: FONT_MONO,
            textAlign: 'left',
            outline: 'none',
            padding: 0,
          }}
        />
      ) : (
        <span
          style={{
            width: `${valueWidth}rem`,
            fontSize: `${fontSize}rem`,
            fontFamily: FONT_MONO,
            color: '#ffffff',
            textAlign: 'left',
            whiteSpace: 'nowrap',
          }}
        >
          {text}
        </span>
      )}
    </div>
  );
};
