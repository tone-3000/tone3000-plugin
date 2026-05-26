import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import cssInjectedByJsPlugin from 'vite-plugin-css-injected-by-js'

const isLib = process.env.BUILD_TARGET === 'lib'

export default defineConfig({
  plugins: [
    react(),
    // Only use CSS injection for library builds
    ...(isLib ? [cssInjectedByJsPlugin()] : []),
  ],
  build: isLib
    // Build NPM library
    ? {
        outDir: 'dist',
        emptyOutDir: true,
        lib: {
          entry: new URL('./src/index.ts', import.meta.url).pathname,
          name: 'PluginUI',
          fileName: (format) => `plugin-ui.${format}.js`
        },
        rollupOptions: {
          external: ['react', 'react-dom', 'juce-framework-frontend'],
          output: {
            globals: {
              react: 'React',
              'react-dom': 'ReactDOM',
              'juce-framework-frontend': 'JuceFrameworkFrontend'
            }
          }
        }
      }
    // Build JUCE Webview static UI
    : {
        outDir: '../plugin/webview',
        emptyOutDir: true,
        assetsDir: 'assets',
        rollupOptions: {
          input: {
            main: './index.html'
          },
          output: {
            format: 'es',
            entryFileNames: '[name].js',
            // Extract CSS to separate files
            assetFileNames: 'assets/[name]-[hash][extname]'
          }
        },
        // Extract CSS to separate files instead of injecting
        cssCodeSplit: true
      },
  base: './'
})
