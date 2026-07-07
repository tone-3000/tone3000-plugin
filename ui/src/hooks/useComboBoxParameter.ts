import { useState, useEffect, useCallback } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { ComboBoxParameter } from '../types/IAudioBackend';

export function useComboBoxParameter(identifier: string) {
  const backend = useAudioBackend();
  const param = backend.getParameterState(identifier, 'comboBox') as ComboBoxParameter;

  const [value, setValue] = useState(param.getChoiceIndex());
  const [choices] = useState<string[]>(param.getChoices());

  const updateValue = useCallback(
    (newValue: number) => {
      setValue(newValue);
      param.setChoiceIndex(newValue);
    },
    [param]
  );

  useEffect(() => {
    let listenerId: number | undefined;

    if (param.valueChangedEvent) {
      listenerId = param.valueChangedEvent.addListener((newValue) => {
        setValue(newValue);
      });
    }

    // Re-sync after subscribing — the backend's one-shot initial update can
    // arrive before this listener exists (see useParameter for details).
    setValue(param.getChoiceIndex());
    param.requestInitialUpdate?.();

    return () => {
      if (listenerId !== undefined && param.valueChangedEvent) {
        param.valueChangedEvent.removeListener(listenerId);
      }
    };
  }, [param]);

  return {
    value,
    choices,
    setValue: updateValue,
  };
}
