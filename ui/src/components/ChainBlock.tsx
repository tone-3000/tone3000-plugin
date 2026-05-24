import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { X as XIcon, GripVertical, FolderClosed } from 'lucide-react';
import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { KnobControl } from './KnobControl';
import { ModelSelect } from './ModelSelect';
import { useAudioBackend } from '../hooks/useAudioBackend';
import type { ChainBlockData } from '../types/tone';

const GEAR_CAPTURE_MAP = {
  amp: 'Amp Head Capture',
  'full-rig': 'Full Rig / Combo Capture',
  pedal: 'Pedal Capture',
  outboard: 'Outboard Capture',
  ir: 'Impulse Response',
};

const imageSize = 128;

const timeAgo = (dateString: string) => {
  const date = new Date(dateString);
  const now = new Date();
  const seconds = Math.floor((now.getTime() - date.getTime()) / 1000);

  // Constants for time units in seconds
  const MINUTE = 60;
  const HOUR = MINUTE * 60; // 3600 seconds
  const DAY = HOUR * 24; // 86400 seconds
  const WEEK = DAY * 7; // 604800 seconds
  const MONTH = WEEK * 4;
  const YEAR = MONTH * 12;

  // Determine the closest unit based on lower thresholds
  if (seconds < MINUTE * 60) {
    // Less than 60 minutes, show in minutes
    const minutes = Math.round(seconds / MINUTE);
    return minutes === 1 ? '1 minute ago' : `${minutes} minutes ago`;
  } else if (seconds < HOUR * 24) {
    // Less than 24 hours, show in hours
    const hours = Math.floor(seconds / HOUR);
    return hours === 1 ? '1 hour ago' : `${hours} hours ago`;
  } else if (seconds < DAY * 7) {
    // Less than 7 days, show in days
    const days = Math.floor(seconds / DAY);
    return days === 1 ? '1 day ago' : `${days} days ago`;
  } else if (seconds < WEEK * 4) {
    // Less than 4 weeks, show in weeks
    const weeks = Math.floor(seconds / WEEK);
    return weeks === 1 ? '1 week ago' : `${weeks} weeks ago`;
  } else if (seconds < MONTH * 12) {
    // Less than 12 months, show in months
    const months = Math.floor(seconds / MONTH);
    return months === 1 ? '1 month ago' : `${months} months ago`;
  } else {
    // Show in years
    const years = Math.round(seconds / YEAR);
    return years === 1 ? '1 year ago' : `${years} years ago`;
  }
};

interface ChainBlockProps {
  block: ChainBlockData;
  onRemove: (id: string) => void;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
}

export const ChainBlock: React.FC<ChainBlockProps> = ({ block, onRemove, onSwitchModel }) => {
  const [outputGain, setOutputGain] = useState<number>(block.outputGain ?? 0.5);
  const [mix, setMix] = useState<number>(block.mix ?? 1.0);
  const [isSwitchingModel, setIsSwitchingModel] = useState(false);
  const [localNamSlimmableSize, setLocalNamSlimmableSize] = useState(block.namSlimmableSize ?? 1);
  const backend = useAudioBackend();

  useEffect(() => {
    setLocalNamSlimmableSize(block.namSlimmableSize ?? 1);
  }, [block.blockId, block.namSlimmableSize]);

  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id: block.blockId,
  });

  const setBlockOutputGain = useMemo(
    () => backend.getPluginFunction('setBlockOutputGain'),
    [backend]
  );
  const setBlockMix = useMemo(() => backend.getPluginFunction('setBlockMix'), [backend]);
  const setBlockNamSlimmableSize = useMemo(
    () => backend.getPluginFunction('setBlockNamSlimmableSize'),
    [backend]
  );

  const handleRemove = useCallback(
    (e: React.MouseEvent) => {
      e.stopPropagation();
      onRemove(block.blockId);
    },
    [block.blockId, onRemove]
  );

  const handleGainChange = useCallback(
    (val: number) => {
      setOutputGain(val);
      try {
        // Fire-and-forget; native may not exist yet
        Promise.resolve(setBlockOutputGain(block.blockId, val)).catch(() => {});
      } catch {
        // ignore missing native function
      }
    },
    [block.blockId, setBlockOutputGain]
  );

  const handleMixChange = useCallback(
    (val: number) => {
      setMix(val);
      try {
        // Fire-and-forget; native may not exist yet
        Promise.resolve(setBlockMix(block.blockId, val)).catch(() => {});
      } catch {
        // ignore missing native function
      }
    },
    [block.blockId, setBlockMix]
  );

  const namSlimmable = block.namSlimmable === true;
  const isLite = localNamSlimmableSize < 0.75;

  const handleNamSizeMode = useCallback(
    (useLite: boolean) => {
      const size = useLite ? 0.5 : 1.0;
      setLocalNamSlimmableSize(size);
      try {
        Promise.resolve(setBlockNamSlimmableSize(block.blockId, size)).catch(() => {});
      } catch {
        // ignore missing native function
      }
    },
    [block.blockId, setBlockNamSlimmableSize]
  );

  const handleModelSelect = async (id: string) => {
    if (!block.models?.length || !onSwitchModel || isSwitchingModel) return;

    const newModelId = parseInt(id, 10);
    if (isNaN(newModelId)) return;

    setIsSwitchingModel(true);
    try {
      await onSwitchModel(block.blockId, newModelId);
    } finally {
      setIsSwitchingModel(false);
    }
  };

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.75 : 1,
  };

  return (
    <div
      ref={setNodeRef}
      style={{
        ...style,
        display: 'flex',
        flexDirection: 'column',
        border: '1px solid rgba(84, 84, 88, 0.65)',
        backgroundColor: '#151517',
        position: 'relative',
        transition: isDragging ? 'none' : 'all 0.2s ease',
        borderRadius: '16px',
        width: '640px',
        // height: '254px',
        padding: '24px',
      }}
    >
      <div
        {...attributes}
        {...listeners}
        style={{
          cursor: 'grab',
          background: '#2C2C2E',
          border: 'none',
          color: '#ffffff',
          width: '32px',
          height: '32px',
          borderRadius: '32px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'absolute',
          top: '-16px',
          left: '-16px',
        }}
        title="Drag to reorder"
      >
        <GripVertical size={16} />
      </div>
      <button
        onClick={handleRemove}
        style={{
          background: '#2C2C2E',
          border: 'none',
          color: '#ffffff',
          cursor: 'pointer',
          width: '32px',
          height: '32px',
          borderRadius: '32px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'absolute',
          top: '-16px',
          right: '-16px',
        }}
        title="Remove"
      >
        <XIcon size={16} />
      </button>

      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          gap: '32px',
        }}
      >
        {/* Image and Knobs/Author Side by Side */}
        <div
          style={{
            display: 'flex',
            gap: '32px',
            alignItems: 'flex-start',
          }}
        >
          {/* Large Tone Image */}
          {block.images?.[0] && (
            <div
              style={{
                width: imageSize,
                height: imageSize,
                borderRadius: '8px',
                overflow: 'hidden',
                flexShrink: 0,
              }}
            >
              <img
                src={block.images[0]}
                alt={block.title}
                style={{ width: '100%', height: '100%', objectFit: 'cover' }}
              />
            </div>
          )}

          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              minWidth: 0,
              flex: 1,
              gap: '16px',
            }}
          >
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                minWidth: 0,
                flex: 1,
                gap: '8px',
              }}
            >
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'column',
                  minWidth: 0,
                  flex: 1,
                }}
              >
                <span
                  style={{
                    fontSize: '18px',
                    color: '#ffffff',
                    fontWeight: '700',
                    overflow: 'hidden',
                    textOverflow: 'ellipsis',
                    display: '-webkit-box',
                    WebkitLineClamp: 2,
                    WebkitBoxOrient: 'vertical',
                  }}
                >
                  {block.title}
                </span>
              </div>

              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
                  alignItems: 'center',
                  gap: '24px',
                }}
              >
                <div
                  style={{
                    display: 'flex',
                    flexDirection: 'row',
                    alignItems: 'center',
                    gap: '16px',
                  }}
                >
                  <span
                    style={{
                      fontSize: '14px',
                      color: 'rgba(235, 235, 245, 0.60)',
                      fontWeight: '400',
                    }}
                  >
                    {GEAR_CAPTURE_MAP[block.gear as keyof typeof GEAR_CAPTURE_MAP]}
                  </span>
                  <span
                    style={{
                      fontFamily: 'monospace',
                      fontSize: '12px',
                      color: 'black',
                      fontWeight: '400',
                      backgroundColor: 'rgba(235, 235, 245, 0.60)',
                      padding: '0 6px',
                      borderRadius: '4px',
                    }}
                  >
                    {block.platform.toUpperCase()}
                  </span>
                </div>
                <div
                  style={{
                    display: 'flex',
                    flexDirection: 'row',
                    alignItems: 'center',
                    gap: '8px',
                    fontSize: '14px',
                    color: 'rgba(235, 235, 245, 0.60)',
                    fontWeight: '400',
                  }}
                >
                  <FolderClosed size={16} />
                  {(block.models_count || 0) > 0 && (
                    <p>
                      {block.models_count?.toLocaleString() || 0} <span>Models</span>
                    </p>
                  )}
                </div>
              </div>
            </div>

            {/* Knobs and Author Column */}
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                gap: '16px',
                flex: 1,
              }}
            >
              {/* Author Info */}
              {block.user && (
                <div
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: '12px',
                    width: '100%',
                  }}
                >
                  <div
                    style={{
                      width: '32px',
                      height: '32px',
                      borderRadius: '50%',
                      overflow: 'hidden',
                      flexShrink: 0,
                    }}
                  >
                    {block.user.avatar_url ? (
                      <img
                        src={block.user.avatar_url}
                        alt={block.user.username}
                        style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                      />
                    ) : (
                      <svg
                        width={32}
                        height={32}
                        viewBox="0 0 224 224"
                        fill="none"
                        xmlns="http://www.w3.org/2000/svg"
                        aria-label="Avatar"
                      >
                        <path
                          d="M112 224C96.7007 224 82.2797 221.072 68.7373 215.216C55.268 209.36 43.3725 201.271 33.051 190.949C22.7294 180.628 14.6405 168.732 8.78431 155.263C2.9281 141.72 0 127.299 0 112C0 96.7008 2.9281 82.3165 8.78431 68.8472C14.6405 55.3047 22.6928 43.3727 32.9412 33.0511C43.2627 22.7295 55.1582 14.6406 68.6274 8.78442C82.1699 2.92821 96.5908 0.000106812 111.89 0.000106812C127.19 0.000106812 141.61 2.92821 155.153 8.78442C168.695 14.6406 180.627 22.7295 190.949 33.0511C201.271 43.3727 209.359 55.3047 215.216 68.8472C221.072 82.3165 224 96.7008 224 112C224 127.299 221.072 141.72 215.216 155.263C209.359 168.732 201.271 180.628 190.949 190.949C180.627 201.271 168.695 209.36 155.153 215.216C141.684 221.072 127.299 224 112 224ZM112 207.31C120.345 207.31 128.617 206.175 136.816 203.906C145.088 201.71 152.847 198.489 160.094 194.243C167.341 190.071 173.783 185.02 179.42 179.09C175.467 172.795 170.05 167.451 163.169 163.059C156.361 158.594 148.565 155.226 139.78 152.957C131.069 150.614 121.809 149.443 112 149.443C102.044 149.443 92.6745 150.614 83.8902 152.957C75.1059 155.299 67.3098 158.703 60.502 163.169C53.7673 167.561 48.4235 172.868 44.4706 179.09C50.1072 185.02 56.549 190.071 63.7961 194.243C71.1163 198.489 78.8758 201.71 87.0745 203.906C95.2732 206.175 103.582 207.31 112 207.31ZM112 130.777C119.027 130.85 125.359 129.093 130.996 125.506C136.706 121.846 141.244 116.868 144.612 110.573C147.979 104.277 149.663 97.2132 149.663 89.3805C149.663 81.987 147.979 75.2158 144.612 69.0668C141.244 62.9178 136.706 58.0132 130.996 54.353C125.286 50.6197 118.954 48.753 112 48.753C104.973 48.753 98.6039 50.6197 92.8941 54.353C87.1843 58.0132 82.6457 62.9178 79.2784 69.0668C75.9111 75.2158 74.264 81.987 74.3372 89.3805C74.3372 97.2132 75.9843 104.241 79.2784 110.463C82.6457 116.685 87.1477 121.626 92.7843 125.286C98.4941 128.873 104.899 130.703 112 130.777Z"
                          fill="#8D8D93"
                        />
                      </svg>
                    )}
                  </div>
                  <span
                    style={{
                      fontSize: '14px',
                      color: '#ffffff',
                      fontWeight: '700',
                    }}
                  >
                    {block.user.username}
                  </span>
                  <span
                    style={{
                      fontSize: '12px',
                      lineHeight: '12px',
                      color: 'rgba(235, 235, 245, 0.60)',
                      fontWeight: '400',
                    }}
                  >
                    {timeAgo(block.created_at)}
                  </span>
                </div>
              )}
            </div>
          </div>
        </div>

        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'flex-start',
            gap: '32px',
            width: '100%',
            minWidth: 0,
            flexWrap: 'nowrap',
            boxSizing: 'border-box',
          }}
        >
          {/* Expanded Model Selector */}

          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'flex-start',
              gap: '12px',
              flex: '1 1 auto',
              minWidth: 0,
            }}
          >
            <div style={{ width: '100%', minWidth: 0 }}>
              <ModelSelect
                options={block.models.map((m) => ({ id: String(m.id), name: m.name }))}
                value={String(block.activeModelId)}
                onChange={handleModelSelect}
              />
            </div>
            {namSlimmable && (
              <div
                style={{
                  display: 'flex',
                  flexDirection: 'row',
                  flex: '0 0 auto',
                  alignSelf: 'flex-start',
                  width: 'max-content',
                  maxWidth: '100%',
                  boxSizing: 'border-box',
                  borderRadius: '8px',
                  border: '1px solid rgba(84, 84, 88, 0.65)',
                  overflow: 'hidden',
                  opacity: block.loaded && !isSwitchingModel ? 1 : 0.45,
                  pointerEvents: block.loaded && !isSwitchingModel ? 'auto' : 'none',
                }}
              >
                <button
                  type="button"
                  onClick={() => handleNamSizeMode(true)}
                  style={{
                    padding: '6px 14px',
                    fontSize: '12px',
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
                    padding: '6px 14px',
                    fontSize: '12px',
                    fontWeight: 700,
                    border: 'none',
                    borderLeft: '1px solid rgba(84, 84, 88, 0.65)',
                    cursor: 'pointer',
                    backgroundColor: !isLite ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
                    color: '#ffffff',
                  }}
                >
                  FULL
                </button>
              </div>
            )}
          </div>

          {/* Knobs */}
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '24px',
              marginTop: '-24px',
              flex: '0 0 auto',
            }}
          >
            <KnobControl
              label="Gain"
              value={outputGain}
              onChange={handleGainChange}
              size={46}
              labelSize={12}
              labelBottom={false}
              innerColor="#151517"
            />
            <KnobControl
              label="Mix"
              value={mix}
              onChange={handleMixChange}
              size={46}
              labelSize={12}
              labelBottom={false}
              innerColor="#151517"
            />
          </div>
        </div>
      </div>
    </div>
  );
};
