import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowRight,
  Bookmark,
  Download,
  FolderClosed,
  Search as SearchIcon,
  X as XIcon,
} from 'lucide-react';
import type { T3KClient } from '../t3k/tone3000-client';
import type { Tone } from '../types/tone';
import { GEAR_LABELS, formatLabel, gearLabel } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { GearIcon, ToneImage } from './GearIcon';
import { BusyOverlay, LoadingDots } from './LoadingDots';
import { Tone3000Logo } from './Tone3000Logo';
import { HELP, helpProps } from './helpText';
import { BORDER, MUTED, SURFACE, SURFACE_RAISED, pillButtonStyle } from './theme';

/**
 * In-plugin tone browser: full-window takeover shown after the user is
 * authenticated (no-prompt OAuth). Settings-style layout (title + close in a
 * centered 600px column, whole page scrolls) with quick links across the
 * API's bounded streams (trending / downloads / favorites / created /
 * latest), a gear-type chip row on trending, and a Browse TONE3000 CTA into
 * the full-catalog Select flow. Clicking a card resolves the tone (models +
 * token) and loads it into the chain.
 *
 * The stream tabs / gear chips / tone rows / paginator port the tone3000.com
 * web components (Tabs, GearChip, ToneCard's isApiSelect variant, Paginator)
 * to this project's inline styles and theme constants.
 *
 * All queries run client-side against the TONE3000 API — native is only
 * involved for the final load (access token + model download).
 */

type StreamKind = 'trending' | 'downloaded' | 'favorited' | 'created' | 'latest';

const STREAMS: { id: StreamKind; label: string }[] = [
  { id: 'trending', label: 'Trending' },
  { id: 'latest', label: 'Latest' },
  { id: 'downloaded', label: 'Downloads' },
  { id: 'favorited', label: 'Favorites' },
  { id: 'created', label: 'Created' },
];

/** Trending feed gear types (deprecated `full-rig` / `ir` excluded). */
const GEAR_CHIPS: { id: string; label: string }[] = (
  ['amp-cab', 'amp', 'pedal', 'cab', 'outboard', 'space', 'experimental'] as const
).map((id) => ({ id, label: GEAR_LABELS[id] }));

const EMPTY_COPY: Record<StreamKind, string> = {
  trending: 'Nothing trending for this gear type right now — try another one.',
  downloaded: 'Tones you download on TONE3000 will show up here.',
  favorited: 'Tones you favorite on TONE3000 will show up here.',
  created: 'Tones you upload to TONE3000 will show up here.',
  latest: 'No recently published tones right now. Check back soon.',
};

const PAGE_SIZE = 12;

/** Remembers the last-viewed stream so the next browse lands on it. */
const STREAM_STORAGE_KEY = 't3k_browser_stream';

/** Matches Settings-style column; same width as the tone rows. */
const COLUMN_MAX_WIDTH = 600;
const CARD_IMAGE_SIZE = 120;

/** Web GearChip port: rounded-full outline chip, white border when active,
    gear glyph leading the label (currentColor, like the web's inheritColor). */
const GearChip: React.FC<{ id: string; label: string; active: boolean; onClick: () => void }> = ({
  id,
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
      display: 'flex',
      alignItems: 'center',
      gap: '8px',
      padding: '7px 16px',
      fontSize: '13px',
      borderRadius: '9999px',
      border: active ? '1px solid #ffffff' : BORDER,
      backgroundColor: 'transparent',
      color: active ? '#ffffff' : MUTED,
      cursor: 'pointer',
      whiteSpace: 'nowrap',
      transition: 'color 0.15s ease, border-color 0.15s ease',
    }}
  >
    <GearIcon gear={id} size={18} color="currentColor" />
    {label}
  </button>
);

/**
 * Horizontally scrollable row with a static right-edge fade (the gallery
 * lanes' EdgeFade ramp): content slides under a smooth ramp to the
 * background instead of hard-clipping at the column edge.
 */
const FadedScrollRow: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <div style={{ position: 'relative' }}>
    <div className="hide-scrollbar" style={{ display: 'flex', gap: '10px', overflowX: 'auto' }}>
      {children}
    </div>
    <div
      style={{
        position: 'absolute',
        top: 0,
        bottom: 0,
        right: 0,
        width: '48px',
        background: 'linear-gradient(to left, #000000, rgba(0, 0, 0, 0))',
        pointerEvents: 'none',
      }}
    />
  </div>
);

/** Web Tabs port: underline tabs — white text + white track when active. */
const StreamTab: React.FC<{ label: string; active: boolean; onClick: () => void }> = ({
  label,
  active,
  onClick,
}) => (
  <button
    onClick={onClick}
    style={{
      padding: '10px 16px',
      fontSize: '14px',
      fontWeight: 600,
      background: 'transparent',
      border: 'none',
      borderBottom: active ? '2px solid #ffffff' : '2px solid rgba(84, 84, 88, 0.65)',
      color: active ? '#ffffff' : MUTED,
      cursor: 'pointer',
      whiteSpace: 'nowrap',
      transition: 'color 0.15s ease',
    }}
  >
    {label}
  </button>
);

/** Count with a small leading icon (downloads / favorites / models). */
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

/**
 * Web ToneCard port (`isApiSelect` variant, simplified): horizontal list row
 * on a raised surface with no border — image left; title, gear + format,
 * counts and creator stacked right. Secondary text is weight 400 like the
 * web (the plugin body default is 600). Stats match the gear-type text size.
 */
const ToneRow: React.FC<{
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
    }}
  >
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        gap: '20px',
        padding: '12px 16px',
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
          textAlign: 'left',
        }}
      >
        <span
          style={{
            fontSize: '18px',
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

        <div style={{ display: 'flex', alignItems: 'center', gap: '14px', minWidth: 0 }}>
          <span
            style={{
              fontSize: '14px',
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

        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            gap: '20px',
            color: MUTED,
            fontSize: '14px',
          }}
        >
          <CountStat icon={<Download size={14} />} value={tone.downloads_count ?? 0} />
          <CountStat icon={<Bookmark size={14} />} value={tone.favorites_count ?? 0} />
          <CountStat icon={<FolderClosed size={14} />} value={tone.models_count ?? 0} />
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', minWidth: 0 }}>
          <div
            style={{
              width: '23px',
              height: '23px',
              borderRadius: '50%',
              overflow: 'hidden',
              flexShrink: 0,
            }}
          >
            <AvatarImage src={tone.user?.avatar_url} alt={tone.user?.username ?? ''} size={23} />
          </div>
          <span
            style={{
              fontSize: '14px',
              color: '#ffffff',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            }}
          >
            {tone.user?.username}
          </span>
        </div>
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
    return STREAMS.some((s) => s.id === saved) ? (saved as StreamKind) : 'trending';
  });
  const [gear, setGear] = useState<string>('amp-cab');
  const [page, setPage] = useState(1);
  const [result, setResult] = useState<StreamResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [retryKey, setRetryKey] = useState(0);
  /** Tone id currently being resolved+loaded (blocks further picks). */
  const [pickingId, setPickingId] = useState<number | null>(null);
  const [pickError, setPickError] = useState<string | null>(null);
  const scrollRef = useRef<HTMLDivElement | null>(null);

  // Jump to the top whenever fresh results land (page turn / tab / gear).
  useEffect(() => {
    scrollRef.current?.scrollTo({ top: 0 });
  }, [result]);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);

    const fetchStream = (): Promise<StreamResult> => {
      switch (stream) {
        case 'trending':
          return client.listTrendingTones(gear).then((res) => ({ data: res.data }));
        case 'latest':
          return client.listLatestTones().then((res) => ({ data: res.data }));
        case 'downloaded':
        case 'favorited':
        case 'created': {
          const list =
            stream === 'downloaded'
              ? client.listDownloadedTones.bind(client)
              : stream === 'favorited'
                ? client.listFavoritedTones.bind(client)
                : client.listCreatedTones.bind(client);
          return list({ page, pageSize: PAGE_SIZE }).then((res) => ({
            data: res.data,
            page: res.page,
            totalPages: res.total_pages,
          }));
        }
      }
    };

    fetchStream()
      .then((res) => {
        if (!cancelled) setResult(res);
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
  }, [client, stream, gear, page, retryKey]);

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
    // Keep the current tab's content mounted — it stays visible ("disabled")
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
            display: 'flex',
            flexDirection: 'column',
            gap: '16px',
            pointerEvents: loading ? 'none' : 'auto',
          }}
        >
          {result.data.map((tone) => {
            const unavailable = isToneUnavailable(tone);
            return (
              <ToneRow
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
      style={{
        position: 'absolute',
        inset: 0,
        backgroundColor: '#000000',
        zIndex: 2000,
        overflow: 'auto',
        color: '#ffffff',
      }}
    >
      <div
        style={{
          maxWidth: `${COLUMN_MAX_WIDTH}px`,
          margin: '0 auto',
          padding: '28px 24px 40px',
          boxSizing: 'border-box',
        }}
      >
        {/* Header — title left; Browse + close on the right. */}
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
            gap: '16px',
            marginBottom: '28px',
          }}
        >
          <span style={{ fontSize: '22px', fontWeight: 600, color: '#ffffff' }}>Select Tone</span>
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px', flexShrink: 0 }}>
            <button onClick={onBrowseTone3000} style={{ ...pillButtonStyle(), gap: '10px' }}>
              <SearchIcon size={14} />
              Browse
              <Tone3000Logo height={12} />
            </button>
            <button
              onClick={onClose}
              {...helpProps(HELP.closeToneBrowser)}
              style={{
                background: 'transparent',
                border: 'none',
                color: '#ffffff',
                cursor: 'pointer',
                display: 'flex',
                alignItems: 'center',
                padding: '4px',
              }}
            >
              <XIcon size={20} />
            </button>
          </div>
        </div>

        {/* Stream tabs */}
        <div style={{ marginBottom: '24px' }}>
          <div className="hide-scrollbar" style={{ display: 'flex', overflowX: 'auto' }}>
            {STREAMS.map((s) => (
              <StreamTab
                key={s.id}
                label={s.label}
                active={stream === s.id}
                onClick={() => switchStream(s.id)}
              />
            ))}
          </div>
        </div>

        {/* Trending gear chips + pick errors */}
        {(stream === 'trending' || pickError) && (
          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              gap: '10px',
              marginBottom: '24px',
            }}
          >
            {stream === 'trending' && (
              <FadedScrollRow>
                {GEAR_CHIPS.map((g) => (
                  <GearChip
                    key={g.id}
                    id={g.id}
                    label={g.label}
                    active={gear === g.id}
                    onClick={() => setGear(g.id)}
                  />
                ))}
              </FadedScrollRow>
            )}
            {pickError && <span style={{ fontSize: '12px', color: '#ff6b5e' }}>{pickError}</span>}
          </div>
        )}

        {/* Tone list */}
        <div style={{ marginBottom: showPaginator ? '16px' : 0 }}>{body}</div>

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
  );
};
