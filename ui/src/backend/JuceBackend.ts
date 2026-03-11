import * as Juce from 'juce-framework-frontend';
import type {
  IAudioBackend,
  ParameterType,
  ParameterMap,
  SliderParameter,
  ToggleParameter,
  ComboBoxParameter,
} from '../types/IAudioBackend';

export class JuceBackend implements IAudioBackend {
  getParameterState<T extends ParameterType>(name: string, type: T): ParameterMap[T] {
    const config = juceMap[type];
    if (!config) throw new Error(`Unsupported parameter type: ${type}`);
    return config.adapt(config.get(name));
  }

  getPluginFunction(name: string): (...args: any[]) => Promise<any> {
    return Juce.getNativeFunction(name);
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

function adaptSlider(slider: Juce.SliderState): SliderParameter {
  return {
    getValue: () => slider.getNormalisedValue(),
    setValue: (value: number) => slider.setNormalisedValue(value),
    valueChangedEvent: {
      addListener: (fn: (val: number) => void) =>
        slider.valueChangedEvent.addListener(() => fn(slider.getNormalisedValue())),
      removeListener: (id: number) => slider.valueChangedEvent.removeListener(id),
    },
  };
}

function adaptToggle(toggle: Juce.ToggleState): ToggleParameter {
  return {
    getValue: () => toggle.getValue(),
    setValue: (value: boolean) => toggle.setValue(value),
    valueChangedEvent: {
      addListener: (fn: (val: boolean) => void) =>
        toggle.valueChangedEvent.addListener(() => fn(toggle.getValue())),
      removeListener: (id: number) => toggle.valueChangedEvent.removeListener(id),
    },
  };
}

function adaptComboBox(combo: Juce.ComboBoxState): ComboBoxParameter {
  return {
    getChoiceIndex: () => combo.getChoiceIndex(),
    setChoiceIndex: (index: number) => combo.setChoiceIndex(index),
    getChoices: () => combo.properties.choices,
    valueChangedEvent: {
      addListener: (fn: (val: number) => void) =>
        combo.valueChangedEvent.addListener(() => fn(combo.getChoiceIndex())),
      removeListener: (id: number) => combo.valueChangedEvent.removeListener(id),
    },
  };
}
