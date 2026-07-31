import React, { useCallback, useEffect, useRef, useState } from 'react';
import { ArrowLeft, ArrowRight, Download, FolderClosed, Search as SearchIcon } from 'lucide-react';
import type { T3KClient } from '../t3k/tone3000-client';
import type { Tone } from '../types/tone';
import { formatLabel, gearLabel, GEAR_FILTERS } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { GearIcon, ToneImage } from './GearIcon';
import { BusyOverlay, LoadingDots } from './LoadingDots';
import { HELP, helpProps } from './helpText';
import { EdgeFade, EDGE_FADE_WIDTH } from './GalleryLane';
import { CARD_WIDTH } from './chainLayout';
import { T3kMark } from './T3kMark';
import {
  BORDER,
  BRAND_RED,
  MUTED,
  SURFACE,
  SURFACE_RAISED,
  WHITE,
  filledPillButtonStyle,
  pillButtonStyle,
} from './theme';

/**
 * In-plugin tone browser: renders inside the main view in place of the signal
 * chain (like the expanded block card), keeping the I/O meters and faceplate
 * around it. An expanded-block-style header (back arrow + "Select Tone" +
 * Browse CTA) stays pinned while the content scrolls beneath it.
 *
 * No longer gated by TONE3000 sign-in: Trending is a public (auth-optional)
 * feed, so it's always browsable. The three session-scoped streams (Recently
 * used / Favorites / Created) still need an account — while signed out they
 * show a sign-in prompt instead of erroring. Resolving a picked tone (models
 * + a download token) always needs a session too, so tapping a Trending card
 * while signed out routes into sign-in instead of a picker request that
 * would just fail. The OAuth redirect only ever fires from one of those
 * sign-in CTAs (no-prompt login, reopens this browser) or the header's
 * Browse button (always the full-catalog Select flow). Every stream carries
 * an optional single gear-type filter via radio-select pills.
 *
 * All queries run client-side against the TONE3000 API — native is only
 * involved for the final load (access token + model download).
 */

type StreamKind = 'trending' | 'downloaded' | 'favorited' | 'created';

const TABS: { id: StreamKind; label: string }[] = [
  { id: 'trending', label: 'Trending' },
  { id: 'downloaded', label: 'Recently used' },
  { id: 'favorited', label: 'Favorites' },
  { id: 'created', label: 'Created' },
];

/** Streams that require a signed-in TONE3000 session; Trending is public. */
const GATED_STREAMS = new Set<StreamKind>(['downloaded', 'favorited', 'created']);

const EMPTY_COPY: Record<StreamKind, string> = {
  trending: 'No trending tones right now — check back soon.',
  downloaded: 'Tones you download on TONE3000 will show up here.',
  favorited: 'Tones you favorite on TONE3000 will show up here.',
  created: 'Tones you upload to TONE3000 will show up here.',
};

const SIGN_IN_HEADING = 'Sign in to see your tones and discover a zillion new ones.';
const DISCOVER_MORE_HEADING = 'Discover a zillion more tones.';

const PAGE_SIZE = 12;

/** Remembers the last-viewed stream so the next browse lands on it. */
const STREAM_STORAGE_KEY = 't3k_browser_stream';

/** Content column — same width as the expanded-block card so the browser
    frame lines up with BLOCK visually. Header / tabs / grid are flush to
    this edge (like ← BLOCK); only the gear-filter row uses EDGE_FADE_WIDTH
    as scroll padding so pills sit outside the fade at rest. */
const COLUMN_MAX_WIDTH = CARD_WIDTH;
const GUTTER = EDGE_FADE_WIDTH;
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

/** Stream header as tabs: white text + underline when active, muted
    otherwise, each sized to its own label (not stretched full-width). Weight
    stays constant across states — bolding only the active label reflows the
    others and shifts the row every time the tab changes. */
const StreamTabs: React.FC<{ active: StreamKind; onChange: (s: StreamKind) => void }> = ({
  active,
  onChange,
}) => (
  <div role="tablist" style={{ display: 'flex', gap: '28px', borderBottom: BORDER }}>
    {TABS.map((tab) => {
      const selected = tab.id === active;
      return (
        <button
          key={tab.id}
          type="button"
          role="tab"
          aria-selected={selected}
          onClick={() => onChange(tab.id)}
          style={{
            background: 'transparent',
            border: 'none',
            padding: '0 0 12px',
            marginBottom: '-1px',
            borderBottom: `2px solid ${selected ? WHITE : 'transparent'}`,
            color: selected ? WHITE : MUTED,
            fontSize: '14px',
            fontWeight: 400,
            cursor: 'pointer',
            whiteSpace: 'nowrap',
          }}
        >
          {tab.label}
        </button>
      );
    })}
  </div>
);

/** Taller than the default outline pill (`pillButtonStyle`) — Browse is the
    single most important action on this screen (the persistent path to the
    full catalog), so it gets a more prominent 40px-tall treatment; the icon
    and mark scale up with it rather than sitting undersized in the extra
    height. */
const browseButtonStyle: React.CSSProperties = {
  ...pillButtonStyle(),
  height: '40px',
  padding: '0 22px',
  gap: '10px',
  fontSize: '15px',
  fontWeight: 400,
};

/** Outline pill CTA that opens the full-catalog Select flow — the header's
    persistent "Browse" affordance, and the exact same component reused as
    Trending's footer for signed-in users (API design guidance: always keep
    a path to the full catalog visible from any partial tone list). */
const BrowseButton: React.FC<{ onClick: () => void }> = ({ onClick }) => (
  <button type="button" onClick={onClick} style={browseButtonStyle}>
    <SearchIcon size={16} />
    Browse
    <T3kMark height={14} />
  </button>
);

/** Gear-type filter chip: icon + label, radio-select (click the active pill
    again to clear it back to "no filter"). */
const GearFilterPill: React.FC<{
  id: string;
  label: string;
  active: boolean;
  onClick: () => void;
}> = ({ id, label, active, onClick }) => (
  <button
    type="button"
    role="radio"
    aria-checked={active}
    onClick={onClick}
    style={{
      display: 'flex',
      alignItems: 'center',
      gap: '8px',
      flexShrink: 0,
      padding: '8px 16px',
      fontSize: '14px',
      fontWeight: 400,
      borderRadius: '9999px',
      border: active ? `1px solid ${WHITE}` : BORDER,
      backgroundColor: 'transparent',
      color: active ? WHITE : MUTED,
      cursor: 'pointer',
      whiteSpace: 'nowrap',
    }}
  >
    <GearIcon gear={id} size={20} color={active ? WHITE : MUTED} />
    {label}
  </button>
);

/** Row of radio-select gear filters for the current stream — default is no
    filter (every gear type). Edges fade to black under the same gradient
    scrim as the chain gallery's horizontal scroll (`EdgeFade`), hinting more
    pills sit off-screen instead of hard-clipping them. Spans the full
    column width with EDGE_FADE_WIDTH as internal scroll padding, so at rest
    the first/last pill sits just outside the fade zone — exactly like the
    chain gallery's own lanes — and only slides under the gradient once
    actually scrolled. */
const GearFilterRow: React.FC<{ active: string | null; onChange: (id: string | null) => void }> = ({
  active,
  onChange,
}) => (
  <div style={{ position: 'relative' }}>
    <div
      role="radiogroup"
      aria-label="Filter by gear type"
      className="hide-scrollbar"
      style={{ display: 'flex', gap: '10px', overflowX: 'auto', padding: `0 ${GUTTER}px` }}
    >
      {GEAR_FILTERS.map((g) => (
        <GearFilterPill
          key={g.id}
          id={g.id}
          label={g.label}
          active={active === g.id}
          onClick={() => onChange(active === g.id ? null : g.id)}
        />
      ))}
    </div>
    <EdgeFade side="left" />
    <EdgeFade side="right" />
  </div>
);

/** Centered sign-in prompt: shown in place of a gated stream's content while
    signed out, and as a discovery footer under the (public) Trending feed.
    The only two places that ever trigger the no-prompt login flow. */
const SignInPrompt: React.FC<{ heading: string; onSignIn: () => void }> = ({
  heading,
  onSignIn,
}) => (
  <div
    style={{
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: '16px',
      padding: '48px 24px',
      textAlign: 'center',
    }}
  >
    <T3kMark height={28} />
    <span style={{ fontSize: '14px', fontWeight: 400, color: '#ffffff', maxWidth: '340px' }}>
      {heading}
    </span>
    <button type="button" onClick={onSignIn} style={filledPillButtonStyle}>
      Sign in or create free account
    </button>
  </div>
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

      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          gap: '16px',
          color: MUTED,
          fontSize: '13px',
        }}
      >
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
          <button
            aria-label="Previous page"
            onClick={() => onPageChange(page - 1)}
            style={arrowStyle}
          >
            <ArrowLeft size={20} />
          </button>
        )}
        {pages.map((p, idx) =>
          p === '...' ? (
            <span
              key={`ellipsis-${idx}`}
              style={{ padding: '4px 8px', color: MUTED, fontSize: '13px' }}
            >
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
  /**
   * True while an OAuth callback is still being resolved (token exchange in
   * flight). The browser is pre-mounted under the busy scrim on the first
   * render after the redirect — before `client` has tokens — so the stream
   * fetch must wait for this to clear or a gated stream fails with
   * not_authenticated.
   */
  authPending?: boolean;
  /** True once the TONE3000 OAuth session has tokens. Gates the three
      session-scoped streams (Recently used / Favorites / Created) — Trending
      stays open either way. */
  authenticated: boolean;
  /** Resolve + load a picked tone; the parent closes the browser on success. */
  onPickTone: (toneId: number) => Promise<void>;
  /** Launch the full-catalog Select flow (prompt=select_tone). */
  onBrowseTone3000: () => void;
  /** Run the no-prompt login flow without leaving the browser — fired from
      any sign-in CTA (a gated stream's prompt, or Trending's discovery
      footer). Never fired from Browse, which always runs Select instead. */
  onSignIn: () => void;
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
  authPending = false,
  authenticated,
  onPickTone,
  onBrowseTone3000,
  onSignIn,
  onClose,
}) => {
  // Land on the stream the user was on last time they browsed; default to
  // the public Trending feed rather than a gated stream that might now be
  // unreachable (signed out on a fresh session).
  const [stream, setStream] = useState<StreamKind>(() => {
    const saved = localStorage.getItem(STREAM_STORAGE_KEY);
    return TABS.some((s) => s.id === saved) ? (saved as StreamKind) : 'trending';
  });
  const [gearFilter, setGearFilter] = useState<string | null>(null);
  const [page, setPage] = useState(1);
  const [result, setResult] = useState<StreamResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [retryKey, setRetryKey] = useState(0);
  /** Tone id currently being resolved+loaded (blocks further picks). */
  const [pickingId, setPickingId] = useState<number | null>(null);
  const [pickError, setPickError] = useState<string | null>(null);
  const scrollRef = useRef<HTMLDivElement | null>(null);

  const showSignInPrompt = GATED_STREAMS.has(stream) && !authenticated;

  // Jump to the top whenever fresh results land (page turn / pill).
  useEffect(() => {
    scrollRef.current?.scrollTo({ top: 0 });
  }, [result]);

  useEffect(() => {
    // Pre-mounted during an OAuth return: the token exchange hasn't finished
    // yet, so we don't yet know whether to render the gated prompt or fetch.
    // Keep showing the loading state; this effect reruns once it clears.
    if (authPending) return;

    // Gated stream, signed out: skip the fetch entirely (it would only fail
    // with not_authenticated and trip the client's re-auth callback) — the
    // sign-in prompt renders instead.
    if (showSignInPrompt) {
      setResult(null);
      setLoading(false);
      setError(null);
      return;
    }

    let cancelled = false;
    setLoading(true);
    setError(null);

    (async () => {
      try {
        if (stream === 'trending') {
          const res = await client.listTrendingTones(gearFilter ?? undefined);
          if (!cancelled) setResult({ data: res.data });
        } else {
          const list =
            stream === 'downloaded'
              ? client.listDownloadedTones.bind(client)
              : stream === 'favorited'
                ? client.listFavoritedTones.bind(client)
                : client.listCreatedTones.bind(client);
          const res = await list({ page, pageSize: PAGE_SIZE, gear: gearFilter ?? undefined });
          if (!cancelled) setResult({ data: res.data, page: res.page, totalPages: res.total_pages });
        }
      } catch {
        if (!cancelled) setError('Failed to load tones from TONE3000.');
      } finally {
        if (!cancelled) setLoading(false);
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [authPending, client, stream, page, gearFilter, showSignInPrompt, retryKey]);

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
    // Each stream starts unfiltered — filters don't carry across tabs.
    setGearFilter(null);
  };

  const handleGearFilterChange = (gear: string | null) => {
    setGearFilter(gear);
    setPage(1);
  };

  const body = (() => {
    if (showSignInPrompt) {
      return <SignInPrompt heading={SIGN_IN_HEADING} onSignIn={onSignIn} />;
    }

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
            // Trending is viewable while signed out, but resolving a tone
            // (fetching its models + a download token) still needs a
            // session — route the click through sign-in instead of a picker
            // request that would just fail as not_authenticated.
            const needsSignIn = stream === 'trending' && !authenticated;
            return (
              <ToneCard
                key={tone.id}
                tone={tone}
                loading={pickingId === tone.id}
                disabled={unavailable || pickingId !== null}
                onPick={() => (needsSignIn ? onSignIn() : handlePick(tone.id))}
              />
            );
          })}
        </div>
        {loading && <BusyOverlay />}
      </div>
    );
  })();

  // Paginated streams keep the paginator at the end of the scrolled page.
  // Trending is a fixed top-10 feed — never paginated.
  const showPaginator =
    !showSignInPrompt && !error && result?.totalPages != null && result.totalPages > 1;
  // Trending always closes with a path to the rest of the catalog: signed
  // out that's a sign-in nudge (picking a tone needs a session anyway);
  // signed in it's just the persistent Browse CTA again, restated here so
  // it's still reachable after scrolling past the top-10 feed.
  const showTrendingFooter = stream === 'trending' && !error;

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
            expanded block card header (back arrow + hairline rule). The tabs
            live in the pinned area too, so only the pills and results scroll
            underneath. */}
        <div
          style={{
            position: 'sticky',
            top: 0,
            zIndex: 2,
            backgroundColor: '#000000',
          }}
        >
          <div
            style={{
              display: 'flex',
              // Top-align with the band (Browse is taller); arrow + label stay
              // centered on each other inside the back button.
              alignItems: 'flex-start',
              gap: '16px',
              // Top inset comes from Plugin's shared 24px middle-band pad.
              // No side inset — flush with the 800px column like ← BLOCK.
              padding: '0 0 16px',
            }}
          >
            <button
              type="button"
              onClick={onClose}
              {...helpProps(HELP.closeToneBrowser)}
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '16px',
                background: 'transparent',
                border: 'none',
                outline: 'none',
                padding: 0,
                margin: 0,
                cursor: 'pointer',
                color: '#ffffff',
              }}
            >
              <ArrowLeft size={16} style={{ display: 'block', flexShrink: 0 }} />
              <span
                style={{
                  fontFamily: 'monospace',
                  fontSize: '16px',
                  fontWeight: 400,
                  textTransform: 'uppercase',
                  lineHeight: 1.4,
                }}
              >
                Select Tone
              </span>
            </button>
            <div style={{ flex: 1 }} />
            <BrowseButton onClick={onBrowseTone3000} />
          </div>
          <StreamTabs active={stream} onChange={switchStream} />
        </div>

        {/* Scrolling content — 24px bottom pad so the paginator / last row
            has air above the faceplate (Select fills the center column to
            the faceplate; this pad lives in the scroll content, not the
            shared meter band). */}
        <div style={{ padding: '20px 0 24px' }}>
          {!showSignInPrompt && (
            <GearFilterRow active={gearFilter} onChange={handleGearFilterChange} />
          )}

          {pickError && (
            <div style={{ marginTop: '16px' }}>
              <span style={{ fontSize: '12px', fontWeight: 400, color: BRAND_RED }}>
                {pickError}
              </span>
            </div>
          )}

          {/* Tone grid / empty state / sign-in prompt */}
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

          {showTrendingFooter &&
            (authenticated ? (
              <div style={{ display: 'flex', justifyContent: 'center', padding: '48px 24px' }}>
                <BrowseButton onClick={onBrowseTone3000} />
              </div>
            ) : (
              <SignInPrompt heading={DISCOVER_MORE_HEADING} onSignIn={onSignIn} />
            ))}
        </div>
      </div>
    </div>
  );
};
