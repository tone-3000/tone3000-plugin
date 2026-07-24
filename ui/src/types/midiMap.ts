/**
 * Snapshot of the processor's MIDI mapping engine (getMidiMapState). Unlike
 * the audio device state this exists in every build — hosts deliver MIDI to
 * the processor just like the standalone's enabled inputs do — so the
 * mapping UI renders everywhere.
 *
 * Program changes need no mapping: PC n always loads the nth preset in list
 * order (gated by the same channel filter).
 */
export interface MidiMapState {
  /** Global channel filter: 0 = omni, 1–16 = that channel only. */
  channel: number;
  /** Target currently armed for learn ('' when idle). */
  learnTargetId: string;
  mappings: MidiMapping[];
}

export type MidiSource = 'cc' | 'note';

/** One learned assignment. A target is an APVTS parameter id or a positional
    block power ("block1Power" = the chain's first tone block). Behavior
    (absolute vs toggle) is derived from the target kind + source, not stored
    — see the native MidiMapper. */
export interface MidiMapping {
  targetId: string;
  source: MidiSource;
  number: number;
}
