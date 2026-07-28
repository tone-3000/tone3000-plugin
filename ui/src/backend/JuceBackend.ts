import * as Juce from 'juce-framework-frontend';
import type {
  IAudioBackend,
  ParameterType,
  ParameterMap,
  SliderParameter,
  ToggleParameter,
  ComboBoxParameter,
} from '../types/IAudioBackend';

/** The raw event bus JUCE injects at `window.__JUCE__.backend`. */
type RawJuceBackend = {
  addEventListener: (eventId: string, fn: (payload: unknown) => void) => [string, number];
  removeEventListener: (token: [string, number]) => void;
  emitEvent: (eventId: string, payload: unknown) => void;
};

function rawJuceBackend(): RawJuceBackend | undefined {
  return (window as unknown as { __JUCE__?: { backend?: RawJuceBackend } }).__JUCE__?.backend;
}

export class JuceBackend implements IAudioBackend {
  getParameterState<T extends ParameterType>(name: string, type: T): ParameterMap[T] {
    const config = juceMap[type];
    if (!config) throw new Error(`Unsupported parameter type: ${type}`);
    return config.adapt(config.get(name));
  }

  getPluginFunction(name: string): (...args: any[]) => Promise<any> {
    return Juce.getNativeFunction(name);
  }

  addEventListener(eventId: string, fn: (payload: unknown) => void): () => void {
    const backend = rawJuceBackend();
    if (!backend) return () => {};
    // The raw JUCE backend returns a [eventId, id] token for removal.
    const token = backend.addEventListener(eventId, fn);
    return () => backend.removeEventListener(token);
  }
}

type JuceGetterMap = {
  [K in ParameterType]: {
    get: (name: string) => any;
    adapt: (raw: any) => ParameterMap[K];
  };
};

const juceMap: JuceGetterMap = {
  slider: {
    get: Juce.getSliderState,
    adapt: adaptSlider,
  },
  toggle: {
    get: Juce.getToggleState,
    adapt: adaptToggle,
  },
  comboBox: {
    get: Juce.getComboBoxState,
    adapt: adaptComboBox,
  },
};

// Ask the backend relay to (re-)send propertiesChanged + valueChanged for a
// state. The JUCE frontend emits this once at module load, but that single shot
// races page load / React mount (notably on Windows WebView2) and the reply is
// dropped if it lands before a listener is attached — so consumers re-request
// after subscribing.
function requestInitialUpdate(state: { identifier: string }): void {
  rawJuceBackend()?.emitEvent(state.identifier, { eventType: 'requestInitialUpdate' });
}

type JuceControlState = Juce.SliderState | Juce.ToggleState | Juce.ComboBoxState;

// Subscribe to both valueChanged and propertiesChanged: for sliders the
// normalised value is derived from the range properties, and the two events
// arrive separately (properties first) during the initial update.
function addControlListener(state: JuceControlState, fn: () => void): number {
  const valueId = state.valueChangedEvent.addListener(fn);
  const propsId = state.propertiesChangedEvent.addListener(fn);
  propsListenerIds.set(listenerKey(state, valueId), propsId);
  return valueId;
}

function removeControlListener(state: JuceControlState, valueId: number): void {
  state.valueChangedEvent.removeListener(valueId);
  const key = listenerKey(state, valueId);
  const propsId = propsListenerIds.get(key);
  if (propsId !== undefined) {
    state.propertiesChangedEvent.removeListener(propsId);
    propsListenerIds.delete(key);
  }
}

const propsListenerIds = new Map<string, number>();
const listenerKey = (state: JuceControlState, valueId: number) => `${state.identifier}#${valueId}`;

function adaptSlider(slider: Juce.SliderState): SliderParameter {
  return {
    getValue: () => slider.getNormalisedValue(),
    setValue: (value: number) => slider.setNormalisedValue(value),
    valueChangedEvent: {
      addListener: (fn: (val: number) => void) =>
        addControlListener(slider, () => fn(slider.getNormalisedValue())),
      removeListener: (id: number) => removeControlListener(slider, id),
    },
    requestInitialUpdate: () => requestInitialUpdate(slider),
  };
}

function adaptToggle(toggle: Juce.ToggleState): ToggleParameter {
  return {
    getValue: () => toggle.getValue(),
    setValue: (value: boolean) => toggle.setValue(value),
    valueChangedEvent: {
      addListener: (fn: (val: boolean) => void) =>
        addControlListener(toggle, () => fn(toggle.getValue())),
      removeListener: (id: number) => removeControlListener(toggle, id),
    },
    requestInitialUpdate: () => requestInitialUpdate(toggle),
  };
}

function adaptComboBox(comboBox: Juce.ComboBoxState): ComboBoxParameter {
  return {
    getValue: () => comboBox.getChoiceIndex(),
    setValue: (index: number) => comboBox.setChoiceIndex(index),
    valueChangedEvent: {
      addListener: (fn: (val: number) => void) =>
        addControlListener(comboBox, () => fn(comboBox.getChoiceIndex())),
      removeListener: (id: number) => removeControlListener(comboBox, id),
    },
    requestInitialUpdate: () => requestInitialUpdate(comboBox),
  };
}
