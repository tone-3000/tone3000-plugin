import { useEffect, useCallback, useRef } from 'react';
import type { Tone } from '../types/tone';

interface UseT3kSelectOptions {
  appId: string;
  onToneSelected?: (tone: Tone) => void;
}

export const useT3kSelect = ({ appId, onToneSelected }: UseT3kSelectOptions) => {
  const processingRef = useRef(false);

  // Handle the callback when user returns from TONE3000 select page (web browser only)
  useEffect(() => {
    const handleCallback = async () => {
      // Prevent duplicate processing
      if (processingRef.current) {
        return;
      }

      const urlParams = new URLSearchParams(window.location.search);
      const toneUrl = urlParams.get('tone_url');

      if (!toneUrl) {
        return;
      }

      processingRef.current = true;

      try {
        console.log('Fetching tone from URL:', toneUrl);

        const response = await fetch(toneUrl);
        if (!response.ok) {
          throw new Error(`Failed to fetch tone: ${response.statusText}`);
        }

        const tone: Tone = await response.json();
        console.log('Tone fetched successfully:', tone.title);

        // Clean up URL by removing the tone_url parameter
        const newUrl = new URL(window.location.href);
        newUrl.searchParams.delete('tone_url');
        window.history.replaceState({}, '', newUrl.toString());

        // Call the callback with the tone data
        if (onToneSelected) {
          onToneSelected(tone);
        }
      } catch (error) {
        console.error('Error loading tone from Select callback:', error);
      } finally {
        processingRef.current = false;
      }
    };

    handleCallback();
  }, [onToneSelected]);

  // Function to initiate the Select flow (web browser only now)
  const startSelectFlow = useCallback(() => {
    const redirectUrl = encodeURIComponent(window.location.href);
    const selectUrl = `https://www.tone3000.com/api/v1/select?app_id=${encodeURIComponent(appId)}&redirect_url=${redirectUrl}`;

    console.log('Starting TONE3000 Select flow:', selectUrl);
    window.location.href = selectUrl;
  }, [appId]);

  return {
    startSelectFlow,
  };
};
