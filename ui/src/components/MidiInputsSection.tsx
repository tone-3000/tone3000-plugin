import React, { useEffect } from 'react';
import { Bluetooth } from 'lucide-react';
import type { AudioDevice } from '../hooks/useAudioDevice';
import { ChoiceIndicator, FieldRow, FIELD_BORDER, outlinedFieldStyle } from './controls';
import { MUTED, SUBTLE } from './theme';

/**
 * MIDI device enablement for the System Settings tab (standalone only —
 * hosts route MIDI to the processor themselves, and opening devices from a
 * plugin would fight the host and double-trigger events).
 *
 * This section only decides which hardware feeds the plugin: enabled inputs
 * are merged into one stream. What each knob or pedal does is MIDI Mapping,
 * which lives in Plugin Settings because it works in DAW builds too.
 */

/** Hot-plug detection: the OS doesn't push MIDI device arrivals, so re-pull
    the device state (which re-enumerates) while this section is visible. */
const HOTPLUG_POLL_MS = 2000;

export const MidiInputsSection: React.FC<{ device: AudioDevice }> = ({ device }) => {
  const { state, refresh, actions } = device;

  useEffect(() => {
    const interval = setInterval(() => refresh(), HOTPLUG_POLL_MS);
    return () => clearInterval(interval);
  }, [refresh]);

  if (!state) return null;
  const inputs = state.midiInputs ?? [];

  return (
    <FieldRow
      label="MIDI Inputs"
      help="Enable the devices you want to control the plugin with. Set what each knob or pedal does in Plugin Settings under MIDI Mapping."
    >
      <div style={{ border: FIELD_BORDER, borderRadius: '10px', overflow: 'hidden' }}>
        {inputs.length === 0 ? (
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
            No MIDI devices found. Connect one and it will appear here.
          </p>
        ) : (
          inputs.map((input) => (
            <button
              key={input.id}
              role="checkbox"
              aria-checked={input.enabled}
              onClick={() => actions.setMidiInputEnabled(input.id, !input.enabled)}
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '12px',
                width: '100%',
                padding: '11px 13px',
                border: 'none',
                background: 'transparent',
                cursor: 'pointer',
                textAlign: 'left',
                color: 'inherit',
              }}
              onMouseEnter={(e) => (e.currentTarget.style.background = 'rgba(255, 255, 255, 0.06)')}
              onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
            >
              <ChoiceIndicator selected={input.enabled} square />
              <span
                style={{
                  flex: 1,
                  minWidth: 0,
                  fontSize: '14px',
                  fontWeight: 400,
                  color: input.enabled ? '#ffffff' : MUTED,
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap',
                }}
              >
                {input.name}
              </span>
              {input.enabled && (
                <span style={{ fontSize: '11px', fontWeight: 400, color: SUBTLE, flexShrink: 0 }}>
                  enabled
                </span>
              )}
            </button>
          ))
        )}
      </div>
      {state.btMidiAvailable && (
        <button
          onClick={() => actions.openBluetoothMidiPairing()}
          style={{
            ...outlinedFieldStyle,
            width: '100%',
            marginTop: '10px',
            padding: '12px 16px',
            cursor: 'pointer',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: '8px',
            fontWeight: 600,
            fontSize: '13px',
          }}
        >
          <Bluetooth size={14} />
          Bluetooth MIDI…
        </button>
      )}
    </FieldRow>
  );
};
