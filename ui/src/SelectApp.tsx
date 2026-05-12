import React from 'react';
import { SelectView } from './components/SelectView';
import { useFunction } from './hooks/useFunction';
import type { T3KTokens } from './t3k/tone3000-client';

/**
 * Entry point for the popup webview that JUCE opens when the user clicks
 * the + icon. The popup's only job is to drive the TONE3000 OAuth Select
 * flow and hand the resulting (tokens + tone_id) back to the main webview
 * across the JUCE bridge — the main webview owns all chain state and does
 * the actual tone/model fetches with the access token it receives.
 */
export const SelectApp: React.FC = () => {
  const sendMessageToMainView = useFunction<string>('sendMessageToMainView');

  const handleSelectComplete = async (payload: {
    tokens: T3KTokens;
    toneId: string;
  }) => {
    try {
      await sendMessageToMainView.invoke('tone3000.toneSelected', payload);
    } catch (error) {
      console.error('SelectApp: failed to relay selection to main view', error);
    }
  };

  const handleSelectCancelled = async (reason: string) => {
    try {
      await sendMessageToMainView.invoke('tone3000.cancelled', reason);
    } catch (error) {
      console.error('SelectApp: failed to relay cancellation to main view', error);
    }
  };

  return (
    <SelectView
      onSelectComplete={handleSelectComplete}
      onSelectCancelled={handleSelectCancelled}
    />
  );
};
