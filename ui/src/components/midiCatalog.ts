import type { MidiMapping } from '../types/midiMap';

/**
 * Display catalog for the MIDI mapping UI: which targets are mappable and
 * how to present them. The native engine accepts any APVTS parameter id (or
 * block-power id) — this list is the UI's curation (setup-domain params like
 * calibration stay out; they describe the rig, not something you perform
 * with).
 */

export interface MappableTarget {
  /** APVTS parameter id or positional block power ("block1Power"). */
  id: string;
  name: string;
  /** Section subtitle, mirroring the faceplate's layout. */
  group: string;
  /** Drives the range/mode display: toggles show "On / Off · Toggle". */
  kind: 'continuous' | 'toggle';
}

/** Block-power targets are positional — "Block 1" is the chain's first tone
    block whatever it currently holds — so a mapping survives tone swaps and
    preset loads, like switches on a pedalboard. Left lane in stereo mode.
    Display-only cap (the engine takes up to 64): enough for any realistic
    pedalboard without burying the picker. */
const BLOCK_POWER_TARGETS = 12;

export const MAPPABLE_TARGETS: MappableTarget[] = [
  { id: 'inputLevel', name: 'Input Gain', group: 'Global', kind: 'continuous' },
  { id: 'outputLevel', name: 'Output Level', group: 'Global', kind: 'continuous' },
  { id: 'outputBalance', name: 'Output Balance', group: 'Global', kind: 'continuous' },
  { id: 'gateEnabled', name: 'Gate Power', group: 'Noise Gate', kind: 'toggle' },
  { id: 'gateThreshold', name: 'Gate Threshold', group: 'Noise Gate', kind: 'continuous' },
  { id: 'toneEqEnabled', name: 'Tone Stack Power', group: 'Tone Stack', kind: 'toggle' },
  { id: 'toneBass', name: 'Bass', group: 'Tone Stack', kind: 'continuous' },
  { id: 'toneMid', name: 'Mid', group: 'Tone Stack', kind: 'continuous' },
  { id: 'toneTreble', name: 'Treble', group: 'Tone Stack', kind: 'continuous' },
  { id: 'spreadEnabled', name: 'Spread Power', group: 'Spread', kind: 'toggle' },
  { id: 'spreadAmount', name: 'Spread Amount', group: 'Spread', kind: 'continuous' },
  { id: 'spreadJitter', name: 'Spread Jitter', group: 'Spread', kind: 'continuous' },
  // Virtual target like block powers: stereo on/off is chain state, not an
  // APVTS parameter (the native mapper resolves the id itself).
  { id: 'stereoEnabled', name: 'Stereo Mode', group: 'Stereo', kind: 'toggle' },
  { id: 'chainPanLeft', name: 'Pan L', group: 'Stereo', kind: 'continuous' },
  { id: 'chainPanRight', name: 'Pan R', group: 'Stereo', kind: 'continuous' },
  ...Array.from({ length: BLOCK_POWER_TARGETS }, (_, i): MappableTarget => ({
    id: `block${i + 1}Power`,
    name: `Block ${i + 1} Power`,
    group: 'Chain',
    kind: 'toggle',
  })),
];

export const targetById = new Map(MAPPABLE_TARGETS.map((t) => [t.id, t]));

/** Well-known General MIDI controller names (shown under "CC n"). */
const CC_NAMES: Record<number, string> = {
  1: 'Mod Wheel',
  2: 'Breath',
  4: 'Foot Controller',
  7: 'Volume',
  10: 'Pan',
  11: 'Expression',
  64: 'Sustain',
  65: 'Portamento',
  66: 'Sostenuto',
  67: 'Soft Pedal',
};

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/** 60 → "C4" (scientific pitch, middle C = C4). */
export const midiNoteName = (note: number) =>
  `${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}`;

/** "CC 64" / "Note C2" — the mapping row's source column. */
export const sourceLabel = (mapping: MidiMapping) =>
  mapping.source === 'cc' ? `CC ${mapping.number}` : `Note ${midiNoteName(mapping.number)}`;

/** Subtitle under the source: the CC's common name, if it has one. */
export const sourceSubtitle = (mapping: MidiMapping) =>
  mapping.source === 'cc' ? (CC_NAMES[mapping.number] ?? '') : 'Momentary';

/** Whether the pairing behaves as a toggle (mirrors the native derivation:
    toggle-kind target, or any note source). */
export const isToggleBehavior = (mapping: MidiMapping) =>
  mapping.source === 'note' || targetById.get(mapping.targetId)?.kind === 'toggle';

export const rangeLabel = (mapping: MidiMapping) =>
  isToggleBehavior(mapping) ? 'On / Off' : '0 – 100%';

export const modeLabel = (mapping: MidiMapping) =>
  isToggleBehavior(mapping) ? 'Toggle' : 'Absolute';
