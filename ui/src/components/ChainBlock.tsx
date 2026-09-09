import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowLeftRight,
  Bookmark,
  Download,
  Equal,
  FolderClosed,
  Gauge,
  Info,
  Power,
  Share,
  Trash2,
} from './icons';
import { ToneImage } from './GearIcon';
import { rem } from '../hooks/useUiScale';
import { KnobControl } from './KnobControl';
import { gainDbScale } from './knobScale';
import { BusyOverlay, LoadingDots } from './LoadingDots';
import { ModelSelect } from './ModelSelect';
import { RetryLoadBadge } from './RetryLoadBadge';
import { BlockMeter } from './BlockMeter';
import { BlockEqView } from './BlockEqView';
import type { EqViewMode } from './BlockEqView';
import { BlockInfoPanel } from './BlockInfoPanel';
import { meterId } from '../hooks/useMeters';
import { useChainActions } from '../hooks/useChainActions';
import { useParameter } from '../hooks/useParameter';
import type { BlockParamName, ToneBlock } from '../types/chain';
import { catalogModelCount, type Model, type Tone } from '../types/tone';
import { isEqFlat, isSlimSizeFull, SLIM_SIZE_FULL, SLIM_SIZE_LITE } from '../types/chain';
import {
  CARD_WIDTH,
  CARD_HEIGHT,
  CARD_RADIUS,
  HEADER_HEIGHT,
  BODY_HEIGHT,
  BODY_PADDING,
} from './chainLayout';
import { formatCount } from '../t3k/formatCount';
import { timeAgoShort } from '../t3k/timeAgoShort';
import { formatLabel, gearLabel } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { HELP, helpProps } from './helpText';
import { useBlockNormalizeControlEnabled, useBlockSizeControlEnabled } from './uiPreferences';
import { useToast } from './Toast';
import { ChromeIconButton, ChromeTextButton, chromeIcon } from './ChromeIconButton';
import { T3K_API } from '../t3k/config';
import {
  BORDER,
  GRAY,
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_SECONDARY,
  FONT_MONO,
  MUTED,
  SEGMENTED_TRACK,
  WHITE,
  segmentedCellStyle,
  segmentedGroupStyle,
  uiOffClass,
} from './theme';

/** Tone image; matches the Figma detail mock (fits body with model select). */
const IMAGE_SIZE = 192;
/** Info view artwork; Figma detail mock is 160 beside the metadata column. */
const IMAGE_SIZE_INFO = 160;
/** Mini meter height in the side rails (meter sits centered above its knob). */
const RAIL_METER_HEIGHT = 160;
/** Centers the normalize (=) chrome box on the Out knob. */
const NORMALIZE_BUTTON_OFFSET = -(KNOB_SIZE_SECONDARY - ICON_BOX_SIZE) / 2;

/** Downloads / bookmarks / models count with a leading icon (same pattern as ToneBrowser). */
const CountStat: React.FC<{ icon: React.ReactNode; value: number }> = ({ icon, value }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: '8rem' }}>
    <span style={{ display: 'grid', placeItems: 'center', color: GRAY }}>{icon}</span>
    <span style={{ fontSize: '14rem', fontWeight: 400, color: MUTED }}>{formatCount(value)}</span>
  </div>
);

/** Bookmark tally. Signed-in: a toggle (outline idle, white fill when favorited). */
const BookmarkStat: React.FC<{
  value: number;
  favorited: boolean;
  onToggle?: () => void;
}> = ({ value, favorited, onToggle }) => {
  const icon = (
    <Bookmark size={16} fill={favorited ? WHITE : 'none'} color={favorited ? WHITE : GRAY} />
  );
  const body = (
    <>
      <span style={{ display: 'grid', placeItems: 'center' }}>{icon}</span>
      <span style={{ fontSize: '14rem', fontWeight: 400, color: MUTED }}>{formatCount(value)}</span>
    </>
  );
  const row: React.CSSProperties = {
    display: 'flex',
    alignItems: 'center',
    gap: '8rem',
  };
  if (!onToggle) return <div style={row}>{body}</div>;
  return (
    <button
      type="button"
      onClick={onToggle}
      {...helpProps(favorited ? HELP.unfavoriteTone : HELP.favoriteTone)}
      style={{
        ...row,
        background: 'transparent',
        border: 'none',
        padding: 0,
        cursor: 'pointer',
        color: 'inherit',
      }}
    >
      {body}
    </button>
  );
};

/** EQ view glyphs: 16×16, stroke inherits selected/muted color. */
const EqSlidersIcon: React.FC = () => (
  <svg
    viewBox="0 0 16 16"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.33333}
    strokeLinecap="round"
    strokeLinejoin="round"
    style={{ width: rem(16), height: rem(16), display: 'block', flexShrink: 0 }}
  >
    <path d="M11.3333 6.66669V12.6667" />
    <path d="M4.66675 3.33331V9.33331" />
    <path d="M13.3333 4.66669C13.3333 3.56212 12.4378 2.66669 11.3333 2.66669C10.2287 2.66669 9.33325 3.56212 9.33325 4.66669C9.33325 5.77126 10.2287 6.66669 11.3333 6.66669C12.4378 6.66669 13.3333 5.77126 13.3333 4.66669Z" />
    <path d="M6.66675 11.3333C6.66675 10.2287 5.77132 9.33331 4.66675 9.33331C3.56218 9.33331 2.66675 10.2287 2.66675 11.3333C2.66675 12.4379 3.56218 13.3333 4.66675 13.3333C5.77132 13.3333 6.66675 12.4379 6.66675 11.3333Z" />
  </svg>
);

const EqCurveIcon: React.FC = () => (
  <svg
    viewBox="0 0 16 16"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
    style={{ width: rem(16), height: rem(16), display: 'block', flexShrink: 0 }}
  >
    <path d="M1 13.5C5 13.5 5.5 2.5 8 2.5C10.5 2.5 11 13.5 15 13.5" />
  </svg>
);

/** NAM A2 size chrome next to the power button. With the Settings "choose
    per block" preference on, the LITE/FULL segmented toggle; off, a
    read-only chip of the block's size styled like the toggle's unselected
    side, so it reads as the same control, locked (its help text points at
    the setting). The caller skips rendering entirely when there's nothing
    to flag (chip mode with the block at the new-block default). */
const BlockSizeControl: React.FC<{
  full: boolean;
  interactive: boolean;
  onChange: (full: boolean) => void;
}> = ({ full, interactive, onChange }) => {
  if (!interactive) {
    return (
      <div
        {...helpProps(HELP.blockSizeChip)}
        style={{ ...segmentedGroupStyle(), cursor: 'default' }}
      >
        <span style={{ ...segmentedCellStyle(), cursor: 'default', color: MUTED }}>
          <span className="cap-trim">{full ? 'FULL' : 'LITE'}</span>
        </span>
      </div>
    );
  }
  return (
    <div {...helpProps(HELP.blockSize)} style={segmentedGroupStyle()}>
      {([false, true] as const).map((isFull) => (
        <button
          key={String(isFull)}
          type="button"
          onClick={() => onChange(isFull)}
          style={{
            ...segmentedCellStyle(),
            color: full === isFull ? WHITE : MUTED,
            transition: 'color 0.15s ease',
          }}
        >
          <span className="cap-trim">{isFull ? 'FULL' : 'LITE'}</span>
        </button>
      ))}
    </div>
  );
};

interface ChainBlockProps {
  block: ToneBlock;
  /** Another enabled+loaded NAM after this block in its lane. With input
      calibration on, such a block hands off at calibrated output level
      instead of normalizing (see the post-model gain stage in
      Processor.cpp); drives the normalize control's overridden state. */
  namDownstream: boolean;
  /** Host sample rate, for the EQ curve math. */
  sampleRate: number;
  /** Default NAM A2 size for new blocks (ChainState.namSlimSizeDefault);
      the read-only size chip only shows when this block differs from it. */
  namSlimSizeDefault: number;
  /** Return to the chain gallery (← BLOCK sits above the bordered card). */
  onBack: () => void;
  /** Info view fills the center column to the faceplate (Select Tone pattern). */
  onFillToFaceplate?: (fill: boolean) => void;
}

/** The detail card (full block view). All mutations come from the
    ChainActions context; only the block itself and the sample rate arrive
    as props. */
export const ChainBlock: React.FC<ChainBlockProps> = ({
  block,
  namDownstream,
  sampleRate,
  namSlimSizeDefault,
  onBack,
  onFillToFaceplate,
}) => {
  const { blockId, tone, params } = block;
  const actions = useChainActions();

  // Optional (=) normalization toggle, revealed by Per-Block Normalization
  // in Plugin Settings.
  const showNormalizeControl = useBlockNormalizeControlEnabled();
  // Whether the header's NAM size chrome is the LITE/FULL toggle or the
  // read-only chip (the "choose per block" setting).
  const sizeControlEnabled = useBlockSizeControlEnabled();

  // Optimistic local values for the controls; native converges via polling.
  const [enabled, setEnabled] = useState(params.enabled);
  const [normalizeOn, setNormalizeOn] = useState(params.normalize ?? true);
  const [slimFull, setSlimFull] = useState(isSlimSizeFull(params.slimSize ?? SLIM_SIZE_LITE));
  const [inputGain, setInputGain] = useState(params.inputGain ?? 0.5);
  const [outputGain, setOutputGain] = useState(params.outputGain ?? 0.5);
  const [mix, setMix] = useState(params.mix ?? 1.0);
  const [isSwitchingModel, setIsSwitchingModel] = useState(false);
  const [showEq, setShowEq] = useState(false);
  const [showInfo, setShowInfo] = useState(false);
  const [infoTone, setInfoTone] = useState<Tone | null>(null);
  const [infoLoading, setInfoLoading] = useState(false);
  const [infoError, setInfoError] = useState<string | null>(null);
  // Optimistic favorite while the PUT/DELETE is in flight; native's
  // is_favorite / favorites_count catch up via refreshToneMetadata.
  const [favoriteOverride, setFavoriteOverride] = useState<{
    on: boolean;
    count: number;
  } | null>(null);
  const favoriteBusyRef = useRef(false);
  const [eqView, setEqView] = useState<EqViewMode>('sliders');
  // Optimistic EQ power/position state (native converges via polling, like
  // `enabled`).
  const [eqOn, setEqOn] = useState(params.eq?.enabled ?? true);
  const [eqPre, setEqPre] = useState(params.eq?.pre ?? false);
  const toast = useToast();
  // True while one of this card's knobs is grabbed; knob prop syncs pause
  // so a stale chain snapshot can't fight the pointer (same pattern as
  // BlockEqView). On release the deferred revision bump resyncs everyone.
  const knobDragRef = useRef(false);
  const handleKnobDragState = useCallback((dragging: boolean) => {
    knobDragRef.current = dragging;
  }, []);

  // Params can change from outside (undo/redo, state restore, other editor
  // window); follow the backend when it reports a new value.
  useEffect(() => setEnabled(params.enabled), [params.enabled]);
  useEffect(() => setNormalizeOn(params.normalize ?? true), [params.normalize]);
  useEffect(
    () => setSlimFull(isSlimSizeFull(params.slimSize ?? SLIM_SIZE_LITE)),
    [params.slimSize]
  );
  useEffect(() => {
    if (!knobDragRef.current) setInputGain(params.inputGain ?? 0.5);
  }, [params.inputGain]);
  useEffect(() => {
    if (!knobDragRef.current) setOutputGain(params.outputGain ?? 0.5);
  }, [params.outputGain]);
  useEffect(() => {
    if (!knobDragRef.current) setMix(params.mix ?? 1.0);
  }, [params.mix]);
  useEffect(() => setEqOn(params.eq?.enabled ?? true), [params.eq?.enabled]);
  useEffect(() => setEqPre(params.eq?.pre ?? false), [params.eq?.pre]);

  const setParam = useCallback(
    (param: BlockParamName, value: number | boolean) =>
      actions.setBlockParam(blockId, param, value),
    [actions, blockId]
  );

  const handleToggleEnabled = useCallback(() => {
    setEnabled((prev) => {
      setParam('enabled', !prev);
      return !prev;
    });
  }, [setParam]);

  const handleToggleNormalize = useCallback(() => {
    setNormalizeOn((prev) => {
      setParam('normalize', !prev);
      return !prev;
    });
  }, [setParam]);

  const handleSetSlimFull = useCallback(
    (full: boolean) => {
      setSlimFull(full);
      actions.setBlockSlimSize(blockId, full ? SLIM_SIZE_FULL : SLIM_SIZE_LITE);
    },
    [actions, blockId]
  );

  const handleToggleEqEnabled = useCallback(() => {
    setEqOn((prev) => {
      actions.setBlockEqEnabled(blockId, !prev);
      return !prev;
    });
  }, [actions, blockId]);

  const handleToggleEqPre = useCallback(() => {
    setEqPre((prev) => {
      actions.setBlockEqPre(blockId, !prev);
      return !prev;
    });
  }, [actions, blockId]);

  const handleResetEq = useCallback(() => {
    actions.resetBlockEq(blockId);
  }, [actions, blockId]);

  const handleShare = useCallback(async () => {
    if (await actions.shareBlock(block)) toast.show('Link Copied');
  }, [actions, block, toast]);

  // A drop-loaded local file (or folder of them): no catalog behind it, so
  // the card keeps the sound controls and drops the catalog chrome (share,
  // counts, info). The models are the dropped files themselves; the picker feeds
  // off the tone's own model list.
  const isLocal = tone.local === true;

  // Full catalog metadata: fetched from the TONE3000 API, never written into
  // saved chain/preset state by the UI. One fetch serves two purposes: it
  // pre-warms the info panel (infoTone) and re-hydrates native's stored tone
  // metadata (refreshToneMetadata merges it in and the chainChanged resync
  // updates every view). `background` (the on-expand sync) never touches the
  // loading/error UI, so offline or signed-out use is undisturbed; the
  // foreground path (info panel open) keeps its spinner and retry UI. The
  // seq ref is a stale guard (the models effect's flag, as a counter since
  // retries reuse this fetch): only the newest request may touch state, so a
  // swap mid-flight can't surface the old tone's info.
  const infoFetchSeq = useRef(0);
  const fetchInfo = useCallback(
    async (toneId: number, background = false) => {
      if (!actions.authenticated) return;
      const seq = ++infoFetchSeq.current;
      if (!background) {
        setInfoLoading(true);
        setInfoError(null);
      }
      try {
        const full = await actions.getTone(toneId);
        if (seq !== infoFetchSeq.current) return;
        // A favorite toggle in flight owns the next metadata write.
        if (favoriteBusyRef.current) return;
        setInfoTone(full);
        // Best-effort: native no-ops when nothing changed server-side.
        actions.refreshToneMetadata(JSON.stringify(full));
      } catch (err) {
        if (background) {
          // Silent by design (offline, API down, tone deleted): the cached
          // tone keeps working, and opening the info panel refetches with
          // its own visible error/retry UI.
          console.debug('Tone metadata sync skipped', err);
          return;
        }
        console.error('Failed to load tone info', err);
        if (seq !== infoFetchSeq.current) return;
        setInfoTone(null);
        setInfoError('Failed to load tone details.');
      } finally {
        if (!background && seq === infoFetchSeq.current) setInfoLoading(false);
      }
    },
    [actions]
  );

  const favorited = favoriteOverride?.on ?? infoTone?.is_favorite ?? tone.is_favorite === true;
  const favoritesCount =
    favoriteOverride?.count ?? infoTone?.favorites_count ?? tone.favorites_count ?? 0;

  const handleToggleFavorite = useCallback(async () => {
    if (!actions.authenticated || favoriteBusyRef.current) return;
    const next = !favorited;
    const nextCount = Math.max(0, favoritesCount + (next ? 1 : -1));
    setFavoriteOverride({ on: next, count: nextCount });
    favoriteBusyRef.current = true;
    try {
      await actions.setToneFavorite(tone.id, next);
      const base = infoTone?.id === tone.id ? infoTone : await actions.getTone(tone.id);
      const patched = { ...base, is_favorite: next, favorites_count: nextCount };
      setInfoTone(patched);
      actions.refreshToneMetadata(JSON.stringify(patched));
    } catch (err) {
      console.error('Failed to update favorite', err);
      setFavoriteOverride(null);
    } finally {
      favoriteBusyRef.current = false;
    }
  }, [actions, favorited, favoritesCount, infoTone, tone.id]);

  const handleToggleInfo = useCallback(() => {
    if (showInfo) {
      setShowInfo(false);
      return;
    }
    setShowEq(false);
    setShowInfo(true);
    if (actions.authenticated && infoTone?.id !== tone.id) void fetchInfo(tone.id);
  }, [actions.authenticated, fetchInfo, infoTone?.id, showInfo, tone.id]);

  // Expand and swap both land here (mount / tone identity change): orphan
  // any in-flight fetch, drop the stale payload, then fetch the latest tone
  // in the background (metadata re-sync + info pre-warm). With the info
  // panel already open (swap from the detail view) the fetch runs foreground
  // so its loading/error UI behaves as before. Local tones have no catalog
  // to sync from.
  useEffect(() => {
    infoFetchSeq.current++;
    setInfoTone(null);
    setInfoError(null);
    setInfoLoading(false);
    setFavoriteOverride(null);
    if (!isLocal && actions.authenticated) void fetchInfo(tone.id, !showInfo);
    // Only the tone identity; opening/closing the panel is handleToggleInfo.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tone.id]);

  // Select Tone pattern: drop the meter-band bottom pad while info is open
  // so the card can scroll to the faceplate; restore it on close/unmount.
  useEffect(() => {
    onFillToFaceplate?.(showInfo);
    return () => onFillToFaceplate?.(false);
  }, [showInfo, onFillToFaceplate]);

  // Native persists only the block's *active* model; the full catalog (tones
  // max out at 300 models) is fetched client-side in one call per tone.
  // Signed out the picker is disabled (and the API needs the token anyway).
  const [models, setModels] = useState<Model[]>([]);
  const [modelsLoading, setModelsLoading] = useState(false);

  // Same stale guard as fetchInfo: only the newest request may touch state
  // (retries reuse this fetch, so a flag can't cover it).
  const modelsFetchSeq = useRef(0);
  const fetchModels = useCallback(async () => {
    if (isLocal || !actions.authenticated) return;
    const seq = ++modelsFetchSeq.current;
    setModelsLoading(true);
    try {
      const list = await actions.listToneModels(tone.id, tone.format);
      if (seq === modelsFetchSeq.current) setModels(list);
    } catch (err) {
      // No error UI: the picker keeps the stored model, and opening it
      // retries (handleModelsOpen), so a transient failure never sticks.
      console.error('Failed to load models', err);
    } finally {
      if (seq === modelsFetchSeq.current) setModelsLoading(false);
    }
  }, [actions, isLocal, tone.format, tone.id]);

  // Fetch on mount, tone change, and auth arrival (`actions` carries
  // `authenticated`, so logging in re-runs this with the guard now open).
  useEffect(() => {
    setModels([]);
    void fetchModels();
    return () => {
      // Reading the counter's latest value here is the point (bumping it
      // orphans whatever fetch is in flight), not a stale-closure bug.
      // eslint-disable-next-line react-hooks/exhaustive-deps
      modelsFetchSeq.current++;
    };
  }, [fetchModels]);

  // A failed fetch leaves the catalog at just the stored model; opening the
  // picker retries so the card never strands on "1/N". No-op once loaded.
  const handleModelsOpen = useCallback(() => {
    if (!modelsLoading && models.length === 0) void fetchModels();
  }, [fetchModels, models.length, modelsLoading]);

  // Local tones own their model list; catalog tones show the full catalog
  // once loaded, just the active model until then.
  const modelOptions = isLocal ? tone.models : models.length ? models : tone.models;

  const handleModelSelect = async (id: string) => {
    if (isSwitchingModel) return;
    const newModelId = parseInt(id, 10);
    if (isNaN(newModelId) || newModelId === block.activeModelId) return;

    // Native only stores the active model, so the switch call carries the
    // model object: from the fetched catalog, or the local tone's own list
    // (whose entries ship their stash model_url).
    const model = (isLocal ? tone.models : models).find((m) => m.id === newModelId);
    if (!model?.model_url) return;

    setIsSwitchingModel(true);
    try {
      await actions.switchModel(blockId, newModelId, { ...model, model_url: model.model_url });
    } finally {
      setIsSwitchingModel(false);
    }
  };

  // A model download/prepare is in flight (switch, swap or first load). The
  // previous model keeps playing during a switch (`loaded` stays true), so
  // loading affordances key off `modelLoading`, not `loaded`.
  const modelBusy = block.modelLoading || (!block.loaded && !block.loadFailed);

  const isNam = tone.format?.toLowerCase() === 'nam';

  // Calibration state (the gauge indicator + the normalize override). Only
  // meaningful while the user's input calibration setting is on: the gauge
  // then reads white when the loaded model carries calibration data and gray
  // when it doesn't. The overridden check mirrors the DSP's calibrated
  // hand-off condition exactly (Processor.cpp): calibration on, sane
  // output_level_dbu metadata, and another NAM downstream. The last NAM
  // stays on normalization, so its control never reads overridden.
  const [calibrateInput] = useParameter('calibrateInput', 'toggle');
  const showCalibration = isNam && calibrateInput;
  const calibrationActive = block.inputLevelDbu !== undefined;
  const handOffLevelSane =
    block.outputLevelDbu !== undefined && block.outputLevelDbu >= -60 && block.outputLevelDbu <= 60;
  const normalizeOverridden = isNam && calibrateInput && namDownstream && handOffLevelSane;
  // Long (reverb-like) IRs load half wet by default (native classifies by
  // kernel length and sets the mix on first load); Alt-click reset on Mix
  // must agree.
  const defaultMix = block.irLong ? 0.5 : 1;
  // Every NAM block in the chain is A2 (the browser filters the catalog and
  // local drops are validated), so NAM badges always carry the A2 mark.
  const formatBadge = formatLabel(tone.format);

  // Picker "n/N" and the folder stat: A2 for NAM, models_count for IR.
  const modelsTotal = catalogModelCount(tone);

  // EQ is shaping this block's audio: powered on and not flat (a flat or
  // bypassed EQ is skipped natively). Uses the optimistic power state so the
  // header glow reacts to the toggle immediately.
  const eqActive = eqOn && params.eq ? !isEqFlat(params.eq) : false;

  const tonePageUrl = infoTone?.url || tone.url || `${T3K_API}/tones/${tone.id}`;

  return (
    <div
      className={showInfo ? 'hide-scrollbar' : undefined}
      style={{
        display: 'flex',
        flexDirection: 'column',
        width: `${CARD_WIDTH}rem`,
        height: '100%',
        boxSizing: 'border-box',
        overflowY: showInfo ? 'auto' : 'hidden',
        overflowX: 'hidden',
      }}
    >
      {/* 24px top/bottom pads live in the scroll content so ← BLOCK + card
          can reach the plugin header and faceplate. */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'stretch',
          padding: showInfo ? '24rem 0' : 0,
        }}
      >
        {/* ← BLOCK sits above the bordered card (Figma: 16px mono, gap 16). */}
        <button
          type="button"
          onClick={onBack}
          {...helpProps(HELP.backToChain)}
          style={{
            alignSelf: 'flex-start',
            display: 'flex',
            alignItems: 'center',
            gap: '16rem',
            marginBottom: '16rem',
            flexShrink: 0,
            background: 'transparent',
            border: 'none',
            outline: 'none',
            padding: 0,
            cursor: 'pointer',
            color: WHITE,
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
            Block
          </span>
        </button>

        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            position: 'relative',
            width: '100%',
            height: showInfo ? undefined : `${CARD_HEIGHT}rem`,
            minHeight: `${CARD_HEIGHT}rem`,
            boxSizing: 'border-box',
            border: BORDER,
            borderRadius: `${CARD_RADIUS}rem`,
            overflow: 'hidden',
          }}
        >
          {/* Header: 16px inset, chrome centered in HEADER_HEIGHT. */}
          <div
            style={{
              height: `${HEADER_HEIGHT}rem`,
              flexShrink: 0,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              padding: `0 ${BODY_PADDING}rem`,
              boxSizing: 'border-box',
              borderBottom: BORDER,
            }}
          >
            <div style={{ display: 'flex', alignItems: 'center', gap: '24rem', flexShrink: 0 }}>
              <ChromeIconButton
                tone="power"
                on={enabled}
                help={HELP.blockPower}
                onClick={handleToggleEnabled}
              >
                <Power />
              </ChromeIconButton>

              {/* With per-block choice off, a block matching the new-block
                default has nothing to say: the chip only appears on a
                mismatch (e.g. a preset's FULL block under a lite default). */}
              {isNam && (sizeControlEnabled || slimFull !== isSlimSizeFull(namSlimSizeDefault)) && (
                <BlockSizeControl
                  full={slimFull}
                  interactive={sizeControlEnabled}
                  onChange={handleSetSlimFull}
                />
              )}

              {/* Calibration indicator (not a button): white = the loaded model
                carries calibration data, gray = it doesn't. Hidden entirely
                while the calibration setting is off. */}
              {showCalibration && (
                <span
                  {...helpProps(calibrationActive ? HELP.blockCalibrated : HELP.blockUncalibrated)}
                  style={{
                    width: `${ICON_BOX_SIZE}rem`,
                    height: `${ICON_BOX_SIZE}rem`,
                    display: 'grid',
                    placeItems: 'center',
                    color: calibrationActive ? WHITE : GRAY,
                  }}
                >
                  {chromeIcon(<Gauge />, ICON_SIZE)}
                </span>
              )}
            </div>

            {/* Right cluster: EQ submenu (pill when open), info, then share/swap/trash.
              EQ stays rightmost in the submenu so opening grows left only.
              marginRight cancels the pill's right pad so EQ doesn't shift
              relative to info. */}
            <div style={{ display: 'flex', alignItems: 'center', gap: '24rem', flexShrink: 0 }}>
              <div
                style={{
                  display: 'inline-flex',
                  alignItems: 'center',
                  gap: showEq ? '16rem' : 0,
                  padding: showEq ? '4rem 12rem' : 0,
                  // Pull back by the pill's right pad so EQ stays put vs share.
                  marginRight: showEq ? -12 : 0,
                  borderRadius: showEq ? '100rem' : 0,
                  backgroundColor: showEq ? SEGMENTED_TRACK : 'transparent',
                  flexShrink: 0,
                  boxSizing: 'border-box',
                }}
              >
                {showEq && (
                  <>
                    <ChromeIconButton
                      tone="power"
                      on={eqOn}
                      help={HELP.eqPower}
                      onClick={handleToggleEqEnabled}
                    >
                      <Power />
                    </ChromeIconButton>
                    {/* PRE routes a live EQ; dims and goes inert with the
                        rest of the editor while the EQ is powered off. */}
                    <span
                      className={uiOffClass(!eqOn)}
                      style={{ display: 'inline-flex', transition: 'opacity 0.2s ease' }}
                    >
                      <ChromeTextButton armed={eqPre} help={HELP.eqPre} onClick={handleToggleEqPre}>
                        PRE
                      </ChromeTextButton>
                    </span>
                    <div
                      style={{
                        ...segmentedGroupStyle(),
                        // Nested track, slightly quieter than the outer pill.
                        backgroundColor: 'rgba(118, 118, 128, 0.24)',
                      }}
                    >
                      <button
                        onClick={() => setEqView('sliders')}
                        {...helpProps(HELP.eqSlidersView)}
                        style={{
                          ...segmentedCellStyle(true),
                          color: eqView === 'sliders' ? WHITE : GRAY,
                        }}
                      >
                        <EqSlidersIcon />
                      </button>
                      <button
                        onClick={() => setEqView('graph')}
                        {...helpProps(HELP.eqCurveView)}
                        style={{
                          ...segmentedCellStyle(true),
                          color: eqView === 'graph' ? WHITE : GRAY,
                        }}
                      >
                        <EqCurveIcon />
                      </button>
                    </div>
                    <span
                      className={uiOffClass(!eqOn)}
                      style={{ display: 'inline-flex', transition: 'opacity 0.2s ease' }}
                    >
                      <ChromeTextButton
                        armed={false}
                        help="Reset all EQ bands to default (flat)"
                        onClick={handleResetEq}
                      >
                        FLAT
                      </ChromeTextButton>
                    </span>
                  </>
                )}
                <ChromeTextButton
                  armed={eqActive}
                  open={showEq}
                  help={HELP.eqToggle}
                  onClick={() => {
                    setShowEq((prev) => !prev);
                    setShowInfo(false);
                  }}
                >
                  EQ
                </ChromeTextButton>
              </div>

              {!isLocal && (
                <ChromeIconButton open={showInfo} help={HELP.toneInfo} onClick={handleToggleInfo}>
                  <Info />
                </ChromeIconButton>
              )}
              {!isLocal && (
                <ChromeIconButton help={HELP.shareTone} onClick={handleShare}>
                  <Share />
                </ChromeIconButton>
              )}
              <ChromeIconButton help={HELP.swapTone} onClick={() => actions.swapBlock(blockId)}>
                <ArrowLeftRight />
              </ChromeIconButton>
              <ChromeIconButton
                help={HELP.removeBlock}
                onClick={() => actions.removeBlock(blockId)}
              >
                <Trash2 />
              </ChromeIconButton>
            </div>
          </div>

          {/* Body: tone view uses BODY_PADDING; EQ spectrum/grid bleeds
            edge-to-edge (interactive chrome insets itself). Info view drops
            knobs/model select and lets the right column grow. While the
            block is bypassed the whole body dims and goes inert (uiOffClass);
            the header (power, EQ, info, share, swap, trash) stays live. */}
          <div
            className={uiOffClass(!enabled)}
            style={{
              height: showInfo ? undefined : `${BODY_HEIGHT}rem`,
              flexShrink: 0,
              display: 'flex',
              flexDirection: 'row',
              alignItems: 'stretch',
              gap: showEq || showInfo ? 0 : '24rem',
              padding: showEq
                ? 0
                : showInfo
                  ? `${BODY_PADDING}rem ${BODY_PADDING}rem 24rem`
                  : `${BODY_PADDING}rem`,
              boxSizing: 'border-box',
              position: 'relative',
              transition: 'opacity 0.2s ease',
              // Keep the body on its own pixel-snapped compositor layer so the
              // opacity fade (power toggle) can't promote/demote a temporary layer
              // that nudges inner content (notably the scaled EQ SVG) by a pixel.
              transform: 'translateZ(0)',
              willChange: 'opacity',
            }}
          >
            {showEq ? (
              <BlockEqView
                blockId={blockId}
                bands={params.eq?.bands ?? []}
                eqEnabled={eqOn}
                sampleRate={sampleRate}
                view={eqView}
                onSetBand={actions.setBlockEqBand}
              />
            ) : (
              <>
                {/* Input rail: meter above In knob (Figma: gap 12). */}
                {!showInfo && (
                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      alignItems: 'center',
                      flexShrink: 0,
                      gap: '12rem',
                    }}
                  >
                    <div style={{ flex: 1, display: 'flex', alignItems: 'center', minHeight: 0 }}>
                      <BlockMeter meterId={meterId.blockIn(blockId)} length={RAIL_METER_HEIGHT} />
                    </div>
                    <KnobControl
                      label="In"
                      value={inputGain}
                      onChange={(val) => {
                        setInputGain(val);
                        setParam('inputGain', val);
                      }}
                      onDragStateChange={handleKnobDragState}
                      size={KNOB_SIZE_SECONDARY}
                      labelBottom={false}
                      thumb="secondary"
                      scale={gainDbScale}
                      defaultValue={0.5}
                      help={HELP.blockIn}
                    />
                  </div>
                )}

                {/* Center: image + tone info on top, model picker spanning full width.
                  Info view keeps image + meta and drops the picker / knobs. */}
                <div
                  style={{
                    flex: 1,
                    minWidth: 0,
                    alignSelf: 'stretch',
                    display: 'flex',
                    flexDirection: 'column',
                    justifyContent: showInfo ? 'flex-start' : 'space-between',
                  }}
                >
                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'row',
                      alignItems: showInfo ? 'flex-start' : 'center',
                      gap: '24rem',
                      minWidth: 0,
                    }}
                  >
                    {/* Tone image (gear glyph fallback, like the web's ToneCard) */}
                    <div
                      style={{
                        position: 'relative',
                        width: rem(showInfo ? IMAGE_SIZE_INFO : IMAGE_SIZE),
                        height: rem(showInfo ? IMAGE_SIZE_INFO : IMAGE_SIZE),
                        borderRadius: '8rem',
                        overflow: 'hidden',
                        flexShrink: 0,
                      }}
                    >
                      <div
                        style={{
                          opacity: modelBusy || block.loadFailed ? 0.35 : 1,
                          transition: 'opacity 0.2s ease',
                          width: '100%',
                          height: '100%',
                        }}
                      >
                        <ToneImage
                          src={tone.images?.[0]}
                          alt={tone.title}
                          gear={tone.gear}
                          local={tone.local}
                          boxSize={showInfo ? IMAGE_SIZE_INFO : IMAGE_SIZE}
                        />
                      </div>
                      {(modelBusy || block.loadFailed) && (
                        <div
                          style={{
                            position: 'absolute',
                            inset: 0,
                            display: 'flex',
                            alignItems: 'center',
                            justifyContent: 'center',
                          }}
                        >
                          {block.loadFailed ? (
                            <RetryLoadBadge onRetry={() => actions.retryLoad(blockId)} />
                          ) : (
                            <LoadingDots />
                          )}
                        </div>
                      )}
                    </div>

                    {/* Tone info: title / gear+badge / counts / creator (Figma gaps).
                      Info view appends description / makes / tags under this. */}
                    <div
                      style={{
                        display: 'flex',
                        flexDirection: 'column',
                        gap: showInfo ? '24rem' : '16rem',
                        minWidth: 0,
                        flex: 1,
                      }}
                    >
                      <div
                        style={{
                          display: 'flex',
                          flexDirection: 'column',
                          gap: '16rem',
                          minWidth: 0,
                        }}
                      >
                        <div
                          style={{
                            display: 'flex',
                            flexDirection: 'column',
                            gap: '8rem',
                            minWidth: 0,
                          }}
                        >
                          <span
                            style={{
                              fontSize: '18rem',
                              color: WHITE,
                              fontWeight: 700,
                              lineHeight: 1.4,
                              display: '-webkit-box',
                              WebkitLineClamp: 2,
                              WebkitBoxOrient: 'vertical',
                              overflow: 'hidden',
                            }}
                          >
                            {tone.title}
                          </span>

                          <div
                            style={{
                              display: 'flex',
                              flexDirection: 'row',
                              alignItems: 'center',
                              gap: '16rem',
                            }}
                          >
                            {tone.gear && (
                              <span style={{ fontSize: '14rem', color: MUTED, fontWeight: 400 }}>
                                {gearLabel(tone.gear)}
                              </span>
                            )}
                            {formatBadge && <FormatBadge label={formatBadge} a2={isNam} />}
                          </div>
                        </div>

                        {!isLocal && (
                          <div
                            style={{
                              display: 'flex',
                              flexDirection: 'row',
                              alignItems: 'center',
                              gap: '24rem',
                            }}
                          >
                            <CountStat
                              icon={<Download size={16} />}
                              value={tone.downloads_count ?? 0}
                            />
                            <BookmarkStat
                              value={favoritesCount}
                              favorited={favorited}
                              onToggle={
                                actions.authenticated
                                  ? () => void handleToggleFavorite()
                                  : undefined
                              }
                            />
                            <CountStat icon={<FolderClosed size={16} />} value={modelsTotal ?? 0} />
                          </div>
                        )}

                        {tone.user && (
                          <div style={{ display: 'flex', alignItems: 'center', gap: '12rem' }}>
                            <div
                              style={{
                                width: '32rem',
                                height: '32rem',
                                borderRadius: '50%',
                                overflow: 'hidden',
                                flexShrink: 0,
                              }}
                            >
                              <AvatarImage
                                src={tone.user.avatar_url}
                                alt={tone.user.username}
                                size={32}
                              />
                            </div>
                            <span style={{ fontSize: '14rem', color: GRAY, fontWeight: 400 }}>
                              {tone.user.username}
                              {tone.published_at && (
                                <span style={{ color: MUTED }}>
                                  {' '}
                                  · {timeAgoShort(tone.published_at)}
                                </span>
                              )}
                            </span>
                          </div>
                        )}
                      </div>

                      {showInfo && (
                        <BlockInfoPanel
                          loading={infoLoading}
                          error={infoError}
                          onRetry={() => void fetchInfo(tone.id)}
                          authenticated={actions.authenticated}
                          onLogin={actions.login}
                          tone={infoTone}
                          pageUrl={tonePageUrl}
                        />
                      )}
                    </div>
                  </div>

                  {/* Switching catalog models re-downloads through native with
                  a Bearer token, so the picker is inert while signed out.
                  The wrapper carries the cursor + hint, as the select itself
                  is pointer-events: none when disabled. Local switches read
                  the stash: no auth, and the picker always shows (the
                  dropped file names are the block's provenance). */}
                  {!showInfo && (
                    <div
                      {...(!isLocal && !actions.authenticated
                        ? helpProps(HELP.modelSelectSignedOut)
                        : {})}
                      style={{
                        cursor: isLocal || actions.authenticated ? 'default' : 'not-allowed',
                      }}
                    >
                      <ModelSelect
                        options={modelOptions.map((m) => ({ id: String(m.id), name: m.name }))}
                        value={String(block.activeModelId)}
                        onChange={handleModelSelect}
                        onOpen={handleModelsOpen}
                        height={36}
                        disabled={!isLocal && !actions.authenticated}
                        loading={modelsLoading}
                        totalCount={isLocal ? tone.models.length : modelsTotal}
                      />
                    </div>
                  )}
                </div>

                {/* Mix knob: bottom aligned, between the model select and the output rail */}
                {!showInfo && (
                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      alignItems: 'center',
                      justifyContent: 'flex-end',
                      flexShrink: 0,
                    }}
                  >
                    <KnobControl
                      label="Mix"
                      value={mix}
                      onChange={(val) => {
                        setMix(val);
                        setParam('mix', val);
                      }}
                      onDragStateChange={handleKnobDragState}
                      size={KNOB_SIZE_SECONDARY}
                      labelBottom={false}
                      thumb="secondary"
                      defaultValue={defaultMix}
                      help={HELP.blockMix}
                    />
                  </div>
                )}

                {/* Output rail: meter above Out (+ optional normalize). The rail
                right-aligns and the meter wrapper is knob-wide, so the meter
                stays centered over the Out knob whether or not the normalize
                button widens the bottom row to its left. */}
                {!showInfo && (
                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      alignItems: 'flex-end',
                      flexShrink: 0,
                      gap: '12rem',
                    }}
                  >
                    <div
                      style={{
                        flex: 1,
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        minHeight: 0,
                        width: `${KNOB_SIZE_SECONDARY}rem`,
                      }}
                    >
                      <BlockMeter meterId={meterId.blockOut(blockId)} length={RAIL_METER_HEIGHT} />
                    </div>
                    <div
                      style={{
                        display: 'flex',
                        flexDirection: 'row',
                        alignItems: 'flex-end',
                        gap: '10rem',
                      }}
                    >
                      {isNam && showNormalizeControl && (
                        /* The wrapper carries the vertical nudge and the overridden
                     hint: a disabled button swallows hover (no mouseover ever
                     fires), so pointer-events pass through it to this span
                     and the hint delegation resolves here instead. */
                        <span
                          {...helpProps(
                            normalizeOverridden
                              ? HELP.blockNormalizeOverridden
                              : HELP.blockNormalize
                          )}
                          style={{
                            display: 'inline-flex',
                            transform: `translateY(${NORMALIZE_BUTTON_OFFSET}rem)`,
                            // The button below is pointer-events: none while
                            // overridden, so the cursor reads from here.
                            cursor: normalizeOverridden ? 'not-allowed' : undefined,
                          }}
                        >
                          <ChromeIconButton
                            tone="power"
                            // Overridden reads as off (gray + fill) even if
                            // the stored setting is on.
                            on={normalizeOn && !normalizeOverridden}
                            help={HELP.blockNormalize}
                            onClick={handleToggleNormalize}
                            disabled={normalizeOverridden}
                            style={normalizeOverridden ? { pointerEvents: 'none' } : undefined}
                          >
                            <Equal size={ICON_SIZE} />
                          </ChromeIconButton>
                        </span>
                      )}
                      <KnobControl
                        label="Out"
                        value={outputGain}
                        onChange={(val) => {
                          setOutputGain(val);
                          setParam('outputGain', val);
                        }}
                        onDragStateChange={handleKnobDragState}
                        size={KNOB_SIZE_SECONDARY}
                        labelBottom={false}
                        thumb="secondary"
                        scale={gainDbScale}
                        defaultValue={0.5}
                        help={isNam || block.irLong ? HELP.blockOut : HELP.blockOutIr}
                      />
                    </div>
                  </div>
                )}
              </>
            )}
            {showInfo && infoLoading && <BusyOverlay align="center" />}
          </div>
        </div>
      </div>
    </div>
  );
};
