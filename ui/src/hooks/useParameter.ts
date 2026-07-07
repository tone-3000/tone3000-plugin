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

    // Close the initial-sync race: the backend's reply to the frontend's
    // startup `requestInitialUpdate` can land before this listener exists (or
    // be dropped entirely while the page is still loading — frequent on
    // Windows WebView2), leaving the knob at its default. Re-read whatever
    // state already arrived, then ask the backend to send it again now that
    // we're subscribed.
    const readCurrent = (): ParameterValueType[T] => {
      switch (type) {
        case 'toggle':
          return (param as ParameterMap['toggle']).getValue() as ParameterValueType[T];
        case 'comboBox':
          return (param as ParameterMap['comboBox']).getChoiceIndex() as ParameterValueType[T];
        default:
          return (param as ParameterMap['slider']).getValue() as ParameterValueType[T];
      }
    };
    setValue(readCurrent());
    param.requestInitialUpdate?.();

    return () => {
      if (listenerId !== undefined && param.valueChangedEvent) {
        param.valueChangedEvent.removeListener(listenerId);
      }
    };
  }, [param, type]);

  return [value, updateValue];
}
