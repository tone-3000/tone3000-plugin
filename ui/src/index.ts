// Export main component
export { Plugin } from './components/Plugin';

// Export Audio Backend interface
export type * from './types/IAudioBackend';

// Export Mock Backend (For implementation testing)
export { MockBackend } from './backend/MockBackend';

// Export Juce Backend
export { JuceBackend } from './backend/JuceBackend';

// Export hooks
export { useAudioBackend, AudioBackendContext } from './hooks/useAudioBackend';
export { useParameter } from './hooks/useParameter';
export { useComboBoxParameter } from './hooks/useComboBoxParameter';
export { useFunction } from './hooks/useFunction';
