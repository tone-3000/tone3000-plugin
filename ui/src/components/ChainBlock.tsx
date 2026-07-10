import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowLeftRight,
  Check,
  GripVertical,
  Power,
  RotateCcw,
  Share,
  Trash2,
} from 'lucide-react';
import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { KnobControl } from './KnobControl';
import { ModelSelect } from './ModelSelect';
import { BlockMeter } from './BlockMeter';
import { BlockEqView } from './BlockEqView';
import type { EqViewMode } from './BlockEqView';
import { meterId } from '../hooks/useMeters';
import type { BlockParamName, EqBand, ToneBlock } from '../types/chain';
import { isEqFlat } from '../types/chain';
import { CARD_WIDTH, CARD_HEIGHT } from './chainLayout';

const HEADER_HEIGHT = 40;
const IMAGE_SIZE = 92;
const KNOB_SIZE = 30;
/** Mini meter height in the side rails (meter sits centered above its knob). */
const RAIL_METER_HEIGHT = 88;

const MUTED = 'rgba(235, 235, 245, 0.60)';
const BORDER = '1px solid rgba(84, 84, 88, 0.65)';

const headerButtonStyle: React.CSSProperties = {
  background: 'transparent',
  border: 'none',
  color: MUTED,
  cursor: 'pointer',
  width: '24px',
  height: '24px',
  borderRadius: '6px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  padding: 0,
  flexShrink: 0,
};

/** EQ menu glyphs, drawn for legibility at header size (two clean faders /
    one bell curve with its drag dot) rather than generic icon-set art. */
const EqSlidersIcon: React.FC<{ color: string }> = ({ color }) => (
  <svg width={14} height={14} viewBox="0 0 14 14">
    <line x1={4.5} y1={1.5} x2={4.5} y2={12.5} stroke={color} strokeWidth={1.4} strokeLinecap="round" />
    <line x1={9.5} y1={1.5} x2={9.5} y2={12.5} stroke={color} strokeWidth={1.4} strokeLinecap="round" />
    <circle cx={4.5} cy={9} r={2} fill={color} />
    <circle cx={9.5} cy={4.5} r={2} fill={color} />
  </svg>
);

const EqCurveIcon: React.FC<{ color: string }> = ({ color }) => (
  <svg width={16} height={14} viewBox="0 0 16 14">
    <path
      d="M1 11 C5 11 5.5 3 8 3 C10.5 3 11 11 15 11"
      fill="none"
      stroke={color}
      strokeWidth={1.5}
      strokeLinecap="round"
    />
    <circle cx={8} cy={3} r={1.8} fill={color} />
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

const AvatarFallback: React.FC<{ size: number }> = ({ size }) => (
  <svg width={size} height={size} viewBox="0 0 224 224" fill="none" aria-label="Avatar">
    <path
      d="M112 224C96.7007 224 82.2797 221.072 68.7373 215.216C55.268 209.36 43.3725 201.271 33.051 190.949C22.7294 180.628 14.6405 168.732 8.78431 155.263C2.9281 141.72 0 127.299 0 112C0 96.7008 2.9281 82.3165 8.78431 68.8472C14.6405 55.3047 22.6928 43.3727 32.9412 33.0511C43.2627 22.7295 55.1582 14.6406 68.6274 8.78442C82.1699 2.92821 96.5908 0.000106812 111.89 0.000106812C127.19 0.000106812 141.61 2.92821 155.153 8.78442C168.695 14.6406 180.627 22.7295 190.949 33.0511C201.271 43.3727 209.359 55.3047 215.216 68.8472C221.072 82.3165 224 96.7008 224 112C224 127.299 221.072 141.72 215.216 155.263C209.359 168.732 201.271 180.628 190.949 190.949C180.627 201.271 168.695 209.36 155.153 215.216C141.684 221.072 127.299 224 112 224ZM112 207.31C120.345 207.31 128.617 206.175 136.816 203.906C145.088 201.71 152.847 198.489 160.094 194.243C167.341 190.071 173.783 185.02 179.42 179.09C175.467 172.795 170.05 167.451 163.169 163.059C156.361 158.594 148.565 155.226 139.78 152.957C131.069 150.614 121.809 149.443 112 149.443C102.044 149.443 92.6745 150.614 83.8902 152.957C75.1059 155.299 67.3098 158.703 60.502 163.169C53.7673 167.561 48.4235 172.868 44.4706 179.09C50.1072 185.02 56.549 190.071 63.7961 194.243C71.1163 198.489 78.8758 201.71 87.0745 203.906C95.2732 206.175 103.582 207.31 112 207.31ZM112 130.777C119.027 130.85 125.359 129.093 130.996 125.506C136.706 121.846 141.244 116.868 144.612 110.573C147.979 104.277 149.663 97.2132 149.663 89.3805C149.663 81.987 147.979 75.2158 144.612 69.0668C141.244 62.9178 136.706 58.0132 130.996 54.353C125.286 50.6197 118.954 48.753 112 48.753C104.973 48.753 98.6039 50.6197 92.8941 54.353C87.1843 58.0132 82.6457 62.9178 79.2784 69.0668C75.9111 75.2158 74.264 81.987 74.3372 89.3805C74.3372 97.2132 75.9843 104.241 79.2784 110.463C82.6457 116.685 87.1477 121.626 92.7843 125.286C98.4941 128.873 104.899 130.703 112 130.777Z"
      fill="#8D8D93"
    />
  </svg>
);

interface ChainBlockProps {
  block: ToneBlock;
  onRemove: (blockId: string) => void;
  /** Launch the Select flow to replace this block's tone in place. */
  onSwap: (blockId: string) => void;
  /** Copy the tone's TONE3000 URL; resolves true when it hit the clipboard. */
  onShare: (block: ToneBlock) => Promise<boolean>;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
  /** Fire-and-forget per-block param setter (see useChainState). */
  onSetParam: (blockId: string, param: BlockParamName, value: number | boolean) => void;
  /** Fire-and-forget whole-band EQ setter (see useChainState). */
  onSetEqBand: (blockId: string, bandIndex: number, band: EqBand) => void;
  /** EQ power/bypass — band settings persist, processing is skipped. */
  onSetEqEnabled: (blockId: string, enabled: boolean) => void;
  onResetEq: (blockId: string) => void;
  /** Host sample rate, for the EQ curve math. */
  sampleRate: number;
}

export const ChainBlock: React.FC<ChainBlockProps> = ({
  block,
  onRemove,
  onSwap,
  onShare,
  onSwitchModel,
  onSetParam,
  onSetEqBand,
  onSetEqEnabled,
  onResetEq,
  sampleRate,
}) => {
  const { blockId, tone, params } = block;

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

  // Params can change from outside (undo/redo, state restore, other editor
  // window); follow the backend when it reports a new value. Mid-drag the
  // polled value trails the optimistic one by at most a poll interval and
  // converges to it, so knobs never visibly fight the pointer.
  useEffect(() => setEnabled(params.enabled), [params.enabled]);
  useEffect(() => setInputGain(params.inputGain ?? 0.5), [params.inputGain]);
  useEffect(() => setOutputGain(params.outputGain ?? 0.5), [params.outputGain]);
  useEffect(() => setMix(params.mix ?? 1.0), [params.mix]);
  useEffect(() => setEqOn(params.eq?.enabled ?? true), [params.eq?.enabled]);
  useEffect(() => setSlimmableSize(params.namSlimmableSize ?? 1), [blockId, params.namSlimmableSize]);
  useEffect(() => () => window.clearTimeout(copiedTimeoutRef.current), []);

  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id: blockId,
  });

  const setParam = useCallback(
    (param: BlockParamName, value: number | boolean) => onSetParam(blockId, param, value),
    [blockId, onSetParam]
  );

  const handleToggleEnabled = useCallback(() => {
    setEnabled((prev) => {
      setParam('enabled', !prev);
      return !prev;
    });
  }, [setParam]);

  const handleToggleEqEnabled = useCallback(() => {
    setEqOn((prev) => {
      onSetEqEnabled(blockId, !prev);
      return !prev;
    });
  }, [blockId, onSetEqEnabled]);

  const handleShare = useCallback(async () => {
    if (await onShare(block)) {
      setCopied(true);
      window.clearTimeout(copiedTimeoutRef.current);
      copiedTimeoutRef.current = window.setTimeout(() => setCopied(false), 1500);
    }
  }, [block, onShare]);

  const isLite = slimmableSize < 0.75;
  const handleNamSizeMode = useCallback(
    (useLite: boolean) => {
      const size = useLite ? 0.5 : 1.0;
      setSlimmableSize(size);
      setParam('namSlimmableSize', size);
    },
    [setParam]
  );

  const handleModelSelect = async (id: string) => {
    if (!tone.models?.length || !onSwitchModel || isSwitchingModel) return;
    const newModelId = parseInt(id, 10);
    if (isNaN(newModelId)) return;

    setIsSwitchingModel(true);
    try {
      await onSwitchModel(blockId, newModelId);
    } finally {
      setIsSwitchingModel(false);
    }
  };

  const isNam = tone.format?.toLowerCase() === 'nam';
  // architecture=2 NAM models are always A2/slimmable — no need to wait on
  // the native capability flag that used to gate this after model load.
  const formatBadge = isNam
    ? 'NAM A2'
    : (tone.format?.toUpperCase() ?? '');

  // EQ is shaping this block's audio: powered on and not flat (a flat or
  // bypassed EQ is skipped natively). Uses the optimistic power state so the
  // header glow reacts to the toggle immediately.
  const eqActive = eqOn && params.eq ? !isEqFlat(params.eq) : false;

  return (
    <div
      ref={setNodeRef}
      style={{
        transform: CSS.Transform.toString(transform),
        transition: isDragging ? 'none' : transition,
        opacity: isDragging ? 0.75 : 1,
        display: 'flex',
        flexDirection: 'column',
        border: BORDER,
        backgroundColor: '#151517',
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
          backgroundColor: '#1C1C1E',
          borderBottom: BORDER,
        }}
      >
        <div
          {...attributes}
          {...listeners}
          title="Drag to reorder"
          style={{ ...headerButtonStyle, cursor: 'grab', color: '#8D8D93' }}
        >
          <GripVertical size={16} />
        </div>

        <button
          onClick={handleToggleEnabled}
          title={enabled ? 'Turn block off' : 'Turn block on'}
          style={{
            ...headerButtonStyle,
            color: enabled ? '#ffffff' : '#8D8D93',
            backgroundColor: enabled ? 'transparent' : 'rgba(235, 235, 245, 0.18)',
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
                backgroundColor: isLite ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
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
                backgroundColor: !isLite ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
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
                  backgroundColor:
                    eqView === 'sliders' ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
                }}
              >
                <EqSlidersIcon color={eqView === 'sliders' ? '#ffffff' : MUTED} />
              </button>
              <button
                onClick={() => setEqView('graph')}
                title="Curve view"
                style={{
                  ...headerGroupButtonStyle,
                  borderLeft: BORDER,
                  backgroundColor:
                    eqView === 'graph' ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
                }}
              >
                <EqCurveIcon color={eqView === 'graph' ? '#ffffff' : MUTED} />
              </button>
            </div>
            <div style={headerGroupStyle}>
              <button
                onClick={() => onResetEq(blockId)}
                title="Reset EQ to flat"
                style={{ ...headerGroupButtonStyle, color: MUTED }}
              >
                <RotateCcw size={12} />
              </button>
              <button
                onClick={handleToggleEqEnabled}
                title={eqOn ? 'Bypass EQ' : 'Enable EQ'}
                style={{
                  ...headerGroupButtonStyle,
                  borderLeft: BORDER,
                  color: eqOn ? '#ffffff' : '#8D8D93',
                  backgroundColor: eqOn ? 'transparent' : 'rgba(235, 235, 245, 0.18)',
                }}
              >
                <Power size={12} />
              </button>
            </div>
          </>
        )}

        {/* EQ view toggle. The text glows when the EQ is shaping audio
            (on and non-flat) so an active EQ is visible even when closed. */}
        <button
          onClick={() => setShowEq((prev) => !prev)}
          title={showEq ? 'Hide EQ' : 'Show EQ'}
          style={{
            ...headerButtonStyle,
            width: 'auto',
            padding: '0 8px',
            fontSize: '11px',
            fontWeight: 700,
            border: BORDER,
            color: eqActive ? '#FFFF00' : showEq ? '#ffffff' : MUTED,
            backgroundColor: showEq ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
            marginRight: '4px',
          }}
        >
          EQ
        </button>

        <button
          onClick={handleShare}
          title="Copy tone link"
          style={{ ...headerButtonStyle, color: copied ? '#30D158' : MUTED }}
        >
          {copied ? <Check size={14} /> : <Share size={14} />}
        </button>
        <button
          onClick={() => onSwap(blockId)}
          title="Swap tone"
          style={headerButtonStyle}
        >
          <ArrowLeftRight size={14} />
        </button>
        <button
          onClick={() => onRemove(blockId)}
          title="Remove block"
          style={headerButtonStyle}
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
            onSetBand={onSetEqBand}
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
          <BlockMeter meterId={meterId.blockIn(blockId)} height={RAIL_METER_HEIGHT} />
          <KnobControl
            label="In"
            value={inputGain}
            onChange={(val) => {
              setInputGain(val);
              setParam('inputGain', val);
            }}
            size={KNOB_SIZE}
            labelSize={12}
            labelBottom={false}
            innerColor="#151517"
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
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', minWidth: 0, flex: 1 }}>
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

              <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '12px' }}>
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
            size={KNOB_SIZE}
            labelSize={12}
            labelBottom={false}
            innerColor="#151517"
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
          <BlockMeter meterId={meterId.blockOut(blockId)} height={RAIL_METER_HEIGHT} />
          <KnobControl
            label="Out"
            value={outputGain}
            onChange={(val) => {
              setOutputGain(val);
              setParam('outputGain', val);
            }}
            size={KNOB_SIZE}
            labelSize={12}
            labelBottom={false}
            innerColor="#151517"
          />
        </div>
          </>
        )}
      </div>
    </div>
  );
};
