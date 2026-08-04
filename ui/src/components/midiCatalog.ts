import type { MidiMapping } from '../types/midiMap';

/**
 * Display catalog for the MIDI mapping UI: which targets are mappable and
 * how to present them. The native engine accepts any APVTS parameter id (or
 * block-power id); this list is the UI's curation (setup-domain params like
 * calibration stay out; they describe the rig, not something you perform
 * with).
 */

export interface MappableTarget {
  /** APVTS parameter id, positional block power ("block1Power"), or a
      virtual action id ("presetNext"). */
  id: string;
  name: string;
  /** Section subtitle, mirroring the faceplate's layout. */
  group: string;
  /** Drives the behavior display: continuous knobs are absolute via CC,
      toggles flip on/off, triggers fire an action once per press. */
  kind: 'continuous' | 'toggle' | 'trigger';
}

/** Block-power targets are positional ("Block 1" is a lane's first tone
    block whatever it currently holds), so a mapping survives tone swaps and
    preset loads, like switches on a pedalboard. "blockNPower" is the Left
    lane (the only lane in mono), "rightBlockNPower" the Right lane (stereo
    only; the mapping screen offers them per the live chain). Display-only
    cap (the engine takes up to 64): enough for any realistic pedalboard
    without burying the picker. */
const BLOCK_POWER_TARGETS = 12;

export const MAPPABLE_TARGETS: MappableTarget[] = [
  // Virtual actions (native resolves the ids itself): step through the
  // preset list in browser order, wrapping at the ends, for footswitches
  // programmed with CC / note buttons instead of program changes.
  { id: 'presetPrevious', name: 'Previous Preset', group: 'Presets', kind: 'trigger' },
  { id: 'presetNext', name: 'Next Preset', group: 'Presets', kind: 'trigger' },
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
  { id: 'spreadOffset', name: 'Spread Offset', group: 'Spread', kind: 'continuous' },
  { id: 'spreadWobble', name: 'Spread Wobble', group: 'Spread', kind: 'continuous' },
  // Virtual target like block powers: stereo on/off is chain state, not an
  // APVTS parameter (the native mapper resolves the id itself).
  { id: 'stereoEnabled', name: 'Stereo Mode', group: 'Stereo', kind: 'toggle' },
  { id: 'stereoOffsetEnabled', name: 'Offset Power', group: 'Stereo', kind: 'toggle' },
  { id: 'stereoOffsetTime', name: 'Offset', group: 'Stereo', kind: 'continuous' },
  { id: 'chainPanLeft', name: 'Pan L', group: 'Stereo', kind: 'continuous' },
  { id: 'chainPanRight', name: 'Pan R', group: 'Stereo', kind: 'continuous' },
  ...Array.from({ length: BLOCK_POWER_TARGETS }, (_, i): MappableTarget => ({
    id: `block${i + 1}Power`,
    name: `Block ${i + 1} Power`,
    group: 'Chain',
    kind: 'toggle',
  })),
  ...Array.from({ length: BLOCK_POWER_TARGETS }, (_, i): MappableTarget => ({
    id: `rightBlock${i + 1}Power`,
    name: `Block ${i + 1} Power`,
    group: 'Chain R',
    kind: 'toggle',
  })),
];

export const targetById = new Map(MAPPABLE_TARGETS.map((t) => [t.id, t]));

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/** 60 → "C4" (scientific pitch, middle C = C4). */
export const midiNoteName = (note: number) =>
  `${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}`;

/** "CC 64" / "Note C2": the mapping row's source column. */
export const sourceLabel = (mapping: MidiMapping) =>
  mapping.source === 'cc' ? `CC ${mapping.number}` : `Note ${midiNoteName(mapping.number)}`;

/** How the pairing behaves, mirroring the native derivation: trigger
    targets fire per press, toggle targets (and any note source) flip
    on/off, everything else tracks the CC value absolutely. */
export const behaviorLabel = (mapping: MidiMapping) => {
  const kind = targetById.get(mapping.targetId)?.kind;
  if (kind === 'trigger') return 'Trigger';
  if (kind === 'toggle' || mapping.source === 'note') return 'Toggle';
  return 'Absolute';
};
