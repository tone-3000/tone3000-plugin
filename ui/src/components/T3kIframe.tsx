import { useEffect, useRef } from 'react';
import type { Tone, T3kDownloadEvent } from '../types/tone';

interface T3kIframeProps {
  onToneSelected?: (tone: Tone) => void;
  onClose?: () => void;
  editingBlockId?: string | null;
}

const T3kIframe: React.FC<T3kIframeProps> = ({ onToneSelected, onClose }) => {
  const iframeRef = useRef<HTMLIFrameElement>(null);

  useEffect(() => {
    const handleMessage = async (event: MessageEvent<T3kDownloadEvent>) => {
      // Security check: ensure message is from expected origin if possible,
      // but for now we'll trust the message structure.

      if (event.data.type === 't3k.download.tone') {
        console.log('T3kIframe received tone:', event.data.tone.title);
        const { tone } = event.data;

        if (onToneSelected) {
          onToneSelected(tone);
        }

        // Close iframe after selection
        if (onClose) {
          onClose();
        }
      }
    };

    window.addEventListener('message', handleMessage);

    return () => {
      window.removeEventListener('message', handleMessage);
    };
  }, [onToneSelected, onClose]);

  return (
    <div style={{ width: '100%', height: '100%', position: 'relative' }}>
      <iframe
        ref={iframeRef}
        src="https://tone-zone-web-ylwj-git-embed-integration-woodyburys-projects.vercel.app/embed"
        style={{
          width: '100%',
          height: '100%',
          overflow: 'hidden',
          border: 'none',
          backgroundColor: '#000000',
        }}
        title="TONE3000"
      />
    </div>
  );
};

export default T3kIframe;
