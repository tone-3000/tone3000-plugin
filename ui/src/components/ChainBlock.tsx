import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeft,
  ArrowLeftRight,
  Check,
  Download,
  Equal,
  FolderClosed,
  Power,
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
import { CARD_WIDTH, CARD_HEIGHT, CARD_RADIUS, HEADER_HEIGHT, BODY_PADDING } from './chainLayout';
import { formatLabel, gearLabel } from '../t3k/labels';
import { AvatarImage } from './AvatarFallback';
import { FormatBadge } from './FormatBadge';
import { HELP, helpProps } from './helpText';
import { useBlockNormalizeControlEnabled } from './uiPreferences';
import { ChromeIconButton, ChromeTextButton } from './ChromeIconButton';
import {
  BORDER,
  GRAY,
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_SECONDARY,
  MUTED,
  SEGMENTED_TRACK,
  WHITE,
  segmentedCellStyle,
  segmentedGroupStyle,
} from './theme';

/** Tone image; matches the Figma detail mock (fits body with model select). */
const IMAGE_SIZE = 192;
/** Mini meter height in the side rails (meter sits centered above its knob). */
const RAIL_METER_HEIGHT = 160;
/** Centers the normalize (=) chrome box on the Out knob. */
const NORMALIZE_BUTTON_OFFSET = -(KNOB_SIZE_SECONDARY - ICON_BOX_SIZE) / 2;

/** Downloads / models count with a leading icon (same pattern as ToneBrowser). */
const CountStat: React.FC<{ icon: React.ReactNode; value: number }> = ({ icon, value }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
    <span style={{ display: 'grid', placeItems: 'center', color: GRAY }}>{icon}</span>
    <span style={{ fontSize: '14px', fontWeight: 400, color: MUTED }}>
      {value.toLocaleString()}
    </span>
  </div>
);

/** EQ view glyphs — 16×16, stroke inherits selected/muted color. */
const EqSlidersIcon: React.FC = () => (
  <svg
    width={16}
    height={16}
    viewBox="0 0 16 16"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.33333}
    strokeLinecap="round"
    strokeLinejoin="round"
    style={{ display: 'block', flexShrink: 0 }}
  >
    <path d="M11.3333 6.66669V12.6667" />
    <path d="M4.66675 3.33331V9.33331" />
    <path d="M13.3333 4.66669C13.3333 3.56212 12.4378 2.66669 11.3333 2.66669C10.2287 2.66669 9.33325 3.56212 9.33325 4.66669C9.33325 5.77126 10.2287 6.66669 11.3333 6.66669C12.4378 6.66669 13.3333 5.77126 13.3333 4.66669Z" />
    <path d="M6.66675 11.3333C6.66675 10.2287 5.77132 9.33331 4.66675 9.33331C3.56218 9.33331 2.66675 10.2287 2.66675 11.3333C2.66675 12.4379 3.56218 13.3333 4.66675 13.3333C5.77132 13.3333 6.66675 12.4379 6.66675 11.3333Z" />
  </svg>
);

const EqCurveIcon: React.FC = () => (
  <svg
    width={16}
    height={16}
    viewBox="0 0 16 16"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.5}
    strokeLinecap="round"
    style={{ display: 'block', flexShrink: 0 }}
  >
    <path d="M1 13.5C5 13.5 5.5 2.5 8 2.5C10.5 2.5 11 13.5 15 13.5" />
  </svg>
);

interface ChainBlockProps {
  block: ToneBlock;
  /** Host sample rate, for the EQ curve math. */
  sampleRate: number;
  /** Return to the chain gallery (← BLOCK sits above the bordered card). */
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
        width: `${CARD_WIDTH}px`,
        boxSizing: 'border-box',
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
          gap: '16px',
          marginBottom: '16px',
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
            fontFamily: 'monospace',
            fontSize: '16px',
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
          height: `${CARD_HEIGHT}px`,
          boxSizing: 'border-box',
          border: BORDER,
          borderRadius: `${CARD_RADIUS}px`,
          overflow: 'hidden',
        }}
      >
        {/* Header — 16px inset, chrome centered in HEADER_HEIGHT. */}
        <div
          style={{
            height: `${HEADER_HEIGHT}px`,
            flexShrink: 0,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
            padding: `0 ${BODY_PADDING}px`,
            boxSizing: 'border-box',
            borderBottom: BORDER,
          }}
        >
          <ChromeIconButton
            tone="power"
            on={enabled}
            help={HELP.blockPower}
            onClick={handleToggleEnabled}
          >
            <Power />
          </ChromeIconButton>

          {/* Right cluster: EQ submenu (pill when open) then share/swap/trash.
              EQ stays rightmost in the submenu so opening grows left only.
              marginRight cancels the pill's right pad so EQ doesn't shift
              relative to share. */}
          <div style={{ display: 'flex', alignItems: 'center', gap: '24px', flexShrink: 0 }}>
            <div
              style={{
                display: 'inline-flex',
                alignItems: 'center',
                gap: showEq ? '16px' : 0,
                padding: showEq ? '4px 12px' : 0,
                // Pull back by the pill's right pad so EQ stays put vs share.
                marginRight: showEq ? -12 : 0,
                borderRadius: showEq ? 100 : 0,
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
                  <ChromeTextButton armed={eqPre} help={HELP.eqPre} onClick={handleToggleEqPre}>
                    PRE
                  </ChromeTextButton>
                  <div
                    style={{
                      ...segmentedGroupStyle(),
                      // Nested track — slightly quieter than the outer pill.
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
                </>
              )}
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

        {/* Body — tone view uses BODY_PADDING; EQ spectrum/grid bleeds
            edge-to-edge (interactive chrome insets itself). */}
        <div
          style={{
            flex: 1,
            minHeight: 0,
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'stretch',
            gap: showEq ? 0 : '24px',
            padding: showEq ? 0 : `${BODY_PADDING}px`,
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
            {/* Input rail: meter above In knob (Figma: gap 12). */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                flexShrink: 0,
                gap: '12px',
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

            {/* Center: image + tone info on top, model picker spanning full width. */}
            <div
              style={{
                flex: 1,
                minWidth: 0,
                alignSelf: 'stretch',
                display: 'flex',
                flexDirection: 'column',
                justifyContent: 'space-between',
              }}
            >
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
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

                {/* Tone info — title / gear+badge / counts / creator (Figma gaps). */}
                <div
                  style={{
                    display: 'flex',
                    flexDirection: 'column',
                    gap: '16px',
                    minWidth: 0,
                    flex: 1,
                  }}
                >
                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      gap: '8px',
                      minWidth: 0,
                    }}
                  >
                    <span
                      style={{
                        fontSize: '18px',
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
                        gap: '16px',
                      }}
                    >
                      {tone.gear && (
                        <span style={{ fontSize: '14px', color: MUTED, fontWeight: 400 }}>
                          {gearLabel(tone.gear)}
                        </span>
                      )}
                      {formatBadge && <FormatBadge label={formatBadge} />}
                    </div>
                  </div>

                  <div
                    style={{
                      display: 'flex',
                      flexDirection: 'row',
                      alignItems: 'center',
                      gap: '24px',
                    }}
                  >
                    <CountStat
                      icon={<Download size={16} />}
                      value={tone.downloads_count ?? 0}
                    />
                    <CountStat
                      icon={<FolderClosed size={16} />}
                      value={tone.models_count ?? 0}
                    />
                  </div>

                  {tone.user && (
                    <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
                      <div
                        style={{
                          width: '32px',
                          height: '32px',
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
                      <span style={{ fontSize: '14px', color: GRAY, fontWeight: 400 }}>
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

            {/* Output rail: meter above Out (+ optional normalize). */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                flexShrink: 0,
                gap: '12px',
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
    </div>
  );
};
