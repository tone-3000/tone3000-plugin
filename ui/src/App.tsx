import { Plugin } from './components/Plugin';
import { AudioBackendContext } from './hooks/useAudioBackend';
import { JuceBackend } from './backend/JuceBackend';
import { MetersProvider } from './hooks/useMeters';
import { ErrorBoundary } from './components/ErrorBoundary';

function App() {
  // Create the JUCE backend - this should work for both development and JUCE
  // The JuceBackend will use the JUCE framework functions when available
  const backend = new JuceBackend();

  return (
    <ErrorBoundary>
      <AudioBackendContext.Provider value={backend}>
        {/* Single shared meter poll loop for every meter in the UI. */}
        <MetersProvider>
          <Plugin />
        </MetersProvider>
      </AudioBackendContext.Provider>
    </ErrorBoundary>
  );
}

export default App;
