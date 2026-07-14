import type {
  IAudioBackend,
  ParameterType,
  ParameterMap,
  SliderParameter,
  ToggleParameter,
} from '../types/IAudioBackend';

export class MockBackend implements IAudioBackend {
  private parameters = {
    slider: {
      getValue: () => 0,
      setValue: (value: number) => {
        console.log(`Set slider to ${value}`);
      },
      valueChangedEvent: {
        addListener: () => {
          console.log('Added slider listener');
          return 1;
        },
        removeListener: (id: number) => {
          console.log('Removed slider listener', id);
        },
      },
    } as SliderParameter,

    toggle: {
      getValue: () => false,
      setValue: (value: boolean) => {
        console.log(`Set toggle to ${value}`);
      },
      valueChangedEvent: {
        addListener: () => {
          console.log('Added toggle listener');
          return 2;
        },
        removeListener: (id: number) => {
          console.log('Removed toggle listener', id);
        },
      },
    } as ToggleParameter,
  };

  getParameterState<T extends ParameterType>(name: string, type: T): ParameterMap[T] {
    console.log(`Returning mock state for ${name} (${type})`);
    return this.parameters[type];
  }

  getPluginFunction(name: string): (...args: any[]) => Promise<any> {
    return async (...args: any[]) => {
      console.log(`Mock native function '${name}' called with`, args);
      return Promise.resolve(`Mocked result of ${name}`);
    };
  }

  addEventListener(eventId: string): () => void {
    console.log(`Mock addEventListener for '${eventId}'`);
    return () => {};
  }
}
