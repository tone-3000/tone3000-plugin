import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowLeftRight,
  Check,
  Equal,
  Power,
  RotateCcw,
  Share,
  Trash2,
} from 'lucide-react';
import { ToneImage } from './GearIcon';
import { KnobControl } from './KnobControl';
import { gainDbScale } from './knobScale';
import { LoadingDots } from './LoadingDots';
import { ModelSelect } from './ModelSelect';
import { RetryLoadBadge } from './RetryLoadBadge';
import { BlockMeter } from './BlockMeter';
import { BlockEqView } from './BlockEqView';
import type { EqViewMode } from './BlockEqView';
import { meterId } from '../hooks/useMeters';
import { useChainActions } from '../hooks/useChainActions';
import type { BlockParamName, ToneBlock } from '../types/chain';
import type { Model } from '../types/tone';
import { isEqFlat } from '../types/chain';
import { CARD_WIDTH, CARD_HEIGHT } from './chainLayout';
import { formatLabel, gearLabel } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { HELP, helpProps } from './helpText';
import { useBlockNormalizeControlEnabled } from './uiPreferences';
import { KNOB_CENTER_OFFSET } from './SpreadControls';
import {
  ACTIVE_OUTLINE,
  BORDER,
  GRAY,
  HIGHLIGHT,
  MUTED,
  iconButtonStyle,
} from './theme';

const HEADER_HEIGHT = 40;
const IMAGE_SIZE = 224;
const KNOB_SIZE = 36;
/** Mini meter height in the side rails (meter sits centered above its knob). */
const RAIL_METER_HEIGHT = 180;

const headerButtonStyle = iconButtonStyle(24);

/** EQ menu glyphs, drawn for legibility at header size (two clean faders /
    one bell curve with its drag dot) rather than generic icon-set art.
    `currentColor` + 1.5 stroke + round caps to match Lucide at this size. */
const EqSlidersIcon: React.FC = () => (
  <svg
    width={14}
    height={14}
    viewBox="0 0 14 14"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
  >
    <line x1={4.5} y1={1.5} x2={4.5} y2={12.5} />
    <line x1={9.5} y1={1.5} x2={9.5} y2={12.5} />
    <circle cx={4.5} cy={9} r={2} fill="currentColor" stroke="none" />
    <circle cx={9.5} cy={4.5} r={2} fill="currentColor" stroke="none" />
  </svg>
);

const EqCurveIcon: React.FC = () => (
  <svg
    width={16}
    height={14}
    viewBox="0 0 16 14"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
  >
    <path d="M1 11 C5 11 5.5 3 8 3 C10.5 3 11 11 15 11" />
    <circle cx={8} cy={3} r={1.8} fill="currentColor" stroke="none" />
  </svg>
);

/** Segmented button group in the card header (matches the LITE/FULL control). */
const headerGroupStyle: React.CSSProperties = {
  display: 'flex',
  flexDirection: 'row',
  height: '24px',
  borderRadius: '6px',
  border: BORDER,
  overflow: 'hidden',
  flexShrink: 0,
};

const headerGroupButtonStyle: React.CSSProperties = {
  width: '28px',
  height: '100%',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  border: 'none',
  cursor: 'pointer',
  backgroundColor: 'transparent',
  padding: 0,
};

interface ChainBlockProps {
  block: ToneBlock;
  /** Host sample rate, for the EQ curve math. */
  sampleRate: number;
  /** Return to the chain gallery (back arrow lives in this card's header). */
  onBack: () => void;
}

/** The detail card (full block view). All mutations come from the
    ChainActions context; only the block itself and the sample rate arrive
    as props. */
export const ChainBlock: React.FC<ChainBlockProps> = ({ block, sampleRate, onBack }) => {
  const { blockId, tone, params } = block;
  const actions = useChainActions();

  // Optional (=) normalization toggle, revealed by the Advanced setting.
  const showNormalizeControl = useBlockNormalizeControlEnabled();

  // Optimistic local values for the controls; native converges via polling.
  const [enabled, setEnabled] = useState(params.enabled);
  const [normalizeOn, setNormalizeOn] = useState(params.normalize ?? true);
  const [inputGain, setInputGain] = useState(params.inputGain ?? 0.5);
  const [outputGain, setOutputGain] = useState(params.outputGain ?? 0.5);
  const [mix, setMix] = useState(params.mix ?? 1.0);
  const [slimmableSize, setSlimmableSize] = useState(params.namSlimmableSize ?? 1);
  const [isSwitchingModel, setIsSwitchingModel] = useState(false);
  const [showEq, setShowEq] = useState(false);
  const [eqView, setEqView] = useState<EqViewMode>('sliders');
  // Optimistic EQ power/position state (native converges via polling, like
  // `enabled`).
  const [eqOn, setEqOn] = useState(params.eq?.enabled ?? true);
  const [eqPre, setEqPre] = useState(params.eq?.pre ?? false);
  const [copied, setCopied] = useState(false);
  const copiedTimeoutRef = useRef<number | undefined>(undefined);
  // True while one of this card's knobs is grabbed — knob prop syncs pause
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
  useEffect(
    () => setSlimmableSize(params.namSlimmableSize ?? 1),
    [blockId, params.namSlimmableSize]
  );
  useEffect(() => () => window.clearTimeout(copiedTimeoutRef.current), []);

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

  const handleShare = useCallback(async () => {
    if (await actions.shareBlock(block)) {
      setCopied(true);
      window.clearTimeout(copiedTimeoutRef.current);
      copiedTimeoutRef.current = window.setTimeout(() => setCopied(false), 1500);
    }
  }, [actions, block]);

  // NAM tier mappers select the bottom tier for values in [0, 0.5) — the
  // boundary itself belongs to the tier above — so LITE must send 0.0, not 0.5.
  const isLite = slimmableSize < 0.5;
  const handleNamSizeMode = useCallback(
    (useLite: boolean) => {
      const size = useLite ? 0.0 : 1.0;
      setSlimmableSize(size);
      setParam('namSlimmableSize', size);
    },
    [setParam]
  );

  // Native persists only the block's *active* model; the full catalog (tones
  // max out at 300 models) is fetched client-side in one call per tone.
  // Signed out the picker is disabled (and the API needs the token anyway).
  const [models, setModels] = useState<Model[]>([]);
  const [modelsLoading, setModelsLoading] = useState(false);

  useEffect(() => {
    if (!actions.authenticated) return;
    let stale = false;
    setModels([]);
    setModelsLoading(true);
    actions
      .listToneModels(tone.id, tone.format)
      .then((list) => {
        if (!stale) setModels(list);
      })
      .catch((err) => console.error('Failed to load models', err))
      .finally(() => {
        if (!stale) setModelsLoading(false);
      });
    return () => {
      stale = true;
    };
  }, [actions, tone.format, tone.id]);

  // Full catalog once loaded; just the active model until then.
  const modelOptions = models.length ? models : tone.models;

  const handleModelSelect = async (id: string) => {
    if (isSwitchingModel) return;
    const newModelId = parseInt(id, 10);
    if (isNaN(newModelId) || newModelId === block.activeModelId) return;

    // Native only stores the active model, so the switch call carries the
    // full model object from the fetched catalog.
    const model = models.find((m) => m.id === newModelId);
    if (!model) return;

    setIsSwitchingModel(true);
    try {
      await actions.switchModel(blockId, newModelId, model);
    } finally {
      setIsSwitchingModel(false);
    }
  };

  // A model download/prepare is in flight (switch, swap or first load). The
  // previous model keeps playing during a switch (`loaded` stays true), so
  // loading affordances key off `modelLoading`, not `loaded`.
  const modelBusy = block.modelLoading || (!block.loaded && !block.loadFailed);
  // LITE/FULL is inert while a model is in flight or nothing is loaded
  // (failed load): stable look, not-allowed cursor, clicks no-op.
  const namSizeLocked = modelBusy || !block.loaded;

  const isNam = tone.format?.toLowerCase() === 'nam';
  // Reverb-style IRs (gear "space"/"pedal") load half wet by default (native
  // sets it in parseToneForLoading); Alt-click reset on Mix must agree.
  const isReverbIr = !isNam && ['space', 'pedal'].includes(tone.gear?.toLowerCase() ?? '');
  const defaultMix = isReverbIr ? 0.5 : 1;
  // All NAM blocks are A2, so the badge is just the format name.
  const formatBadge = formatLabel(tone.format);

  // Picker's "n/N" total from the tone metadata (A2-only for NAM — that's
  // all the plugin loads).
  const modelsTotal = isNam ? tone.a2_models_count : tone.models_count;

  // EQ is shaping this block's audio: powered on and not flat (a flat or
  // bypassed EQ is skipped natively). Uses the optimistic power state so the
  // header glow reacts to the toggle immediately.
  const eqActive = eqOn && params.eq ? !isEqFlat(params.eq) : false;

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        position: 'relative',
        width: `${CARD_WIDTH}px`,
        height: `${CARD_HEIGHT}px`,
        boxSizing: 'border-box',
      }}
    >
      {/* Header — a plain row separated from the body by a hairline rule. */}
      <div
        style={{
          height: `${HEADER_HEIGHT}px`,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          gap: '24px',
          padding: '0 0 16px',
          boxSizing: 'border-box',
          borderBottom: BORDER,
        }}
      >
        <button
          onClick={onBack}
          {...helpProps(HELP.backToChain)}
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          <ArrowLeft size={18} />
        </button>

        <button
          onClick={handleToggleEnabled}
          {...helpProps(HELP.blockPower)}
          style={{
            ...headerButtonStyle,
            color: enabled ? '#ffffff' : GRAY,
            backgroundColor: enabled ? 'transparent' : HIGHLIGHT,
          }}
        >
          <Power size={14} />
        </button>

        {/* LITE/FULL for every NAM block (architecture=2 = always A2).
            Subtle segmented control: the active side just reads white.
            While a model loads the control keeps its normal look (no
            opacity flicker) — clicks just no-op behind a not-allowed
            cursor until the new engine is in. */}
        {isNam && (
          <div
            style={{
              display: 'flex',
              flexDirection: 'row',
              alignItems: 'center',
              height: '24px',
              borderRadius: '4px',
              // Same grey as the model select bar so the header controls match.
              backgroundColor: 'rgba(120, 120, 128, 0.36)',
              overflow: 'hidden',
              flexShrink: 0,
            }}
          >
            <button
              type="button"
              onClick={() => !namSizeLocked && handleNamSizeMode(true)}
              {...helpProps(HELP.namLite)}
              style={{
                height: '100%',
                display: 'flex',
                alignItems: 'center',
                padding: '0 4px 0 10px',
                fontSize: '11px',
                fontWeight: 400,
                fontFamily: 'monospace',
                border: 'none',
                cursor: namSizeLocked ? 'not-allowed' : 'pointer',
                backgroundColor: 'transparent',
                color: isLite ? '#ffffff' : MUTED,
                transition: 'color 0.15s ease',
              }}
            >
              LITE
            </button>
            <button
              type="button"
              onClick={() => !namSizeLocked && handleNamSizeMode(false)}
              {...helpProps(HELP.namFull)}
              style={{
                height: '100%',
                display: 'flex',
                alignItems: 'center',
                padding: '0 10px 0 4px',
                fontSize: '11px',
                fontWeight: 400,
                fontFamily: 'monospace',
                border: 'none',
                cursor: namSizeLocked ? 'not-allowed' : 'pointer',
                backgroundColor: 'transparent',
                color: !isLite ? '#ffffff' : MUTED,
                transition: 'color 0.15s ease',
              }}
            >
              FULL
            </button>
          </div>
        )}

        <div style={{ flex: 1, minWidth: 0 }} />

        {/* EQ menu — lives here (not floating over the grid) while the EQ
            editor is open: view switcher, then reset + power. */}
        {showEq && (
          <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
            <div style={headerGroupStyle}>
              <button
                onClick={() => setEqView('sliders')}
                {...helpProps(HELP.eqSlidersView)}
                style={{
                  ...headerGroupButtonStyle,
                  color: eqView === 'sliders' ? '#ffffff' : MUTED,
                  backgroundColor: eqView === 'sliders' ? HIGHLIGHT : 'transparent',
                }}
              >
                <EqSlidersIcon />
              </button>
              <button
                onClick={() => setEqView('graph')}
                {...helpProps(HELP.eqCurveView)}
                style={{
                  ...headerGroupButtonStyle,
                  borderLeft: BORDER,
                  color: eqView === 'graph' ? '#ffffff' : MUTED,
                  backgroundColor: eqView === 'graph' ? HIGHLIGHT : 'transparent',
                }}
              >
                <EqCurveIcon />
              </button>
            </div>
            {/* PRE moves the EQ before the block's model (after In Gain);
                off = the default post-block position. Same two-state look
                as the EQ toggle: outline + white text while engaged. */}
            <button
              onClick={handleToggleEqPre}
              {...helpProps(HELP.eqPre)}
              style={{
                ...headerButtonStyle,
                width: 'auto',
                padding: '0 8px',
                fontSize: '11px',
                fontWeight: 400,
                fontFamily: 'monospace',
                border: eqPre ? ACTIVE_OUTLINE : BORDER,
                color: eqPre ? '#ffffff' : MUTED,
                backgroundColor: eqPre ? HIGHLIGHT : 'transparent',
              }}
            >
              PRE
            </button>
            {/* Reset and power stand alone (no bordered group). */}
            <button
              onClick={() => actions.resetBlockEq(blockId)}
              {...helpProps(HELP.eqReset)}
              style={{ ...headerButtonStyle, border: 'none', color: '#ffffff' }}
            >
              <RotateCcw size={14} />
            </button>
            <button
              onClick={handleToggleEqEnabled}
              {...helpProps(HELP.eqPower)}
              style={{
                ...headerButtonStyle,
                border: 'none',
                color: eqOn ? '#ffffff' : GRAY,
                backgroundColor: eqOn ? 'transparent' : HIGHLIGHT,
              }}
            >
              <Power size={14} />
            </button>
          </div>
        )}

        {/* EQ view toggle. Two independent signals (see theme.ts patterns):
            white text + bright outline = the EQ is shaping audio (on and
            non-flat), even when the editor is closed; grey fill = the
            editor panel is currently open (like the tuner toggle). */}
        <button
          onClick={() => setShowEq((prev) => !prev)}
          {...helpProps(HELP.eqToggle)}
          style={{
            ...headerButtonStyle,
            width: 'auto',
            padding: '0 8px',
            fontSize: '11px',
            fontWeight: 400,
            fontFamily: 'monospace',
            border: eqActive ? ACTIVE_OUTLINE : BORDER,
            color: eqActive || showEq ? '#ffffff' : MUTED,
            backgroundColor: showEq ? HIGHLIGHT : 'transparent',
          }}
        >
          EQ
        </button>

        <button
          onClick={handleShare}
          {...helpProps(HELP.shareTone)}
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          {copied ? <Check size={14} /> : <Share size={14} />}
        </button>
        <button
          onClick={() => actions.swapBlock(blockId)}
          {...helpProps(HELP.swapTone)}
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          <ArrowLeftRight size={14} />
        </button>
        <button
          onClick={() => actions.removeBlock(blockId)}
          {...helpProps(HELP.removeBlock)}
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          <Trash2 size={14} />
        </button>
      </div>

      {/* Body */}
      <div
        style={{
          flex: 1,
          minHeight: 0,
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'stretch',
          // The EQ grid bleeds edge-to-edge; the normal view only breathes
          // below the header rule (no side/bottom gutters — the card has no
          // border or background to inset from).
          gap: showEq ? 0 : '24px',
          padding: showEq ? 0 : '16px 0 0',
          boxSizing: 'border-box',
          opacity: enabled ? 1 : 0.45,
          transition: 'opacity 0.2s ease',
          // Keep the body on its own pixel-snapped compositor layer so the
          // opacity fade (power toggle) can't promote/demote a temporary layer
          // that nudges inner content — notably the scaled EQ SVG — by a pixel.
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
            {/* Input rail: knob pinned at the bottom, meter centered in the
                space between it and the card header */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                flexShrink: 0,
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
                size={KNOB_SIZE}
                labelSize={12}
                labelBottom={false}
                innerColor="#000000"
                scale={gainDbScale}
                defaultValue={0.5}
                help={HELP.blockIn}
              />
            </div>

            {/* Center: image + tone info on top, model picker spanning the full
            width below (it extends under the image, not bound to the info
            column). */}
            <div
              style={{
                flex: 1,
                minWidth: 0,
                display: 'flex',
                flexDirection: 'column',
                justifyContent: 'space-between',
                gap: '22px',
              }}
            >
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
                  // Copy centers on the image's vertical middle, web-style.
                  alignItems: 'center',
                  gap: '24px',
                  minWidth: 0,
                }}
              >
                {/* Tone image (gear glyph fallback, like the web's ToneCard) */}
                <div
                  style={{
                    position: 'relative',
                    width: IMAGE_SIZE,
                    height: IMAGE_SIZE,
                    borderRadius: '8px',
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
                      boxSize={IMAGE_SIZE}
                    />
                  </div>
                  {/* Loading dots while a model downloads/prepares (mirrors
                      the gallery tile); if it failed (e.g. a model switch
                      while offline), the retry affordance instead — the
                      switch is driven from this card, so the failure must
                      be visible in place. */}
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

                {/* Tone info */}
                <div
                  style={{
                    display: 'flex',
                    flexDirection: 'column',
                    gap: '6px',
                    minWidth: 0,
                    flex: 1,
                  }}
                >
                  <span
                    style={{
                      fontSize: '18px',
                      color: '#ffffff',
                      fontWeight: '600',
                      // Two-line clamp with trailing ellipsis.
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
                      gap: '12px',
                    }}
                  >
                    {tone.gear && (
                      <span style={{ fontSize: '14px', color: MUTED, fontWeight: '400' }}>
                        {gearLabel(tone.gear)}
                      </span>
                    )}
                    {formatBadge && <FormatBadge label={formatBadge} />}
                  </div>

                  {tone.user && (
                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                      <div
                        style={{
                          width: '23px',
                          height: '23px',
                          borderRadius: '50%',
                          overflow: 'hidden',
                          flexShrink: 0,
                        }}
                      >
                        <AvatarImage
                          src={tone.user.avatar_url}
                          alt={tone.user.username}
                          size={23}
                        />
                      </div>
                      <span style={{ fontSize: '14px', color: '#ffffff', fontWeight: '400' }}>
                        {tone.user.username}
                      </span>
                    </div>
                  )}
                </div>
              </div>

              {/* Switching models re-downloads through native with a Bearer
                  token, so the picker is inert while signed out. The wrapper
                  carries the cursor + hint — the select itself is
                  pointer-events: none when disabled. */}
              <div
                {...(!actions.authenticated ? helpProps(HELP.modelSelectSignedOut) : {})}
                style={{ cursor: actions.authenticated ? 'default' : 'not-allowed' }}
              >
                <ModelSelect
                  options={modelOptions.map((m) => ({ id: String(m.id), name: m.name }))}
                  value={String(block.activeModelId)}
                  onChange={handleModelSelect}
                  height={36}
                  disabled={!actions.authenticated}
                  loading={modelsLoading}
                  totalCount={modelsTotal}
                />
              </div>
            </div>

            {/* Mix knob: bottom aligned, between the model select and the output rail */}
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
                size={KNOB_SIZE}
                labelSize={12}
                labelBottom={false}
                innerColor="#000000"
                defaultValue={defaultMix}
                help={HELP.blockMix}
              />
            </div>

            {/* Output rail: knob pinned at the bottom, meter centered in the
                space between it and the card header. Per-block normalization
                (=) sits left of Out — same placement language as the faceplate
                auto-balance (=) beside Bal. NAM only, revealed by Advanced. */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                flexShrink: 0,
              }}
            >
              <div style={{ flex: 1, display: 'flex', alignItems: 'center', minHeight: 0 }}>
                <BlockMeter meterId={meterId.blockOut(blockId)} length={RAIL_METER_HEIGHT} />
              </div>
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
                  alignItems: 'flex-end',
                  gap: '10px',
                }}
              >
                {isNam && showNormalizeControl && (
                  <button
                    onClick={handleToggleNormalize}
                    {...helpProps(HELP.blockNormalize)}
                    style={{
                      width: '18px',
                      height: '18px',
                      borderRadius: '5px',
                      border: normalizeOn ? ACTIVE_OUTLINE : BORDER,
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      cursor: 'pointer',
                      padding: 0,
                      flexShrink: 0,
                      boxSizing: 'border-box',
                      color: normalizeOn ? '#ffffff' : GRAY,
                      backgroundColor: normalizeOn ? HIGHLIGHT : 'transparent',
                      transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
                    }}
                  >
                    <Equal size={12} />
                  </button>
                )}
                <KnobControl
                  label="Out"
                  value={outputGain}
                  onChange={(val) => {
                    setOutputGain(val);
                    setParam('outputGain', val);
                  }}
                  onDragStateChange={handleKnobDragState}
                  size={KNOB_SIZE}
                  labelSize={12}
                  labelBottom={false}
                  innerColor="#000000"
                  scale={gainDbScale}
                  defaultValue={0.5}
                  help={isNam ? HELP.blockOut : HELP.blockOutIr}
                />
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
};
