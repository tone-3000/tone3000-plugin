import { useContext, createContext } from 'react';
import type { IAudioBackend } from '../types/IAudioBackend';

const AudioBackendContext = createContext<IAudioBackend | null>(null);

export function useAudioBackend(): IAudioBackend {
  const backend = useContext(AudioBackendContext);
  if (!backend) {
    throw new Error('No audio backend provided');
  }
  return backend;
}

export { AudioBackendContext };
