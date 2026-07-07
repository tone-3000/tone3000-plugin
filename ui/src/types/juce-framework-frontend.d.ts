declare module 'juce-framework-frontend' {
  interface ListenerList {
    addListener(callback: () => void): number;
    removeListener(id: number): void;
  }

  export interface SliderState {
    /** Backend event channel id ("__juce__slider" + name). */
    identifier: string;
    getNormalisedValue(): number;
    setNormalisedValue(value: number): void;
    valueChangedEvent: ListenerList;
    propertiesChangedEvent: ListenerList;
  }

  export interface ToggleState {
    identifier: string;
    getValue(): boolean;
    setValue(value: boolean): void;
    valueChangedEvent: ListenerList;
    propertiesChangedEvent: ListenerList;
  }

  export interface ComboBoxState {
    identifier: string;
    getChoiceIndex(): number;
    setChoiceIndex(index: number): void;
    properties: {
      choices: string[];
    };
    valueChangedEvent: ListenerList;
    propertiesChangedEvent: ListenerList;
  }

  export function getSliderState(name: string): SliderState;
  export function getToggleState(name: string): ToggleState;
  export function getComboBoxState(name: string): ComboBoxState;
  export function getNativeFunction(name: string): (...args: any[]) => Promise<any>;
}
