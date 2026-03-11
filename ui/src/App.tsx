import { Plugin } from './components/Plugin';
import { AudioBackendContext } from './hooks/useAudioBackend';
import { JuceBackend } from './backend/JuceBackend';

function App() {
  // Create the JUCE backend - this should work for both development and JUCE
  // The JuceBackend will use the JUCE framework functions when available
  const backend = new JuceBackend();

  return (
    <AudioBackendContext.Provider value={backend}>
      <Plugin />
    </AudioBackendContext.Provider>
  );
}

export default App;
