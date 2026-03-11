export type ParameterType = 'slider' | 'toggle' | 'comboBox';

export type ParameterMap = {
  slider: SliderParameter;
  toggle: ToggleParameter;
  comboBox: ComboBoxParameter;
};

export type ParameterValueType = {
  slider: number;
  toggle: boolean;
  comboBox: number;
};

export interface IAudioBackend {
  getParameterState<T extends ParameterType>(name: string, type: T): ParameterMap[T];
  getPluginFunction(name: string): (...args: any[]) => Promise<any>;
}

export interface ValueChangeEvent<T> {
  addListener(fn: (value: T) => void): number;
  removeListener(id: number): void;
}

export interface SliderParameter {
  getValue(): number;
  setValue(value: number): void;
  valueChangedEvent?: ValueChangeEvent<number>;
}

export interface ToggleParameter {
  getValue(): boolean;
  setValue(value: boolean): void;
  valueChangedEvent?: ValueChangeEvent<boolean>;
}

export interface ComboBoxParameter {
  getChoiceIndex(): number;
  setChoiceIndex(index: number): void;
  getChoices(): string[];
  valueChangedEvent?: ValueChangeEvent<number>;
}
