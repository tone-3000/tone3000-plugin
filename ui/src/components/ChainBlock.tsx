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
import { ChromeIconButton, ChromeTextButton } from './ChromeIconButton';
import {
  BORDER,
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_SECONDARY,
  MUTED,
  segmentedCellStyle,
  segmentedGroupStyle,
} from './theme';

/** Every header control shares ICON_BOX_SIZE; the leftover air below the
    chrome row keeps HEADER_HEIGHT at 40 so the card still lines up with the
    main I/O meters and GRAPH_H (see chainLayout / eqShared). */
const HEADER_HEIGHT = 40;
const HEADER_CHROME_H = ICON_BOX_SIZE;
const IMAGE_SIZE = 224;
/** Mini meter height in the side rails (meter sits centered above its knob). */
const RAIL_METER_HEIGHT = 180;
/** Centers the normalize (=) chrome box on the Out knob. */
const NORMALIZE_BUTTON_OFFSET = -(KNOB_SIZE_SECONDARY - ICON_BOX_SIZE) / 2;

/** EQ menu glyphs — shared 16×14 box / 1.5 stroke. Sliders sit halfway
    between the old full-height faders and the curve's y=3..11 band. */
const EqSlidersIcon: React.FC = () => (
  <svg
    width={ICON_SIZE}
    height={ICON_SIZE}
    viewBox="0 0 16 14"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
    preserveAspectRatio="xMidYMid meet"
    style={{ display: 'block', flexShrink: 0 }}
  >
    <line x1={5.5} y1={2.25} x2={5.5} y2={11.75} />
    <line x1={10.5} y1={2.25} x2={10.5} y2={11.75} />
    <circle cx={5.5} cy={8.8} r={1.8} fill="currentColor" stroke="none" />
    <circle cx={10.5} cy={5.2} r={1.8} fill="currentColor" stroke="none" />
  </svg>
);

const EqCurveIcon: React.FC = () => (
  <svg
    width={ICON_SIZE}
    height={ICON_SIZE}
    viewBox="0 0 16 14"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
    preserveAspectRatio="xMidYMid meet"
    style={{ display: 'block', flexShrink: 0 }}
  >
    <path d="M1 11 C5 11 5.5 3 8 3 C10.5 3 11 11 15 11" />
    <circle cx={8} cy={3} r={1.8} fill="currentColor" stroke="none" />
  </svg>
);

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

  const isNam = tone.format?.toLowerCase() === 'nam';
  // Long (reverb-like) IRs load half wet by default (native classifies by
  // kernel length and sets the mix on first load); Alt-click reset on Mix
  // must agree.
  const defaultMix = block.irLong ? 0.5 : 1;
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
      {/* Header — one ICON_BOX_SIZE chrome row on a shared centerline, then
          air to the hairline. Back/power/LITE/EQ/share all use the same 20px
          height (the old 24px back button was what pulled power off-center). */}
      <div
        style={{
          height: `${HEADER_HEIGHT}px`,
          flexShrink: 0,
          display: 'flex',
          flexDirection: 'column',
          boxSizing: 'border-box',
          borderBottom: BORDER,
        }}
      >
        <div
          style={{
            height: `${HEADER_CHROME_H}px`,
            display: 'flex',
            alignItems: 'center',
            gap: '24px',
            flexShrink: 0,
          }}
        >
        {/* Back is a bare 16px glyph — no chrome box (matches Select Tone). */}
        <button
          type="button"
          onClick={onBack}
          {...helpProps(HELP.backToChain)}
          style={{
            background: 'transparent',
            border: 'none',
            outline: 'none',
            padding: 0,
            margin: 0,
            display: 'grid',
            placeItems: 'center',
            cursor: 'pointer',
            color: '#ffffff',
            lineHeight: 0,
          }}
        >
          <ArrowLeft size={16} style={{ display: 'block' }} />
        </button>

        <ChromeIconButton
          tone="power"
          on={enabled}
          help={HELP.blockPower}
          onClick={handleToggleEnabled}
        >
          <Power />
        </ChromeIconButton>

        <div style={{ flex: 1, minWidth: 0 }} />

        {/* EQ toggle + its open menu share one 16px-gap row so the toggle
            sits as close to the menu as the menu items do to each other
            (not the header's 24px group gap). */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px', flexShrink: 0 }}>
          {showEq && (
            <>
              <div style={segmentedGroupStyle()}>
                <button
                  onClick={() => setEqView('sliders')}
                  {...helpProps(HELP.eqSlidersView)}
                  style={{
                    ...segmentedCellStyle(true),
                    color: eqView === 'sliders' ? '#ffffff' : MUTED,
                  }}
                >
                  <EqSlidersIcon />
                </button>
                <button
                  onClick={() => setEqView('graph')}
                  {...helpProps(HELP.eqCurveView)}
                  style={{
                    ...segmentedCellStyle(true),
                    color: eqView === 'graph' ? '#ffffff' : MUTED,
                  }}
                >
                  <EqCurveIcon />
                </button>
              </div>
              {/* PRE moves the EQ before the block's model (after In Gain);
                  off = the default post-block position. */}
              <ChromeTextButton armed={eqPre} help={HELP.eqPre} onClick={handleToggleEqPre}>
                PRE
              </ChromeTextButton>
              <ChromeIconButton help={HELP.eqReset} onClick={() => actions.resetBlockEq(blockId)}>
                <RotateCcw />
              </ChromeIconButton>
              <ChromeIconButton
                tone="power"
                on={eqOn}
                help={HELP.eqPower}
                onClick={handleToggleEqEnabled}
              >
                <Power />
              </ChromeIconButton>
            </>
          )}

          {/* Two independent signals (see theme.ts patterns): white fill =
              editor open; yellow fill = EQ shaping audio while closed. */}
          <ChromeTextButton
            armed={eqActive}
            open={showEq}
            help={HELP.eqToggle}
            onClick={() => setShowEq((prev) => !prev)}
          >
            EQ
          </ChromeTextButton>
        </div>

        <ChromeIconButton help={HELP.shareTone} onClick={handleShare}>
          {copied ? <Check /> : <Share />}
        </ChromeIconButton>
        <ChromeIconButton help={HELP.swapTone} onClick={() => actions.swapBlock(blockId)}>
          <ArrowLeftRight />
        </ChromeIconButton>
        <ChromeIconButton help={HELP.removeBlock} onClick={() => actions.removeBlock(blockId)}>
          <Trash2 />
        </ChromeIconButton>
        </div>
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
                size={KNOB_SIZE_SECONDARY}
                labelSize={12}
                labelBottom={false}
                thumb="secondary"
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
                size={KNOB_SIZE_SECONDARY}
                labelSize={12}
                labelBottom={false}
                thumb="secondary"
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
                  <ChromeIconButton
                    tone="armed"
                    on={normalizeOn}
                    help={HELP.blockNormalize}
                    onClick={handleToggleNormalize}
                    offsetY={NORMALIZE_BUTTON_OFFSET}
                  >
                    <Equal size={ICON_SIZE} />
                  </ChromeIconButton>
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
                  labelSize={12}
                  labelBottom={false}
                  thumb="secondary"
                  scale={gainDbScale}
                  defaultValue={0.5}
                  help={isNam || block.irLong ? HELP.blockOut : HELP.blockOutIr}
                />
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
};
