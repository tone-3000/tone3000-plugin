import React from 'react';
import { SelectView } from './components/SelectView';
import { useFunction } from './hooks/useFunction';

// This is the entry point for the select webview
export const SelectApp: React.FC = () => {
  const sendMessageToMainView = useFunction<string>('sendMessageToMainView');

  const handleToneSelected = async (toneUrl: string) => {
    console.log('SelectApp: Sending tone URL to main view');
    try {
      await sendMessageToMainView.invoke('tone3000.toneSelected', toneUrl);
      console.log('SelectApp: Message sent successfully');
    } catch (error) {
      console.error('SelectApp: Failed to send message', error);
    }
  };

  return <SelectView onToneSelected={handleToneSelected} />;
};
