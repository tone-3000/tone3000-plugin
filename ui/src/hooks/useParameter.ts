import { useState, useEffect, useCallback } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { ParameterType, ParameterMap, ParameterValueType } from '../types/IAudioBackend';

type Parameter<T extends ParameterType> = ParameterMap[T];

function readCurrent<T extends ParameterType>(param: Parameter<T>, type: T): ParameterValueType[T] {
  if (type === 'toggle')
    return (param as ParameterMap['toggle']).getValue() as ParameterValueType[T];
  return (param as ParameterMap['slider']).getValue() as ParameterValueType[T];
}

export function useParameter<T extends ParameterType>(
  identifier: string,
  type: T
): [ParameterValueType[T], (value: ParameterValueType[T]) => void] {
  const backend = useAudioBackend();
  const param = backend.getParameterState(identifier, type) as Parameter<T>;
  const [value, setValue] = useState<ParameterValueType[T]>(() => readCurrent(param, type));

  const updateValue = useCallback(
    (newValue: ParameterValueType[T]) => {
      // Only update if the value actually changed
      if (newValue === value) return;

      setValue(newValue);

      if (type === 'toggle') {
        (param as ParameterMap['toggle']).setValue(newValue as ParameterValueType['toggle']);
      } else {
        (param as ParameterMap['slider']).setValue(newValue as ParameterValueType['slider']);
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
    setValue(readCurrent(param, type));
    param.requestInitialUpdate?.();

    return () => {
      if (listenerId !== undefined && param.valueChangedEvent) {
        param.valueChangedEvent.removeListener(listenerId);
      }
    };
  }, [param, type]);

  return [value, updateValue];
}
