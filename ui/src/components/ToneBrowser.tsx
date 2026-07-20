import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowRight,
  Download,
  FolderClosed,
  Search as SearchIcon,
} from 'lucide-react';
import type { T3KClient } from '../t3k/tone3000-client';
import type { Tone } from '../types/tone';
import { formatLabel, gearLabel } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { ToneImage } from './GearIcon';
import { BusyOverlay, LoadingDots } from './LoadingDots';
import { HELP, helpProps } from './helpText';
import { CARD_WIDTH } from './chainLayout';
import { BORDER, MUTED, SURFACE, SURFACE_RAISED, iconButtonStyle, pillButtonStyle } from './theme';

/**
 * In-plugin tone browser: renders inside the main view in place of the signal
 * chain (like the expanded block card), keeping the I/O meters and faceplate
 * around it. An expanded-block-style header (back arrow + "Select Tone" +
 * Browse CTA) stays pinned while the content scrolls beneath it.
 *
 * Bounded streams (recents=downloads / favorites / created) show as pills.
 * Cards render two-up. Clicking a card resolves the tone (models + token) and
 * loads it into the chain.
 *
 * All queries run client-side against the TONE3000 API — native is only
 * involved for the final load (access token + model download).
 */

type StreamKind = 'downloaded' | 'favorited' | 'created';

const STREAMS: { id: StreamKind; label: string }[] = [
  { id: 'downloaded', label: 'Recents' },
  { id: 'favorited', label: 'Favorites' },
  { id: 'created', label: 'Created' },
];

const EMPTY_COPY: Record<StreamKind, string> = {
  downloaded: 'Tones you download on TONE3000 will show up here.',
  favorited: 'Tones you favorite on TONE3000 will show up here.',
  created: 'Tones you upload to TONE3000 will show up here.',
};

const PAGE_SIZE = 12;

/** Remembers the last-viewed stream so the next browse lands on it. */
const STREAM_STORAGE_KEY = 't3k_browser_stream';

/** Content column matches the expanded block card width. */
const COLUMN_MAX_WIDTH = CARD_WIDTH;
const CARD_IMAGE_SIZE = 112;

/** Compact relative time (e.g. "13h", "3d", "2w") for the card creator line. */
const timeAgo = (iso?: string): string => {
  if (!iso) return '';
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return '';
  const secs = Math.max(0, (Date.now() - then) / 1000);
  const mins = secs / 60;
  const hours = mins / 60;
  const days = hours / 24;
  if (secs < 60) return `${Math.floor(secs)}s`;
  if (mins < 60) return `${Math.floor(mins)}m`;
  if (hours < 24) return `${Math.floor(hours)}h`;
  if (days < 7) return `${Math.floor(days)}d`;
  if (days < 365) return `${Math.floor(days / 7)}w`;
  return `${Math.floor(days / 365)}y`;
};

/** Pill toggle for the stream selector (rounded-full; white when active). */
const StreamPill: React.FC<{ label: string; active: boolean; onClick: () => void }> = ({
  label,
  active,
  onClick,
}) => (
  <button
    type="button"
    aria-pressed={active}
    onClick={onClick}
    style={{
      flexShrink: 0,
      padding: '8px 18px',
      fontSize: '13px',
      fontWeight: 400,
      borderRadius: '9999px',
      border: active ? '1px solid #ffffff' : BORDER,
      backgroundColor: 'transparent',
      color: active ? '#ffffff' : MUTED,
      cursor: 'pointer',
      whiteSpace: 'nowrap',
      transition: 'color 0.15s ease, border-color 0.15s ease',
    }}
  >
    {label}
  </button>
);

/** Count with a small leading icon (downloads / models). */
const CountStat: React.FC<{ icon: React.ReactNode; value: number }> = ({ icon, value }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
    {icon}
    <span>{value.toLocaleString()}</span>
  </div>
);

/** The plugin runtime only loads A2 NAM weights — NAM tones without any A2
    model can't be added and render inert. Other formats are always fine. */
const isToneUnavailable = (tone: Tone): boolean =>
  tone.format?.toLowerCase() === 'nam' && (tone.a2_models_count ?? 0) === 0;

/** Two-up tone card: square image left; title, gear + format, counts and
    creator stacked right. Secondary text is weight 400 (the plugin body
    default is 600). */
const ToneCard: React.FC<{
  tone: Tone;
  loading: boolean;
  disabled: boolean;
  onPick: () => void;
}> = ({ tone, loading, disabled, onPick }) => (
  <div
    onClick={disabled ? undefined : onPick}
    style={{
      position: 'relative',
      width: '100%',
      borderRadius: '12px',
      backgroundColor: SURFACE,
      cursor: disabled ? 'default' : 'pointer',
      opacity: disabled && !loading ? 0.45 : 1,
      boxSizing: 'border-box',
      overflow: 'hidden',
      fontWeight: 400,
      letterSpacing: 'normal',
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      gap: '16px',
      padding: '12px',
    }}
  >
    <div
      style={{
        width: `${CARD_IMAGE_SIZE}px`,
        height: `${CARD_IMAGE_SIZE}px`,
        borderRadius: '8px',
        overflow: 'hidden',
        flexShrink: 0,
        backgroundColor: SURFACE_RAISED,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}
    >
      <ToneImage
        src={tone.images?.[0]}
        alt={tone.title}
        gear={tone.gear}
        boxSize={CARD_IMAGE_SIZE}
        draggable={false}
      />
    </div>

    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '8px',
        minWidth: 0,
        flex: 1,
        textAlign: 'left',
      }}
    >
      <span
        style={{
          fontSize: '14px',
          fontWeight: 600,
          color: '#ffffff',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
          display: '-webkit-box',
          WebkitLineClamp: 2,
          WebkitBoxOrient: 'vertical',
          lineHeight: 1.3,
        }}
      >
        {tone.title}
      </span>

      <div style={{ display: 'flex', alignItems: 'center', gap: '10px', minWidth: 0 }}>
        <span
          style={{
            fontSize: '13px',
            color: MUTED,
            whiteSpace: 'nowrap',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
          }}
        >
          {gearLabel(tone.gear)}
        </span>
        <FormatBadge label={formatLabel(tone.format)} />
      </div>

      <div style={{ display: 'flex', flexDirection: 'row', gap: '16px', color: MUTED, fontSize: '13px' }}>
        <CountStat icon={<Download size={14} />} value={tone.downloads_count ?? 0} />
        <CountStat icon={<FolderClosed size={14} />} value={tone.models_count ?? 0} />
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: '8px', minWidth: 0 }}>
        <div
          style={{
            width: '22px',
            height: '22px',
            borderRadius: '50%',
            overflow: 'hidden',
            flexShrink: 0,
          }}
        >
          <AvatarImage src={tone.user?.avatar_url} alt={tone.user?.username ?? ''} size={22} />
        </div>
        <span
          style={{
            fontSize: '13px',
            color: '#ffffff',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          }}
        >
          {tone.user?.username}
          {timeAgo(tone.created_at) && (
            <span style={{ color: MUTED }}> · {timeAgo(tone.created_at)}</span>
          )}
        </span>
      </div>
    </div>

    {loading && <BusyOverlay align="center" />}
  </div>
);

/** Web Paginator port: numbered outline buttons + arrow prev/next. */
const Paginator: React.FC<{
  page: number;
  totalPages: number;
  onPageChange: (page: number) => void;
}> = ({ page, totalPages, onPageChange }) => {
  const pages: (number | '...')[] = [];
  if (totalPages > 7) {
    if (page <= 3) {
      for (let i = 1; i <= 4; i++) pages.push(i);
      pages.push('...', totalPages);
    } else if (page >= totalPages - 2) {
      pages.push(1, '...');
      for (let i = totalPages - 3; i <= totalPages; i++) pages.push(i);
    } else {
      pages.push(1, '...', page - 1, page, page + 1, '...', totalPages);
    }
  } else {
    for (let i = 1; i <= totalPages; i++) pages.push(i);
  }

  const arrowStyle: React.CSSProperties = {
    background: 'transparent',
    border: 'none',
    color: MUTED,
    cursor: 'pointer',
    display: 'flex',
    alignItems: 'center',
    padding: '4px',
  };

  return (
    <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        {page > 1 && (
          <button aria-label="Previous page" onClick={() => onPageChange(page - 1)} style={arrowStyle}>
            <ArrowLeft size={20} />
          </button>
        )}
        {pages.map((p, idx) =>
          p === '...' ? (
            <span key={`ellipsis-${idx}`} style={{ padding: '4px 8px', color: MUTED, fontSize: '13px' }}>
              …
            </span>
          ) : (
            <button
              key={p}
              onClick={() => onPageChange(p)}
              style={{
                padding: '4px 12px',
                borderRadius: '6px',
                border: p === page ? '1px solid #ffffff' : BORDER,
                backgroundColor: 'transparent',
                color: p === page ? '#ffffff' : MUTED,
                fontSize: '13px',
                cursor: 'pointer',
              }}
            >
              {p}
            </button>
          )
        )}
        {page < totalPages && (
          <button aria-label="Next page" onClick={() => onPageChange(page + 1)} style={arrowStyle}>
            <ArrowRight size={20} />
          </button>
        )}
      </div>
    </div>
  );
};

interface ToneBrowserProps {
  client: T3KClient;
  /** Resolve + load a picked tone; the parent closes the browser on success. */
  onPickTone: (toneId: number) => Promise<void>;
  /** Launch the full-catalog Select flow (prompt=select_tone). */
  onBrowseTone3000: () => void;
  /** Return to the signal chain (the header back arrow). */
  onClose: () => void;
}

interface StreamResult {
  data: Tone[];
  page?: number;
  totalPages?: number;
}

export const ToneBrowser: React.FC<ToneBrowserProps> = ({
  client,
  onPickTone,
  onBrowseTone3000,
  onClose,
}) => {
  // Land on the stream the user was on last time they browsed.
  const [stream, setStream] = useState<StreamKind>(() => {
    const saved = localStorage.getItem(STREAM_STORAGE_KEY);
    return STREAMS.some((s) => s.id === saved) ? (saved as StreamKind) : 'downloaded';
  });
  const [page, setPage] = useState(1);
  const [result, setResult] = useState<StreamResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [retryKey, setRetryKey] = useState(0);
  /** Tone id currently being resolved+loaded (blocks further picks). */
  const [pickingId, setPickingId] = useState<number | null>(null);
  const [pickError, setPickError] = useState<string | null>(null);
  const scrollRef = useRef<HTMLDivElement | null>(null);

  // Jump to the top whenever fresh results land (page turn / pill).
  useEffect(() => {
    scrollRef.current?.scrollTo({ top: 0 });
  }, [result]);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);

    const list =
      stream === 'downloaded'
        ? client.listDownloadedTones.bind(client)
        : stream === 'favorited'
          ? client.listFavoritedTones.bind(client)
          : client.listCreatedTones.bind(client);

    list({ page, pageSize: PAGE_SIZE })
      .then((res) => {
        if (!cancelled) setResult({ data: res.data, page: res.page, totalPages: res.total_pages });
      })
      .catch(() => {
        if (!cancelled) setError('Failed to load tones from TONE3000.');
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });

    return () => {
      cancelled = true;
    };
  }, [client, stream, page, retryKey]);

  const handlePick = useCallback(
    async (toneId: number) => {
      if (pickingId !== null) return;
      setPickError(null);
      setPickingId(toneId);
      try {
        await onPickTone(toneId);
      } catch (err) {
        console.error('Failed to load picked tone', err);
        setPickError('Failed to load that tone. Please try again.');
        setPickingId(null);
      }
    },
    [onPickTone, pickingId]
  );

  const switchStream = (next: StreamKind) => {
    if (next === stream) return;
    setStream(next);
    localStorage.setItem(STREAM_STORAGE_KEY, next);
    setPage(1);
    // Keep the current stream's content mounted — it stays visible ("disabled")
    // under the busy overlay until the new stream loads in (web pattern).
  };

  const body = (() => {
    if (error && !loading) {
      return (
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            gap: '14px',
            padding: '64px 0',
            color: MUTED,
            fontSize: '13px',
            fontWeight: 400,
            textAlign: 'center',
          }}
        >
          {error}
          <button onClick={() => setRetryKey((k) => k + 1)} style={pillButtonStyle()}>
            Try again
          </button>
        </div>
      );
    }

    if (!result || result.data.length === 0) {
      // First load (nothing to dim yet): dots alone in the empty area.
      if (loading) {
        return (
          <div style={{ display: 'flex', justifyContent: 'center', padding: '64px 0' }}>
            <LoadingDots />
          </div>
        );
      }
      return (
        <div
          style={{
            padding: '64px 24px',
            color: MUTED,
            fontSize: '13px',
            fontWeight: 400,
            textAlign: 'center',
          }}
        >
          {EMPTY_COPY[stream]}
        </div>
      );
    }

    // Content stays mounted while a new stream/page loads — dimmed and
    // non-interactive under the busy overlay (web `Tones.tsx` pattern).
    return (
      <div style={{ position: 'relative' }}>
        <div
          style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: '16px',
            pointerEvents: loading ? 'none' : 'auto',
          }}
        >
          {result.data.map((tone) => {
            const unavailable = isToneUnavailable(tone);
            return (
              <ToneCard
                key={tone.id}
                tone={tone}
                loading={pickingId === tone.id}
                disabled={unavailable || pickingId !== null}
                onPick={() => handlePick(tone.id)}
              />
            );
          })}
        </div>
        {loading && <BusyOverlay />}
      </div>
    );
  })();

  // Paginated streams keep the paginator at the end of the scrolled page.
  const showPaginator = !error && result?.totalPages != null && result.totalPages > 1;

  return (
    <div
      ref={scrollRef}
      className="hide-scrollbar"
      style={{
        height: '100%',
        overflowY: 'auto',
        overflowX: 'hidden',
        color: '#ffffff',
      }}
    >
      <div style={{ maxWidth: `${COLUMN_MAX_WIDTH}px`, margin: '0 auto', width: '100%' }}>
        {/* Header — pinned while the content scrolls beneath it. Matches the
            expanded block card header (back arrow + hairline rule). The 24px
            gap below the main header is owned by the parent (Plugin) wrapper. */}
        <div
          style={{
            position: 'sticky',
            top: 0,
            zIndex: 2,
            backgroundColor: '#000000',
            display: 'flex',
            alignItems: 'center',
            gap: '24px',
            padding: '24px 0 12px',
            borderBottom: BORDER,
          }}
        >
          <button
            onClick={onClose}
            {...helpProps(HELP.closeToneBrowser)}
            style={{ ...iconButtonStyle(24), color: '#ffffff' }}
          >
            <ArrowLeft size={18} />
          </button>
          <span
            style={{
              fontFamily: 'monospace',
              fontSize: '16px',
              fontWeight: 500,
              letterSpacing: '0.08em',
              textTransform: 'uppercase',
              color: '#ffffff',
            }}
          >
            Select Tone
          </span>
          <div style={{ flex: 1 }} />
          <button onClick={onBrowseTone3000} style={{ ...pillButtonStyle(), gap: '8px' }}>
            <SearchIcon size={14} />
            Browse
          </button>
        </div>

        {/* Scrolling content. The bottom padding lives inside the scroll area
            so the paginator / last row clears the faceplate at max scroll
            (it isn't a fixed band above the faceplate). */}
        <div style={{ padding: '20px 0 24px' }}>
          {/* Stream pills */}
          <div className="hide-scrollbar" style={{ display: 'flex', gap: '10px', overflowX: 'auto' }}>
            {STREAMS.map((s) => (
              <StreamPill
                key={s.id}
                label={s.label}
                active={stream === s.id}
                onClick={() => switchStream(s.id)}
              />
            ))}
          </div>

          {pickError && (
            <div style={{ marginTop: '16px' }}>
              <span style={{ fontSize: '12px', fontWeight: 400, color: '#ff6b5e' }}>
                {pickError}
              </span>
            </div>
          )}

          {/* Tone grid */}
          <div style={{ marginTop: '24px', marginBottom: showPaginator ? '16px' : 0 }}>{body}</div>

          {showPaginator && (
            <div
              style={{
                display: 'flex',
                justifyContent: 'flex-end',
                pointerEvents: loading ? 'none' : 'auto',
                opacity: loading ? 0.5 : 1,
              }}
            >
              <Paginator page={page} totalPages={result!.totalPages!} onPageChange={setPage} />
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
