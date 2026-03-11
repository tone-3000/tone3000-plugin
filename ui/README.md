# TONE3000 Plugin UI (React)

This is the React-based user interface for the TONE3000 plugin, replacing the previous Vue.js implementation.

## Features

- **Simple Design**: Clean, minimal UI without complex assets
- **Full Functionality**: All the same features as the Vue version:
  - NAM model selection
  - IR selection  
  - Tone controls (Bass, Mid, Treble)
  - Toggle controls (EQ, IR, Gate, Normalize options)
  - Level controls (Input, Gate, Output)
- **JUCE Integration**: Seamless integration with the JUCE framework
- **Development Support**: Mock backend for testing

## Development

```bash
# Install dependencies
npm install

# Start development server
npm run dev

# Build for JUCE webview
npm run build

# Build for NPM library
npm run build:npm
```

## Architecture

- **Components**: Simple React components for sliders, toggles, and combo boxes
- **Hooks**: Custom React hooks that replace Vue composables
- **Backend**: Abstract interface for JUCE integration
- **Types**: Full TypeScript support

## Build Outputs

- **JUCE Build**: Outputs to `../plugin/webview/` for JUCE integration
- **NPM Build**: Outputs to `dist/` for library distribution

## Usage

The UI automatically detects whether it's running in JUCE (using JuceBackend) or development (using MockBackend) and provides the appropriate interface.
