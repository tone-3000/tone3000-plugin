import { useState, useEffect, useCallback } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { ParameterType, ParameterMap, ParameterValueType } from '../types/IAudioBackend';

type Parameter<T extends ParameterType> = ParameterMap[T];

export function useParameter<T extends ParameterType>(
  identifier: string,
  type: T
): [ParameterValueType[T], (value: ParameterValueType[T]) => void] {
  const backend = useAudioBackend();
  const param = backend.getParameterState(identifier, type) as Parameter<T>;
  const [value, setValue] = useState<ParameterValueType[T]>(() => {
    switch (type) {
      case 'slider': {
        const p = param as ParameterMap['slider'];
        return p.getValue() as ParameterValueType[T];
      }
      case 'toggle': {
        const p = param as ParameterMap['toggle'];
        return p.getValue() as ParameterValueType[T];
      }
      case 'comboBox': {
        const p = param as ParameterMap['comboBox'];
        return p.getChoiceIndex() as ParameterValueType[T];
      }
      default:
        throw new Error(`Unsupported parameter type: ${type}`);
    }
  });

  const updateValue = useCallback(
    (newValue: ParameterValueType[T]) => {
      // Only update if the value actually changed
      if (newValue === value) return;

      setValue(newValue);

      // Immediately call the backend, like the Vue version does
      switch (type) {
        case 'slider': {
          const p = param as ParameterMap['slider'];
          p.setValue(newValue as ParameterValueType['slider']);
          break;
        }
        case 'toggle': {
          const p = param as ParameterMap['toggle'];
          p.setValue(newValue as ParameterValueType['toggle']);
          break;
        }
        case 'comboBox': {
          const p = param as ParameterMap['comboBox'];
          p.setChoiceIndex(newValue as ParameterValueType['comboBox']);
          break;
        }
      }
    },
    [param, type, value]
  );

  useEffect(() => {
    let listenerId: number | undefined;

    if (param.valueChangedEvent) {
      listenerId = param.valueChangedEvent.addListener((newValue) => {
        setValue(newValue as ParameterValueType[T]);
      });
    }

    return () => {
      if (listenerId !== undefined && param.valueChangedEvent) {
        param.valueChangedEvent.removeListener(listenerId);
      }
    };
  }, [param]);

  return [value, updateValue];
}
