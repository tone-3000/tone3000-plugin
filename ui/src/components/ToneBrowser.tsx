import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowRight,
  Bookmark,
  Download,
  FolderClosed,
  Search as SearchIcon,
} from './icons';
import type { T3KClient } from '../t3k/tone3000-client';
import { catalogModelCount, type Tone } from '../types/tone';
import { formatCount } from '../t3k/formatCount';
import { timeAgoShort } from '../t3k/timeAgoShort';
import { formatLabel, gearLabel, GEAR_FILTERS } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { GearIcon, ToneImage } from './GearIcon';
import { BusyOverlay, LoadingDots } from './LoadingDots';
import { HELP, helpProps } from './helpText';
import { EdgeFade, EDGE_FADE_WIDTH } from './GalleryLane';
import { useHorizontalWheelScroll } from '../hooks/useHorizontalWheelScroll';
import { CARD_WIDTH } from './chainLayout';
import { T3kMark } from './T3kMark';
import {
  BORDER,
  BRAND_RED,
  DISABLED_OPACITY,
  FONT_MONO,
  GRAY,
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
 * used / Favorites / Created) still need an account; while signed out they
 * show a sign-in prompt instead of erroring. Resolving a picked tone (models
 * + a download token) always needs a session too, so tapping a Trending card
 * while signed out routes into sign-in instead of a picker request that
 * would just fail. The OAuth redirect only ever fires from one of those
 * sign-in CTAs (no-prompt login, reopens this browser) or the header's
 * Browse button (always the full-catalog Select flow). A single gear-type
 * filter (radio-select pills) is shared across every stream so it stays
 * sticky when switching tabs.
 *
 * All queries run client-side against the TONE3000 API; native is only
 * involved for the final load (access token + model download).
 */

type StreamKind = 'trending' | 'downloaded' | 'favorited' | 'created';

const TABS: { id: StreamKind; label: string; icon?: React.ComponentType<{ size?: number }> }[] = [
  { id: 'trending', label: 'Trending' },
  { id: 'downloaded', label: 'Recently used' },
  { id: 'favorited', label: 'Favorites', icon: Bookmark },
  { id: 'created', label: 'Created' },
];

/** Streams that require a signed-in TONE3000 session; Trending is public. */
const GATED_STREAMS = new Set<StreamKind>(['downloaded', 'favorited', 'created']);

const EMPTY_COPY: Record<StreamKind, string> = {
  trending: 'No trending tones right now. Check back soon.',
  downloaded: 'Tones you download on TONE3000 will show up here.',
  favorited: 'Tones you favorite on TONE3000 will show up here.',
  created: 'Tones you upload to TONE3000 will show up here.',
};

const SIGN_IN_HEADING = 'Sign in to see your tones and discover zillions more.';
const DISCOVER_MORE_HEADING = 'Discover zillions more tones.';

const PAGE_SIZE = 12;

/** Remembers the last-viewed stream so the next browse lands on it. */
const STREAM_STORAGE_KEY = 't3k_browser_stream';

/** Content column, same width as the expanded-block card so the browser
    frame lines up with BLOCK visually. Header / tabs / grid / filter pills
    are flush to this edge (like ← BLOCK). */
const COLUMN_MAX_WIDTH = CARD_WIDTH;
const CARD_IMAGE_SIZE = 112;

/** Stream header as tabs: evenly distributed across the full column width
    (each tab flex:1 with its label centered), white text + a full-tab-width
    underline when active, muted otherwise. Bold on every label (Figma Arial
    Bold) so switching tabs doesn't reflow the segment. */
const StreamTabs: React.FC<{ active: StreamKind; onChange: (s: StreamKind) => void }> = ({
  active,
  onChange,
}) => (
  <div role="tablist" style={{ display: 'flex', borderBottom: BORDER }}>
    {TABS.map((tab) => {
      const selected = tab.id === active;
      const Icon = tab.icon;
      return (
        <button
          key={tab.id}
          type="button"
          role="tab"
          aria-selected={selected}
          onClick={() => onChange(tab.id)}
          style={{
            flex: 1,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: '8rem',
            background: 'transparent',
            border: 'none',
            padding: '0 0 12rem',
            marginBottom: '-1rem',
            borderBottom: `2rem solid ${selected ? WHITE : 'transparent'}`,
            color: selected ? WHITE : MUTED,
            fontSize: '14rem',
            fontWeight: 700,
            cursor: 'pointer',
            whiteSpace: 'nowrap',
          }}
        >
          {Icon && <Icon size={15} />}
          {tab.label}
        </button>
      );
    })}
  </div>
);

/** Taller than the default outline pill (`pillButtonStyle`); Browse is the
    single most important action on this screen (the persistent path to the
    full catalog), so it gets a more prominent 40px-tall treatment; the icon
    and mark scale up with it rather than sitting undersized in the extra
    height. */
const browseButtonStyle: React.CSSProperties = {
  ...pillButtonStyle,
  height: '40rem',
  padding: '0 22rem',
  gap: '10rem',
  fontSize: '15rem',
};

/** Outline pill CTA that opens the full-catalog Select flow: the header's
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
      gap: '8rem',
      flexShrink: 0,
      padding: '8rem 16rem',
      fontSize: '14rem',
      fontWeight: 400,
      borderRadius: '9999rem',
      border: active ? `1rem solid ${WHITE}` : BORDER,
      backgroundColor: 'transparent',
      color: active ? WHITE : GRAY,
      cursor: 'pointer',
      whiteSpace: 'nowrap',
    }}
  >
    <GearIcon gear={id} size={20} color={active ? WHITE : GRAY} />
    {label}
  </button>
);

/** Row of radio-select gear filters shared across streams; default is no
    filter (every gear type). Edges fade to black under the same gradient
    scrim as the chain gallery's horizontal scroll (`EdgeFade`). The row
    bleeds EDGE_FADE_WIDTH past the column on each side and re-applies that
    as scroll padding, so at rest the first/last pill sits flush with the
    column edge (outside the fade) and only slides under the gradient once
    actually scrolled. */
const GearFilterRow: React.FC<{ active: string | null; onChange: (id: string | null) => void }> = ({
  active,
  onChange,
}) => {
  const wheelScrollRef = useHorizontalWheelScroll<HTMLDivElement>();
  return (
    <div
      style={{
        position: 'relative',
        marginLeft: `-${EDGE_FADE_WIDTH}rem`,
        marginRight: `-${EDGE_FADE_WIDTH}rem`,
      }}
    >
      <div
        ref={wheelScrollRef}
        role="radiogroup"
        aria-label="Filter by gear type"
        className="hide-scrollbar"
        style={{
          display: 'flex',
          gap: '10rem',
          overflowX: 'auto',
          // overflow-x:auto also clips vertically at the scrollport, and at
          // fractional UI scales the pill height can round a subpixel taller
          // than the row, shaving the bottom border. The vertical padding
          // gives the pills breathing room inside the scrollport; the negative
          // margins cancel it outside so the surrounding layout is unchanged.
          padding: `2rem ${EDGE_FADE_WIDTH}rem`,
          margin: '-2rem 0',
        }}
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
};

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
      gap: '16rem',
      padding: '48rem 24rem',
      textAlign: 'center',
    }}
  >
    <T3kMark height={28} />
    <span style={{ fontSize: '14rem', fontWeight: 400, color: '#ffffff', maxWidth: '420rem' }}>
      {heading}
    </span>
    <button type="button" onClick={onSignIn} style={filledPillButtonStyle}>
      Sign in or create free account
    </button>
  </div>
);

/** Count with a small leading icon (downloads, bookmarks, models). */
const CountStat: React.FC<{ icon: React.ReactNode; value: number }> = ({ icon, value }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: '6rem' }}>
    {icon}
    <span>{formatCount(value)}</span>
  </div>
);

/** The plugin runtime only loads A2 NAM weights, so NAM tones without any A2
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
      borderRadius: '12rem',
      backgroundColor: SURFACE,
      cursor: disabled ? 'not-allowed' : 'pointer',
      opacity: disabled && !loading ? DISABLED_OPACITY : 1,
      boxSizing: 'border-box',
      overflow: 'hidden',
      fontWeight: 400,
      letterSpacing: 'normal',
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      gap: '16rem',
      padding: '12rem',
    }}
  >
    <div
      style={{
        width: `${CARD_IMAGE_SIZE}rem`,
        height: `${CARD_IMAGE_SIZE}rem`,
        borderRadius: '8rem',
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
        gap: '8rem',
        minWidth: 0,
        flex: 1,
        textAlign: 'left',
      }}
    >
      <span
        style={{
          fontSize: '14rem',
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

      <div style={{ display: 'flex', alignItems: 'center', gap: '10rem', minWidth: 0 }}>
        <span
          style={{
            fontSize: '13rem',
            color: MUTED,
            whiteSpace: 'nowrap',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
          }}
        >
          {gearLabel(tone.gear)}
        </span>
        {/* The A2 mark only where the plugin can actually load the tone;
            NAM cards without A2 models render disabled and unmarked. */}
        <FormatBadge
          label={formatLabel(tone.format)}
          a2={tone.format?.toLowerCase() === 'nam' && !isToneUnavailable(tone)}
        />
      </div>

      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          gap: '16rem',
          color: MUTED,
          fontSize: '13rem',
        }}
      >
        <CountStat icon={<Download size={14} />} value={tone.downloads_count ?? 0} />
        <CountStat icon={<Bookmark size={14} />} value={tone.favorites_count ?? 0} />
        <CountStat icon={<FolderClosed size={14} />} value={catalogModelCount(tone)} />
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: '8rem', minWidth: 0 }}>
        <div
          style={{
            width: '22rem',
            height: '22rem',
            borderRadius: '50%',
            overflow: 'hidden',
            flexShrink: 0,
          }}
        >
          <AvatarImage src={tone.user?.avatar_url} alt={tone.user?.username ?? ''} size={22} />
        </div>
        <span
          style={{
            fontSize: '13rem',
            color: MUTED,
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          }}
        >
          {tone.user?.username}
          {tone.published_at && <> · {timeAgoShort(tone.published_at)}</>}
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
    padding: '4rem',
  };

  return (
    <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '8rem' }}>
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
              style={{ padding: '4rem 8rem', color: MUTED, fontSize: '13rem' }}
            >
              …
            </span>
          ) : (
            <button
              key={p}
              onClick={() => onPageChange(p)}
              style={{
                padding: '4rem 12rem',
                borderRadius: '6rem',
                border: p === page ? '1rem solid #ffffff' : BORDER,
                backgroundColor: 'transparent',
                color: p === page ? '#ffffff' : MUTED,
                fontSize: '13rem',
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
   * render after the redirect (before `client` has tokens), so the stream
   * fetch must wait for this to clear or a gated stream fails with
   * not_authenticated.
   */
  authPending?: boolean;
  /** True once the TONE3000 OAuth session has tokens. Gates the three
      session-scoped streams (Recently used / Favorites / Created); Trending
      stays open either way. */
  authenticated: boolean;
  /** Resolve + load a picked tone; the parent closes the browser on success. */
  onPickTone: (toneId: number) => Promise<void>;
  /** Launch the full-catalog Select flow (prompt=select_tone). */
  onBrowseTone3000: () => void;
  /** Run the no-prompt login flow without leaving the browser; fired from
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
    // with not_authenticated and trip the client's re-auth callback); the
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
          if (!cancelled)
            setResult({ data: res.data, page: res.page, totalPages: res.total_pages });
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
            gap: '16rem',
            padding: '48rem 24rem',
            textAlign: 'center',
          }}
        >
          <span style={{ fontSize: '14rem', fontWeight: 400, color: WHITE, maxWidth: '340rem' }}>
            {error}
          </span>
          <button onClick={() => setRetryKey((k) => k + 1)} style={filledPillButtonStyle}>
            Try again
          </button>
        </div>
      );
    }

    if (!result || result.data.length === 0) {
      // First load (nothing to dim yet): dots alone in the empty area.
      if (loading) {
        return (
          <div style={{ display: 'flex', justifyContent: 'center', padding: '64rem 0' }}>
            <LoadingDots />
          </div>
        );
      }
      return (
        <div
          style={{
            padding: '64rem 24rem',
            color: MUTED,
            fontSize: '13rem',
            fontWeight: 400,
            textAlign: 'center',
          }}
        >
          {EMPTY_COPY[stream]}
        </div>
      );
    }

    // Content stays mounted while a new stream/page loads, dimmed and
    // non-interactive under the busy overlay (web `Tones.tsx` pattern).
    return (
      <div style={{ position: 'relative' }}>
        <div
          style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: '16rem',
            pointerEvents: loading ? 'none' : 'auto',
          }}
        >
          {result.data.map((tone) => {
            const unavailable = isToneUnavailable(tone);
            // Trending is viewable while signed out, but resolving a tone
            // (fetching its models + a download token) still needs a
            // session, so route the click through sign-in instead of a picker
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
  // Trending is a fixed top-10 feed and is never paginated.
  const showPaginator =
    !showSignInPrompt && !error && result?.totalPages != null && result.totalPages > 1;
  // Trending always closes with a path to the rest of the catalog: signed
  // out that's a sign-in nudge (picking a tone needs a session anyway);
  // signed in it's just the persistent Browse CTA again, restated here so
  // it's still reachable after scrolling past the top-10 feed. Held back
  // until results land so it never floats alone under the loading dots.
  const showTrendingFooter = stream === 'trending' && !error && !loading;

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
      {/* Header, pinned while the content scrolls beneath it. Matches the
          expanded block card header (back arrow + hairline rule). The tabs
          live in the pinned area too, so only the pills and results scroll
          underneath. An inset black bar covers gear pills as they scroll
          up; it's pulled in from the center-column edges so stereo VU
          meters (which overflow their mono slot into this column) stay
          visible. The inner div re-centers the 800px content column. */}
      <div
        style={{
          position: 'sticky',
          top: 0,
          zIndex: 2,
        }}
      >
        <div
          aria-hidden
          style={{
            position: 'absolute',
            top: 0,
            bottom: 0,
            // Stereo meters overflow ~6px into this column; 12px clears
            // them while still covering the 32px pill-row edge fade.
            left: '12rem',
            right: '12rem',
            backgroundColor: '#000000',
            pointerEvents: 'none',
          }}
        />
        <div
          style={{
            position: 'relative',
            maxWidth: `${COLUMN_MAX_WIDTH}rem`,
            margin: '0 auto',
            width: '100%',
          }}
        >
          <div
            style={{
              display: 'flex',
              // Top-align with the band (Browse is taller); arrow + label stay
              // centered on each other inside the back button.
              alignItems: 'flex-start',
              gap: '16rem',
              // Top inset comes from Plugin's shared 24px middle-band pad.
              // No side inset; flush with the 800px column like ← BLOCK.
              padding: '0 0 16rem',
            }}
          >
            <button
              type="button"
              onClick={onClose}
              {...helpProps(HELP.closeToneBrowser)}
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '16rem',
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
                  fontFamily: FONT_MONO,
                  fontSize: '16rem',
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
      </div>

      {/* Scrolling content; 24px bottom pad so the paginator / last row
          has air above the faceplate (Select fills the center column to
          the faceplate; this pad lives in the scroll content, not the
          shared meter band). */}
      <div style={{ maxWidth: `${COLUMN_MAX_WIDTH}rem`, margin: '0 auto', width: '100%' }}>
        <div style={{ padding: '20rem 0 24rem' }}>
          {!showSignInPrompt && (
            <GearFilterRow active={gearFilter} onChange={handleGearFilterChange} />
          )}

          {pickError && (
            <div style={{ marginTop: '16rem' }}>
              <span style={{ fontSize: '12rem', fontWeight: 400, color: BRAND_RED }}>
                {pickError}
              </span>
            </div>
          )}

          {/* Tone grid / empty state / sign-in prompt */}
          <div style={{ marginTop: '24rem', marginBottom: showPaginator ? '16rem' : 0 }}>
            {body}
          </div>

          {showPaginator && (
            <div
              style={{
                display: 'flex',
                justifyContent: 'flex-end',
                pointerEvents: loading ? 'none' : 'auto',
                opacity: loading ? DISABLED_OPACITY : 1,
              }}
            >
              <Paginator page={page} totalPages={result!.totalPages!} onPageChange={setPage} />
            </div>
          )}

          {showTrendingFooter &&
            (authenticated ? (
              <div style={{ display: 'flex', justifyContent: 'center', padding: '48rem 24rem' }}>
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
