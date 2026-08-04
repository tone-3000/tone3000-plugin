/**
 * Snapshot of the standalone app's audio device state, produced by the
 * native StandaloneAudioSettings controller (getAudioDeviceState). The UI
 * treats this as read-only truth: every field is post-open readback. A
 * requested rate/buffer is a wish; these values are what the device actually
 * runs at. Never available in hosted builds (the DAW owns devices).
 */
export interface AudioDeviceState {
  /** All registered driver types; a driver picker renders only when > 1
      (macOS has a single CoreAudio type; never show a one-option select). */
  deviceTypes: string[];
  currentType: string;
  /** False only for ASIO-style backends where one driver owns both
      directions; those render a single device picker. */
  separateIO: boolean;
  inputDevices: string[];
  outputDevices: string[];
  /** Currently selected device names ('' = no device, a real option). */
  inputDevice: string;
  outputDevice: string;
  /** True when a device is actually open and running. */
  deviceOpen: boolean;
  inputChannels: AudioInputChannel[];
  /** Stereo-pair labels for multi-out interfaces (empty when the device has
      2 or fewer outputs and no picker is needed). */
  outputPairs: string[];
  /** Index into outputPairs of the active pair; -1 when not applicable. */
  activeOutputPair: number;
  sampleRates: number[];
  sampleRate: number;
  bufferSizes: number[];
  bufferSize: number;
  /** Vendor control panel exists (ASIO); buffer/clock may live there. */
  hasControlPanel: boolean;
  /** Output monitoring (inverse of the standalone holder's input mute). */
  hearYourself: boolean;
  /** Built-in mic feeding speakers; monitoring would squeal. */
  feedbackRisk: boolean;
  /** Windows only: an ASIO driver reports devices while another type is
      current (drives the "lower latency available" nudge). */
  asioAvailable: boolean;
  /** OS microphone gate. 'denied' means the OS is blocking audio input (macOS
      privacy), so input is silent until re-enabled and the app relaunched.
      'granted' on platforms without a queryable per-app mic gate. */
  micPermission: 'granted' | 'denied' | 'unknown';
  /** MIDI hardware, re-enumerated per pull (polling while the settings tab is
      open doubles as hot-plug detection). Enabled inputs are merged into one
      stream feeding the plugin; what each control does is the MIDI Mapping
      tab's business (see MidiMapState). */
  midiInputs: MidiInputDevice[];
  /** OS Bluetooth MIDI pairing dialog exists (macOS). */
  btMidiAvailable: boolean;
}

export interface MidiInputDevice {
  /** OS device identifier (stable key for enable/disable). */
  id: string;
  name: string;
  enabled: boolean;
}

export interface AudioInputChannel {
  /** Device channel index (stable key for selection + metering). */
  index: number;
  name: string;
  active: boolean;
}

/** Result shape of every audio settings mutation. */
export interface AudioDeviceResult {
  ok: boolean;
  error: string;
}
