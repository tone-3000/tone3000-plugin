/** Display labels for TONE3000 gear types and model formats, shared by the
    tone browser rows and the chain's expanded block cards. */

/** Mirrors the web's GEAR_CAPTURE_SMALL_MAP (compact card labels). */
export const GEAR_LABELS: Record<string, string> = {
  amp: 'Amp Head',
  'amp-cab': 'Amp + Cab',
  'full-rig': 'Amp + Cab',
  pedal: 'Pedal',
  outboard: 'Outboard',
  cab: 'Cabinet',
  space: 'Space',
  experimental: 'Experimental',
  ir: 'IR',
};

export const FORMAT_LABELS: Record<string, string> = {
  nam: 'NAM',
  ir: 'IR',
  'aida-x': 'AIDA-X',
  'aa-snapshot': 'Snapshot',
  proteus: 'Proteus',
};

export const gearLabel = (gear?: string): string =>
  gear ? (GEAR_LABELS[gear.toLowerCase()] ?? gear) : '';

export const formatLabel = (format?: string): string =>
  format ? (FORMAT_LABELS[format.toLowerCase()] ?? format.toUpperCase()) : '';

/**
 * Gear-type filter chips for the tone browser streams (Trending / Recently
 * used / Favorites / Created), one radio-select pill per `Gear` enum value.
 * Order and copy mirror the web's filter row; `space` reads "Spaces" here
 * (matches the icon's own aria-label) even though card meta text uses the
 * singular "Space" from GEAR_LABELS above.
 */
export const GEAR_FILTERS: { id: string; label: string }[] = [
  { id: 'amp-cab', label: 'Amp + Cab' },
  { id: 'amp', label: 'Amp Head' },
  { id: 'cab', label: 'Cabinet' },
  { id: 'pedal', label: 'Pedal' },
  { id: 'outboard', label: 'Outboard' },
  { id: 'space', label: 'Spaces' },
  { id: 'experimental', label: 'Experimental' },
];
