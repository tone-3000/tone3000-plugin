import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { RotateCcw, X } from 'lucide-react';
import { useMidiMap } from '../hooks/useMidiMap';
import type { MidiMapping } from '../types/midiMap';
import type { ChainItem, ToneBlock } from '../types/chain';
import { FieldRow, FIELD_BORDER, SelectField, captionStyle, ctaButtonStyle } from './controls';
import { MAPPABLE_TARGETS, behaviorLabel, sourceLabel, targetById } from './midiCatalog';
import { BORDER, MUTED, SUBTLE, BRAND_YELLOW } from './theme';

/**
 * MIDI Mapping sub-screen (Plugin Settings): control the plugin from pedals
 * and knobs. Opened from the main plugin settings screen, same pattern as
 * Advanced. Lives under Plugin Settings (not System) because the map reads
 * the processor's MIDI buffer, so it works identically in DAW builds where
 * the System tab doesn't exist. Mappings serialize with plugin state.
 *
 * Learn flow: pick a target (or hit re-learn on a row), then move a hardware
 * control; the first CC / note-on wins. The engine owns the armed state
 * (this component just renders it), so learn survives screen switches and
 * completes from the hardware side via the midiMapChanged event.
 *
 * Rows are two lines (target on the left, where block powers show the tone
 * currently in that chain slot; source + behavior on the right) so nothing
 * ellipses at the settings panel's width.
 */

/** Give up an armed learn after this long (mirrors the mockup's ~10 s). */
const LEARN_TIMEOUT_MS = 10000;

const channelOptions = [
  { value: '0', label: 'Omni' },
  ...Array.from({ length: 16 }, (_, i) => ({ value: String(i + 1), label: `Channel ${i + 1}` })),
];

/** "block3Power" → {2, left}, "rightBlock3Power" → {2, right}; null for
    anything else (mirrors the native parse). */
const blockPowerTarget = (targetId: string): { index: number; right: boolean } | null => {
  const match = /^(right)?[bB]lock(\d+)Power$/.exec(targetId);
  return match ? { index: Number(match[2]) - 1, right: match[1] !== undefined } : null;
};

/** Borderless row action, house icon-chrome style: muted at rest, white on
    hover, with no box like the faceplate's icon buttons. */
const rowIconButtonStyle: React.CSSProperties = {
  width: '26px',
  height: '26px',
  flexShrink: 0,
  display: 'grid',
  placeItems: 'center',
  background: 'none',
  border: 'none',
  padding: 0,
  color: MUTED,
  cursor: 'pointer',
};

const RowIconButton: React.FC<{
  label: string;
  onClick: () => void;
  children: React.ReactNode;
}> = ({ label, onClick, children }) => (
  <button
    onClick={onClick}
    title={label}
    aria-label={label}
    style={rowIconButtonStyle}
    onMouseEnter={(e) => (e.currentTarget.style.color = '#ffffff')}
    onMouseLeave={(e) => (e.currentTarget.style.color = MUTED)}
  >
    {children}
  </button>
);

const rowStyle = (first: boolean): React.CSSProperties => ({
  display: 'flex',
  alignItems: 'center',
  gap: '14px',
  padding: '11px 14px',
  borderTop: first ? 'none' : BORDER,
});

const rowTitleStyle: React.CSSProperties = {
  display: 'block',
  fontSize: '14px',
  fontWeight: 600,
  color: '#ffffff',
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  whiteSpace: 'nowrap',
};

const rowSubtitleStyle: React.CSSProperties = {
  display: 'block',
  fontSize: '11px',
  fontWeight: 400,
  color: SUBTLE,
  marginTop: '3px',
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  whiteSpace: 'nowrap',
};

/** Target name over its context (group; block powers add the tone currently
    sitting in that chain slot). */
const TargetCell: React.FC<{ targetId: string; context: string }> = ({ targetId, context }) => (
  <span style={{ flex: 1, minWidth: 0 }}>
    <span style={rowTitleStyle}>{targetById.get(targetId)?.name ?? targetId}</span>
    <span style={rowSubtitleStyle}>{context}</span>
  </span>
);

const MappingRow: React.FC<{
  mapping: MidiMapping;
  context: string;
  first: boolean;
  onRelearn: () => void;
  onRemove: () => void;
}> = ({ mapping, context, first, onRelearn, onRemove }) => (
  <div style={rowStyle(first)}>
    <TargetCell targetId={mapping.targetId} context={context} />
    <span style={{ flexShrink: 0, textAlign: 'right' }}>
      <span
        style={{
          display: 'block',
          fontSize: '13px',
          fontWeight: 400,
          fontFamily: 'monospace',
          color: '#ffffff',
          whiteSpace: 'nowrap',
        }}
      >
        {sourceLabel(mapping)}
      </span>
      <span style={{ ...rowSubtitleStyle, textAlign: 'right' }}>{behaviorLabel(mapping)}</span>
    </span>
    <span style={{ display: 'flex', gap: '2px', flexShrink: 0 }}>
      <RowIconButton label="Re-learn" onClick={onRelearn}>
        <RotateCcw size={14} />
      </RowIconButton>
      <RowIconButton label="Remove" onClick={onRemove}>
        <X size={15} />
      </RowIconButton>
    </span>
  </div>
);

const LearningRow: React.FC<{
  targetId: string;
  context: string;
  first: boolean;
  onCancel: () => void;
}> = ({ targetId, context, first, onCancel }) => (
  <div style={{ ...rowStyle(first), background: '#000' }}>
    <TargetCell targetId={targetId} context={context} />
    <span
      style={{
        flexShrink: 0,
        display: 'flex',
        alignItems: 'center',
        gap: '8px',
        fontSize: '12px',
        fontWeight: 400,
        color: '#ffffff',
        whiteSpace: 'nowrap',
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
      Listening…
    </span>
    <button
      onClick={onCancel}
      style={{
        background: 'none',
        border: 'none',
        padding: '4px 2px',
        flexShrink: 0,
        color: '#ffffff',
        fontSize: '12px',
        fontWeight: 600,
        cursor: 'pointer',
      }}
    >
      Cancel
    </button>
  </div>
);

export const MidiMapSettings: React.FC<{
  /** Chain lanes (block powers are positional over each lane's tone blocks);
      lets rows and the picker show what each block slot currently holds.
      `chainRight` is null outside stereo mode. */
  chain: ChainItem[];
  chainRight: ChainItem[] | null;
}> = ({ chain, chainRight }) => {
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

  // Block powers address a lane's Nth *tone* block (insert slots skipped),
  // matching the native toggle's positional walk.
  const stereo = chainRight != null;
  const toneBlocks = useMemo(
    () => chain.filter((item): item is ToneBlock => item.kind === 'tone'),
    [chain]
  );
  const rightToneBlocks = useMemo(
    () => (chainRight ?? []).filter((item): item is ToneBlock => item.kind === 'tone'),
    [chainRight]
  );

  /** Row / picker subtitle: group, plus the lane and live tone title for
      block powers. A mapping can outlive its block (chains shrink, stereo
      turns off; mappings are positional pedalboard facts), so say so instead
      of showing a stale name. */
  const targetContext = useCallback(
    (targetId: string): string => {
      const block = blockPowerTarget(targetId);
      if (!block) return targetById.get(targetId)?.group ?? '';
      // Lanes are only worth naming while two exist.
      const lane = block.right ? 'Chain R' : stereo ? 'Chain L' : 'Chain';
      if (block.right && !stereo) return `${lane} · Stereo off`;
      const title = (block.right ? rightToneBlocks : toneBlocks)[block.index]?.tone.title;
      return `${lane} · ${title ?? 'Empty slot'}`;
    },
    [stereo, toneBlocks, rightToneBlocks]
  );

  const unmappedOptions = useMemo(() => {
    const mapped = new Set(state?.mappings.map((m) => m.targetId));
    return MAPPABLE_TARGETS.filter((t) => {
      if (mapped.has(t.id)) return false;
      // Only offer block powers for blocks that exist right now; the picker
      // names each one after the tone it currently holds.
      const block = blockPowerTarget(t.id);
      if (!block) return true;
      return block.index < (block.right ? rightToneBlocks : toneBlocks).length;
    }).map((t) => ({ value: t.id, label: t.name, sublabel: targetContext(t.id) }));
  }, [state?.mappings, toneBlocks, rightToneBlocks, targetContext]);

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
          {state.mappings.map((mapping, index) =>
            mapping.targetId === learnTargetId ? (
              <LearningRow
                key={mapping.targetId}
                targetId={mapping.targetId}
                context={targetContext(mapping.targetId)}
                first={index === 0}
                onCancel={() => actions.cancelLearn()}
              />
            ) : (
              <MappingRow
                key={mapping.targetId}
                mapping={mapping}
                context={targetContext(mapping.targetId)}
                first={index === 0}
                onRelearn={() => startLearn(mapping.targetId)}
                onRemove={() => actions.removeMapping(mapping.targetId)}
              />
            )
          )}
          {pendingLearn && (
            <LearningRow
              targetId={pendingLearn}
              context={targetContext(pendingLearn)}
              first={state.mappings.length === 0}
              onCancel={() => actions.cancelLearn()}
            />
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
              ...ctaButtonStyle,
              marginTop: '12px',
              boxSizing: 'border-box',
              ...(unmappedOptions.length === 0 && {
                border: FIELD_BORDER,
                color: SUBTLE,
                cursor: 'default',
              }),
            }}
          >
            Add Mapping
          </button>
        )}
        <p style={{ ...captionStyle, marginTop: '10px' }}>
          Map Previous / Next Preset to step through presets from CC or note buttons. Program
          change messages also switch presets directly, in the order shown in the preset
          browser.
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
