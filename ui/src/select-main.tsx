import React from 'react';
import ReactDOM from 'react-dom/client';
import { SelectApp } from './SelectApp';
import { AudioBackendContext } from './hooks/useAudioBackend';
import { JuceBackend } from './backend/JuceBackend';
import './index.css';

// Create the JUCE backend - same as main app
const backend = new JuceBackend();

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <AudioBackendContext.Provider value={backend}>
      <SelectApp />
    </AudioBackendContext.Provider>
  </React.StrictMode>
);
