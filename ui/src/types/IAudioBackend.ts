export type ParameterType = 'slider' | 'toggle' | 'comboBox';

export type ParameterMap = {
  slider: SliderParameter;
  toggle: ToggleParameter;
  comboBox: ComboBoxParameter;
};

export type ParameterValueType = {
  slider: number;
  toggle: boolean;
  /** Choice index into the parameter's option list. */
  comboBox: number;
};

export interface IAudioBackend {
  getParameterState<T extends ParameterType>(name: string, type: T): ParameterMap[T];
  getPluginFunction(name: string): (...args: unknown[]) => Promise<unknown>;
  /**
   * Subscribe to a native-emitted event (WebBrowserComponent::emitEvent…).
   * Returns an unsubscribe function. Used for push-style updates (e.g.
   * `chainChanged`) so the UI doesn't have to fast-poll.
   */
  addEventListener(eventId: string, fn: (payload: unknown) => void): () => void;
}

export interface ValueChangeEvent<T> {
  addListener(fn: (value: T) => void): number;
  removeListener(id: number): void;
}

export interface SliderParameter {
  getValue(): number;
  setValue(value: number): void;
  valueChangedEvent?: ValueChangeEvent<number>;
  /** Ask the backend to re-send the current value/properties (safe to call any time). */
  requestInitialUpdate?(): void;
}

export interface ToggleParameter {
  getValue(): boolean;
  setValue(value: boolean): void;
  valueChangedEvent?: ValueChangeEvent<boolean>;
  requestInitialUpdate?(): void;
}

export interface ComboBoxParameter {
  /** Current choice index. */
  getValue(): number;
  setValue(index: number): void;
  valueChangedEvent?: ValueChangeEvent<number>;
  requestInitialUpdate?(): void;
}
