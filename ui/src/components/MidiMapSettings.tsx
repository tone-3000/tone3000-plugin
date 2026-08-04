import React, { useEffect, useMemo, useState } from 'react';
import { RotateCcw, X } from 'lucide-react';
import { useMidiMap } from '../hooks/useMidiMap';
import type { MidiMapping } from '../types/midiMap';
import { FieldRow, FIELD_BORDER, SelectField, captionStyle, outlinedFieldStyle } from './controls';
import {
  MAPPABLE_TARGETS,
  modeLabel,
  rangeLabel,
  sourceLabel,
  sourceSubtitle,
  targetById,
} from './midiCatalog';
import { MUTED, SUBTLE, BRAND_YELLOW } from './theme';

/**
 * MIDI Mapping sub-screen (Plugin Settings): control the plugin from pedals
 * and knobs. Opened from the main plugin settings screen — same pattern as
 * Advanced. Lives under Plugin Settings (not System) because the map reads
 * the processor's MIDI buffer, so it works identically in DAW builds where
 * the System tab doesn't exist. Mappings serialize with plugin state.
 *
 * Learn flow: pick a target (or hit re-learn on a row), then move a hardware
 * control; the first CC / note-on wins. The engine owns the armed state —
 * this component just renders it — so learn survives screen switches and
 * completes from the hardware side via the midiMapChanged event.
 *
 * Preset switching needs no mapping: program change n loads the nth preset
 * (a caption below the list says so).
 */

/** Give up an armed learn after this long (mirrors the mockup's ~10 s). */
const LEARN_TIMEOUT_MS = 10000;

const channelOptions = [
  { value: '0', label: 'Omni' },
  ...Array.from({ length: 16 }, (_, i) => ({ value: String(i + 1), label: `Channel ${i + 1}` })),
];

const rowButtonStyle: React.CSSProperties = {
  width: '28px',
  height: '28px',
  flexShrink: 0,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  background: 'none',
  border: FIELD_BORDER,
  borderRadius: '7px',
  color: MUTED,
  cursor: 'pointer',
};

/** Two-line cell: value on top, dim subtitle underneath. */
const Cell: React.FC<{
  flex: number;
  title: React.ReactNode;
  subtitle?: string;
  mono?: boolean;
}> = ({ flex, title, subtitle, mono }) => (
  <span style={{ flex, minWidth: 0 }}>
    <span
      style={{
        display: 'block',
        fontSize: mono ? '13px' : '14px',
        fontWeight: mono ? 400 : 600,
        fontFamily: mono ? 'monospace' : undefined,
        color: '#ffffff',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
      }}
    >
      {title}
    </span>
    {subtitle && (
      <span style={{ display: 'block', fontSize: '11px', fontWeight: 400, color: SUBTLE, marginTop: '2px' }}>
        {subtitle}
      </span>
    )}
  </span>
);

const rowStyle: React.CSSProperties = {
  display: 'flex',
  alignItems: 'center',
  gap: '12px',
  padding: '12px 14px',
};

const MappingRow: React.FC<{
  mapping: MidiMapping;
  onRelearn: () => void;
  onRemove: () => void;
}> = ({ mapping, onRelearn, onRemove }) => {
  const target = targetById.get(mapping.targetId);
  return (
    <div style={rowStyle}>
      <Cell flex={1.2} title={target?.name ?? mapping.targetId} subtitle={target?.group} />
      <Cell flex={1} title={sourceLabel(mapping)} subtitle={sourceSubtitle(mapping)} mono />
      <Cell flex={1} title={rangeLabel(mapping)} subtitle={modeLabel(mapping)} />
      <button onClick={onRelearn} title="Re-learn" aria-label="Re-learn" style={rowButtonStyle}>
        <RotateCcw size={13} />
      </button>
      <button onClick={onRemove} title="Remove" aria-label="Remove" style={rowButtonStyle}>
        <X size={14} />
      </button>
    </div>
  );
};

const LearningRow: React.FC<{ targetId: string; onCancel: () => void }> = ({
  targetId,
  onCancel,
}) => {
  const target = targetById.get(targetId);
  return (
    <div style={{ ...rowStyle, background: '#000' }}>
      <Cell flex={1.2} title={target?.name ?? targetId} subtitle={target?.group} />
      <span
        style={{
          flex: 2,
          minWidth: 0,
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          fontSize: '13px',
          fontWeight: 400,
          color: '#ffffff',
          animation: 't3kMidiListen 1.2s ease-in-out infinite',
        }}
      >
        <span
          aria-hidden
          style={{
            width: '7px',
            height: '7px',
            borderRadius: '50%',
            background: BRAND_YELLOW,
            flexShrink: 0,
          }}
        />
        Listening. Move a knob or press a pedal.
      </span>
      <button
        onClick={onCancel}
        style={{ ...rowButtonStyle, width: 'auto', padding: '0 12px', color: '#ffffff' }}
      >
        Cancel
      </button>
    </div>
  );
};

export const MidiMapSettings: React.FC = () => {
  const { state, actions } = useMidiMap(true);
  // "+ Add mapping" swaps to a target picker; choosing one arms learn.
  const [picking, setPicking] = useState(false);

  const learnTargetId = state?.learnTargetId ?? '';

  // Learn is armed until hardware answers; give up after a while so an
  // unplugged controller doesn't leave the row listening forever.
  useEffect(() => {
    if (!learnTargetId) return;
    const timeout = setTimeout(() => actions.cancelLearn(), LEARN_TIMEOUT_MS);
    return () => clearTimeout(timeout);
  }, [learnTargetId, actions]);

  const unmappedOptions = useMemo(() => {
    const mapped = new Set(state?.mappings.map((m) => m.targetId));
    return MAPPABLE_TARGETS.filter((t) => !mapped.has(t.id)).map((t) => ({
      value: t.id,
      label: `${t.name} (${t.group})`,
    }));
  }, [state?.mappings]);

  // Hosted/standalone builds always report a state; only the bridge-less dev
  // browser lands here.
  if (!state) return null;

  const startLearn = (targetId: string) => {
    setPicking(false);
    actions.startLearn(targetId);
  };

  // A learn armed for a not-yet-mapped target renders as a pending row at
  // the end of the list; re-learn on an existing row takes over that row.
  const pendingLearn =
    learnTargetId && !state.mappings.some((m) => m.targetId === learnTargetId)
      ? learnTargetId
      : '';

  return (
    <>
      <style>{`@keyframes t3kMidiListen { 0%, 100% { opacity: 1; } 50% { opacity: 0.45; } }`}</style>

      <div style={{ marginBottom: '28px' }}>
        <div style={{ border: FIELD_BORDER, borderRadius: '10px', overflow: 'hidden' }}>
          {state.mappings.map((mapping) =>
            mapping.targetId === learnTargetId ? (
              <LearningRow
                key={mapping.targetId}
                targetId={mapping.targetId}
                onCancel={() => actions.cancelLearn()}
              />
            ) : (
              <MappingRow
                key={mapping.targetId}
                mapping={mapping}
                onRelearn={() => startLearn(mapping.targetId)}
                onRemove={() => actions.removeMapping(mapping.targetId)}
              />
            )
          )}
          {pendingLearn && (
            <LearningRow targetId={pendingLearn} onCancel={() => actions.cancelLearn()} />
          )}
          {state.mappings.length === 0 && !pendingLearn && (
            <p
              style={{
                margin: 0,
                padding: '20px',
                textAlign: 'center',
                fontSize: '12px',
                fontWeight: 400,
                fontStyle: 'italic',
                color: SUBTLE,
              }}
            >
              No mappings yet. Add one, then move a control on your MIDI device.
            </p>
          )}
        </div>

        {picking ? (
          <div style={{ marginTop: '12px' }}>
            <SelectField
              value={null}
              placeholder="Choose a control"
              options={unmappedOptions}
              onChange={startLearn}
              ariaLabel="Control to map"
            />
          </div>
        ) : (
          <button
            onClick={() => setPicking(true)}
            disabled={unmappedOptions.length === 0}
            style={{
              ...outlinedFieldStyle,
              width: '100%',
              marginTop: '12px',
              padding: '12px 16px',
              cursor: unmappedOptions.length === 0 ? 'default' : 'pointer',
              color: unmappedOptions.length === 0 ? SUBTLE : '#ffffff',
              fontWeight: 600,
              fontSize: '13px',
            }}
          >
            + Add mapping
          </button>
        )}
        <p style={{ ...captionStyle, marginTop: '10px' }}>
          Presets need no mapping: program change messages switch presets in the order shown in
          the preset browser.
        </p>
      </div>

      <FieldRow label="MIDI Channel" help="Omni listens on every channel.">
        <SelectField
          value={String(state.channel)}
          options={channelOptions}
          onChange={(channel) => actions.setChannel(Number(channel))}
          ariaLabel="MIDI channel"
        />
      </FieldRow>
    </>
  );
};
