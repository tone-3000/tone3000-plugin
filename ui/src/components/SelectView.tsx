import React, { useEffect } from 'react';

interface SelectViewProps {
  onToneSelected: (toneUrl: string) => void;
}

export const SelectView: React.FC<SelectViewProps> = ({ onToneSelected }) => {
  useEffect(() => {
    handleSelectFlow();
  }, []);

  const handleSelectFlow = () => {
    console.log('=== Select View: Starting ===');

    const urlParams = new URLSearchParams(window.location.search);
    const toneUrl = urlParams.get('tone_url');

    if (!toneUrl) {
      console.log('No tone_url found, redirecting to TONE3000...');

      // Redirect to TONE3000 Select
      const appId = 'TONE3000-Plugin';
      const redirectUrl = encodeURIComponent(window.location.href);
      const selectUrl = `https://www.tone3000.com/api/v1/select?app_id=${encodeURIComponent(appId)}&redirect_url=${redirectUrl}`;
      window.location.href = selectUrl;

      return;
    }

    console.log('✓ Tone URL received:', toneUrl);

    // Clean up URL
    const newUrl = new URL(window.location.href);
    newUrl.searchParams.delete('tone_url');
    window.history.replaceState({}, '', newUrl.toString());

    // Send tone URL to main view - let it handle fetching and loading
    onToneSelected(toneUrl);
  };

  return (
    <div
      style={{
        width: '100%',
        height: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        backgroundColor: '#000',
        color: '#fff',
      }}
    ></div>
  );
};
