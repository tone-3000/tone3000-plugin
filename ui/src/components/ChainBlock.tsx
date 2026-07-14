import React, { useCallback, useEffect, useRef, useState } from 'react';
import { ArrowLeftRight, Check, Power, RotateCcw, Share, Trash2 } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { gainDbScale } from './knobScale';
import { ModelSelect } from './ModelSelect';
import { BlockMeter } from './BlockMeter';
import { BlockEqView } from './BlockEqView';
import type { EqViewMode } from './BlockEqView';
import { meterId } from '../hooks/useMeters';
import { useChainActions } from '../hooks/useChainActions';
import type { BlockParamName, ToneBlock } from '../types/chain';
import { isEqFlat } from '../types/chain';
import { CARD_WIDTH, CARD_HEIGHT } from './chainLayout';
import { AvatarFallback } from './AvatarFallback';
import {
  ACTIVE_OUTLINE,
  BORDER,
  GRAY,
  HIGHLIGHT,
  MUTED,
  SURFACE,
  SURFACE_RAISED,
  iconButtonStyle,
} from './theme';

const HEADER_HEIGHT = 40;
const IMAGE_SIZE = 200;
const KNOB_SIZE = 30;
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
}

/** The detail card (full block view). All mutations come from the
    ChainActions context; only the block itself and the sample rate arrive
    as props. */
export const ChainBlock: React.FC<ChainBlockProps> = ({ block, sampleRate }) => {
  const { blockId, tone, params } = block;
  const actions = useChainActions();

  // Optimistic local values for the controls; native converges via polling.
  const [enabled, setEnabled] = useState(params.enabled);
  const [inputGain, setInputGain] = useState(params.inputGain ?? 0.5);
  const [outputGain, setOutputGain] = useState(params.outputGain ?? 0.5);
  const [mix, setMix] = useState(params.mix ?? 1.0);
  const [slimmableSize, setSlimmableSize] = useState(params.namSlimmableSize ?? 1);
  const [isSwitchingModel, setIsSwitchingModel] = useState(false);
  const [showEq, setShowEq] = useState(false);
  const [eqView, setEqView] = useState<EqViewMode>('sliders');
  // Optimistic EQ power state (native converges via polling, like `enabled`).
  const [eqOn, setEqOn] = useState(params.eq?.enabled ?? true);
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

  const handleToggleEqEnabled = useCallback(() => {
    setEqOn((prev) => {
      actions.setBlockEqEnabled(blockId, !prev);
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

  const handleModelSelect = async (id: string) => {
    if (!tone.models?.length || isSwitchingModel) return;
    const newModelId = parseInt(id, 10);
    if (isNaN(newModelId)) return;

    setIsSwitchingModel(true);
    try {
      await actions.switchModel(blockId, newModelId);
    } finally {
      setIsSwitchingModel(false);
    }
  };

  const isNam = tone.format?.toLowerCase() === 'nam';
  // architecture=2 NAM models are always A2/slimmable — no need to wait on
  // the native capability flag that used to gate this after model load.
  const formatBadge = isNam ? 'NAM A2' : (tone.format?.toUpperCase() ?? '');

  // EQ is shaping this block's audio: powered on and not flat (a flat or
  // bypassed EQ is skipped natively). Uses the optimistic power state so the
  // header glow reacts to the toggle immediately.
  const eqActive = eqOn && params.eq ? !isEqFlat(params.eq) : false;

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        border: BORDER,
        backgroundColor: SURFACE,
        position: 'relative',
        borderRadius: '16px',
        width: `${CARD_WIDTH}px`,
        height: `${CARD_HEIGHT}px`,
        boxSizing: 'border-box',
        overflow: 'hidden',
      }}
    >
      {/* Header */}
      <div
        style={{
          height: `${HEADER_HEIGHT}px`,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          gap: '4px',
          padding: '8px 12px',
          boxSizing: 'border-box',
          backgroundColor: SURFACE_RAISED,
          borderBottom: BORDER,
        }}
      >
        <button
          onClick={handleToggleEnabled}
          title={enabled ? 'Turn block off' : 'Turn block on'}
          style={{
            ...headerButtonStyle,
            color: enabled ? '#ffffff' : GRAY,
            backgroundColor: enabled ? 'transparent' : HIGHLIGHT,
          }}
        >
          <Power size={14} />
        </button>

        {/* LITE/FULL for every NAM block (architecture=2 = always A2). */}
        {isNam && (
          <div
            style={{
              display: 'flex',
              flexDirection: 'row',
              marginLeft: '4px',
              borderRadius: '6px',
              border: BORDER,
              overflow: 'hidden',
              flexShrink: 0,
              opacity: block.loaded && !isSwitchingModel ? 1 : 0.45,
              pointerEvents: block.loaded && !isSwitchingModel ? 'auto' : 'none',
            }}
          >
            <button
              type="button"
              onClick={() => handleNamSizeMode(true)}
              style={{
                padding: '3px 10px',
                fontSize: '11px',
                fontWeight: 700,
                border: 'none',
                cursor: 'pointer',
                backgroundColor: isLite ? HIGHLIGHT : 'transparent',
                color: '#ffffff',
              }}
            >
              LITE
            </button>
            <button
              type="button"
              onClick={() => handleNamSizeMode(false)}
              style={{
                padding: '3px 10px',
                fontSize: '11px',
                fontWeight: 700,
                border: 'none',
                borderLeft: BORDER,
                cursor: 'pointer',
                backgroundColor: !isLite ? HIGHLIGHT : 'transparent',
                color: '#ffffff',
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
          <>
            <div style={headerGroupStyle}>
              <button
                onClick={() => setEqView('sliders')}
                title="Sliders view"
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
                title="Curve view"
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
            <div style={headerGroupStyle}>
              <button
                onClick={() => actions.resetBlockEq(blockId)}
                title="Reset EQ to flat"
                style={{ ...headerGroupButtonStyle, color: '#ffffff' }}
              >
                <RotateCcw size={14} />
              </button>
              <button
                onClick={handleToggleEqEnabled}
                title={eqOn ? 'Bypass EQ' : 'Enable EQ'}
                style={{
                  ...headerGroupButtonStyle,
                  borderLeft: BORDER,
                  color: eqOn ? '#ffffff' : GRAY,
                  backgroundColor: eqOn ? 'transparent' : HIGHLIGHT,
                }}
              >
                <Power size={14} />
              </button>
            </div>
          </>
        )}

        {/* EQ view toggle. Two independent signals (see theme.ts patterns):
            white text + bright outline = the EQ is shaping audio (on and
            non-flat), even when the editor is closed; grey fill = the
            editor panel is currently open (like the tuner toggle). */}
        <button
          onClick={() => setShowEq((prev) => !prev)}
          title={showEq ? 'Hide EQ' : 'Show EQ'}
          style={{
            ...headerButtonStyle,
            width: 'auto',
            padding: '0 8px',
            fontSize: '11px',
            fontWeight: 700,
            border: eqActive ? ACTIVE_OUTLINE : BORDER,
            color: eqActive || showEq ? '#ffffff' : MUTED,
            backgroundColor: showEq ? HIGHLIGHT : 'transparent',
            marginRight: '4px',
          }}
        >
          EQ
        </button>

        <button
          onClick={handleShare}
          title="Copy tone link"
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          {copied ? <Check size={14} /> : <Share size={14} />}
        </button>
        <button
          onClick={() => actions.swapBlock(blockId)}
          title="Swap tone"
          style={{ ...headerButtonStyle, color: '#ffffff' }}
        >
          <ArrowLeftRight size={14} />
        </button>
        <button
          onClick={() => actions.removeBlock(blockId)}
          title="Remove block"
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
          // The EQ grid bleeds edge-to-edge; the normal view keeps its gutters.
          gap: showEq ? 0 : '12px',
          padding: showEq ? 0 : '12px',
          boxSizing: 'border-box',
          opacity: enabled ? 1 : 0.45,
          transition: 'opacity 0.2s ease',
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
            {/* Input rail: meter centered above the input gain knob */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                justifyContent: 'flex-end',
                gap: '8px',
                flexShrink: 0,
              }}
            >
              <BlockMeter meterId={meterId.blockIn(blockId)} length={RAIL_METER_HEIGHT} />
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
                innerColor={SURFACE}
                scale={gainDbScale}
                defaultValue={0.5}
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
                gap: '8px',
              }}
            >
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
                  alignItems: 'flex-start',
                  gap: '12px',
                  minWidth: 0,
                }}
              >
                {/* Tone image */}
                {tone.images?.[0] && (
                  <div
                    style={{
                      width: IMAGE_SIZE,
                      height: IMAGE_SIZE,
                      borderRadius: '8px',
                      overflow: 'hidden',
                      flexShrink: 0,
                    }}
                  >
                    <img
                      src={tone.images[0]}
                      alt={tone.title}
                      style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                    />
                  </div>
                )}

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
                      fontSize: '16px',
                      color: '#ffffff',
                      fontWeight: '700',
                      overflow: 'hidden',
                      textOverflow: 'ellipsis',
                      whiteSpace: 'nowrap',
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
                      <span style={{ fontSize: '13px', color: MUTED, fontWeight: '400' }}>
                        {tone.gear}
                      </span>
                    )}
                    {formatBadge && (
                      <span
                        style={{
                          fontFamily: 'monospace',
                          fontSize: '11px',
                          color: 'black',
                          fontWeight: '400',
                          backgroundColor: MUTED,
                          padding: '0 6px',
                          borderRadius: '4px',
                          whiteSpace: 'nowrap',
                        }}
                      >
                        {formatBadge}
                      </span>
                    )}
                  </div>

                  {tone.user && (
                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                      <div
                        style={{
                          width: '20px',
                          height: '20px',
                          borderRadius: '50%',
                          overflow: 'hidden',
                          flexShrink: 0,
                        }}
                      >
                        {tone.user.avatar_url ? (
                          <img
                            src={tone.user.avatar_url}
                            alt={tone.user.username}
                            style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                          />
                        ) : (
                          <AvatarFallback size={20} />
                        )}
                      </div>
                      <span style={{ fontSize: '13px', color: '#ffffff', fontWeight: '700' }}>
                        {tone.user.username}
                      </span>
                    </div>
                  )}
                </div>
              </div>

              <ModelSelect
                options={(tone.models ?? []).map((m) => ({ id: String(m.id), name: m.name }))}
                value={String(block.activeModelId)}
                onChange={handleModelSelect}
                height={36}
              />
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
                innerColor={SURFACE}
                defaultValue={1}
              />
            </div>

            {/* Output rail: meter centered above the output gain knob */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                justifyContent: 'flex-end',
                gap: '8px',
                flexShrink: 0,
              }}
            >
              <BlockMeter meterId={meterId.blockOut(blockId)} length={RAIL_METER_HEIGHT} />
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
                innerColor={SURFACE}
                scale={gainDbScale}
                defaultValue={0.5}
              />
            </div>
          </>
        )}
      </div>
    </div>
  );
};
