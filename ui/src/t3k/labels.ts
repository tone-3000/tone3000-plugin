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
