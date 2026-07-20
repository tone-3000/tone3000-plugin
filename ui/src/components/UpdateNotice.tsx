import React, { useMemo } from 'react';
import DOMPurify from 'dompurify';
import { X } from 'lucide-react';
import type { UpdateNoticeData } from '../hooks/useUpdateNotice';
import { BORDER, MUTED, SURFACE, iconButtonStyle, pillButtonStyle } from './theme';

interface UpdateNoticeProps {
  notice: UpdateNoticeData | null;
  onRemindLater: (days: number) => void;
}

// The message body is remote HTML. Formatting tags only — no scripts, no
// event handlers, no embeds — so a compromised or spoofed response can never
// execute in the webview (which holds the JUCE bridge and OAuth tokens).
const SANITIZE_OPTIONS = {
  ALLOWED_TAGS: ['p', 'br', 'b', 'strong', 'i', 'em', 'a', 'ul', 'ol', 'li'],
  ALLOWED_ATTR: ['href'],
};

// Any link inside the message must open in the system browser, never
// navigate the plugin webview (GuardedWebView routes target=_blank there).
DOMPurify.addHook('afterSanitizeAttributes', (node) => {
  if (node.tagName === 'A') {
    node.setAttribute('target', '_blank');
    node.setAttribute('rel', 'noopener noreferrer');
  }
});

// Closing without picking a duration is the shortest snooze — the notice is
// deliberately persistent until the user updates.
const DISMISS_DAYS = 1;
const REMIND_OPTIONS = [
  { days: 1, label: '1 day' },
  { days: 7, label: '7 days' },
  { days: 30, label: '30 days' },
];

/**
 * "Update available" notice, shown at most once per editor-open when the
 * startup version check (useUpdateNotice) finds a newer published build.
 * Same full-window scrim language as OfflineModal / OAuthOverlay, one layer
 * below them so a connectivity/auth problem always wins.
 */
export const UpdateNotice: React.FC<UpdateNoticeProps> = ({ notice, onRemindLater }) => {
  const messageHtml = useMemo(
    () => (notice ? DOMPurify.sanitize(notice.messageHtml, SANITIZE_OPTIONS) : ''),
    [notice]
  );

  if (!notice) return null;

  return (
    <div
      role="dialog"
      aria-label="Plugin update available"
      style={{
        position: 'absolute',
        inset: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.5)',
        backdropFilter: 'blur(4px)',
        WebkitBackdropFilter: 'blur(4px)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: 24,
        zIndex: 2500,
      }}
    >
      <div
        style={{
          position: 'relative',
          width: 420,
          maxWidth: '100%',
          backgroundColor: SURFACE,
          border: BORDER,
          borderRadius: 16,
          padding: 24,
          color: '#fff',
          textAlign: 'center',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          gap: 16,
        }}
      >
        <button
          type="button"
          onClick={() => onRemindLater(DISMISS_DAYS)}
          aria-label="Close"
          style={{ ...iconButtonStyle(), position: 'absolute', top: 12, right: 12 }}
        >
          <X size={16} />
        </button>

        <div style={{ fontSize: 15, fontWeight: 600 }}>
          Update available — v{notice.version}
        </div>

        <div
          style={{ fontSize: 13, fontWeight: 400, lineHeight: 1.5, color: MUTED, maxWidth: 340 }}
          dangerouslySetInnerHTML={{ __html: messageHtml }}
        />

        <a
          href={notice.url}
          target="_blank"
          rel="noopener noreferrer"
          style={{ ...pillButtonStyle(), textDecoration: 'none' }}
        >
          Download update
        </a>

        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: 10,
            fontSize: 12,
            fontWeight: 400,
            color: MUTED,
          }}
        >
          <span>Remind me in</span>
          {REMIND_OPTIONS.map(({ days, label }) => (
            <button
              key={days}
              type="button"
              onClick={() => onRemindLater(days)}
              style={{
                background: 'transparent',
                border: 'none',
                padding: 0,
                fontSize: 12,
                fontWeight: 400,
                color: '#ffffff',
                cursor: 'pointer',
                textDecoration: 'underline',
              }}
            >
              {label}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
};
