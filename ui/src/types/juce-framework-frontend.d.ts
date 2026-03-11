declare module 'juce-framework-frontend' {
  export interface SliderState {
    getNormalisedValue(): number;
    setNormalisedValue(value: number): void;
    valueChangedEvent: {
      addListener(callback: () => void): number;
      removeListener(id: number): void;
    };
  }

  export interface ToggleState {
    getValue(): boolean;
    setValue(value: boolean): void;
    valueChangedEvent: {
      addListener(callback: () => void): number;
      removeListener(id: number): void;
    };
  }

  export interface ComboBoxState {
    getChoiceIndex(): number;
    setChoiceIndex(index: number): void;
    properties: {
      choices: string[];
    };
    valueChangedEvent: {
      addListener(callback: () => void): number;
      removeListener(id: number): void;
    };
  }

  export function getSliderState(name: string): SliderState;
  export function getToggleState(name: string): ToggleState;
  export function getComboBoxState(name: string): ComboBoxState;
  export function getNativeFunction(name: string): (...args: any[]) => Promise<any>;
}
