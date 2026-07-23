import React, { useCallback, useEffect, useState } from 'react';
import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { ArrowLeftRight, GripVertical, PlusCircle, Power, Trash2 } from 'lucide-react';
import { BlockEnergyBorder, BlockLed } from './BlockLed';
import { ToneImage } from './GearIcon';
import { LoadingDots } from './LoadingDots';
import { RetryLoadBadge } from './RetryLoadBadge';
import { meterId } from '../hooks/useMeters';
import { useChainActions } from '../hooks/useChainActions';
import { HELP, helpProps, toneTileHelp } from './helpText';
import type { ChainItem, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';
import { GRAY, HIGHLIGHT, SURFACE, SURFACE_RAISED, iconButtonStyle } from './theme';

/**
 * Gallery view of a chain block: a square tone image with quick actions
 * (drag / power / swap / trash) overlaid along the top edge and a simplified
 * horizontal output level + clip strip along the bottom. Clicking the tile
 * opens the detailed card view.
 *
 * The full visual surface (TileSurface) is shared between the sortable tile
 * and the DragOverlay ghost, so the copy that follows the pointer during a
 * drag looks identical to the resting tile — just semi-transparent.
 */

/** Opacity of the moving copy while dragging (matches the old chain). */
const DRAG_GHOST_OPACITY = 0.75;

const actionButtonStyle = iconButtonStyle(22);

/** Keep tile buttons from taking focus on press — the webview scrolls the
    focused element into view, which nudges the whole lane by a pixel. */
const preventFocus = (e: React.MouseEvent) => e.preventDefault();

/** Interactive wiring for a tile's chrome; omitted for the drag ghost. */
interface TileActions {
  onOpen: () => void;
  onTogglePower: (e: React.MouseEvent) => void;
  onSwap: (e: React.MouseEvent) => void;
  onRemove: (e: React.MouseEvent) => void;
  /** Retry a failed model download (shown when block.loadFailed). */
  onRetryLoad: () => void;
  /** useSortable attributes + listeners for the grip. */
  grip: React.HTMLAttributes<HTMLDivElement>;
}

/**
 * The complete tile visual: artwork, loading scrim, top action strip and
 * bottom meter. Rendered inert (no handlers) inside the DragOverlay so the
 * moving copy matches the resting tile exactly.
 */
const TileSurface: React.FC<{
  block: ToneBlock;
  size: number;
  enabled: boolean;
  actions?: TileActions;
}> = ({ block, size, enabled, actions }) => {
  const { blockId, tone } = block;

  // A model download/prepare is in flight: `modelLoading` covers switches
  // (where the previous model keeps playing, so `loaded` stays true) and
  // `!loaded` covers fresh blocks that have nothing to play yet.
  const busy = block.modelLoading || (!block.loaded && !block.loadFailed);
  const outMeterId = meterId.blockOut(blockId);

  return (
    // Outer shell stays overflow-visible so inset energy glow isn't needed
    // outside the tile; kept for a stable size box around the face.
    <div
      style={{
        width: `${size}px`,
        height: `${size}px`,
        position: 'relative',
        flexShrink: 0,
      }}
    >
      <div
        // Header reveals on :hover via CSS (see index.css) — JS hover state
        // dies across drag re-renders. The inert drag ghost pins it visible.
        className={actions ? 'gallery-tile' : 'gallery-tile tile-chrome-visible'}
        onClick={actions?.onOpen}
        // The inert drag ghost skips help — it rides under the pointer, so its
        // hover events would pin the hint for the whole drag.
        {...(actions ? helpProps(toneTileHelp(tone.title)) : {})}
        style={{
          width: `${size}px`,
          height: `${size}px`,
          borderRadius: '12px',
          backgroundColor: SURFACE,
          position: 'relative',
          overflow: 'hidden',
          cursor: actions ? 'pointer' : 'grabbing',
          boxSizing: 'border-box',
        }}
      >
        {/* Tone image (dimmed while powered off, loading, or failed) */}
        <div
          style={{
            position: 'absolute',
            inset: 0,
            opacity: enabled && !busy && !block.loadFailed ? 1 : 0.35,
            transition: 'opacity 0.2s ease',
          }}
        >
          <ToneImage
            src={tone.images?.[0]}
            alt={tone.title}
            gear={tone.gear}
            boxSize={size}
            draggable={false}
          />
        </div>

        {/* Busy dots while the model downloads natively; if the download
            failed, a retry affordance instead (dots would spin forever). */}
        {(busy || block.loadFailed) && (
          <div
            style={{
              position: 'absolute',
              inset: 0,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              // Clicks pass through to the tile except on the retry button.
              pointerEvents: 'none',
            }}
          >
            {block.loadFailed && actions ? (
              <div style={{ pointerEvents: 'auto' }}>
                <RetryLoadBadge onRetry={actions.onRetryLoad} />
              </div>
            ) : (
              !block.loadFailed && <LoadingDots />
            )}
          </div>
        )}

        {/* Translucent strip under the quick actions so they read on any art.
            Fades in with the header (opacity only — never a layout change). */}
        <div
          className="tile-chrome"
          style={{
            position: 'absolute',
            top: 0,
            left: 0,
            right: 0,
            height: '32px',
            background: 'rgba(0, 0, 0, 0.35)',
            pointerEvents: 'none',
          }}
        />

        {/* Top quick-action bar: drag / power / swap / trash (hover-revealed) */}
        <div
          className="tile-chrome"
          style={{
            position: 'absolute',
            top: 0,
            left: 0,
            right: 0,
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            padding: '4px',
          }}
        >
          <div
            {...(actions?.grip ?? {})}
            onClick={(e) => e.stopPropagation()}
            {...(actions ? helpProps(HELP.dragGrip) : {})}
            // touch-action: none — otherwise touch devices claim the gesture
            // for lane scrolling and pointercancel kills the drag instantly.
            style={{ ...actionButtonStyle, cursor: 'grab', color: '#ffffff', touchAction: 'none' }}
          >
            <GripVertical size={14} />
          </div>
          <div style={{ flex: 1 }} />
          <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
            <button
              onClick={actions?.onTogglePower}
              onMouseDown={preventFocus}
              {...(actions ? helpProps(HELP.blockPower) : {})}
              style={{
                ...actionButtonStyle,
                color: enabled ? '#ffffff' : GRAY,
                backgroundColor: enabled ? 'transparent' : HIGHLIGHT,
              }}
            >
              <Power size={14} />
            </button>
            <button
              onClick={actions?.onSwap}
              onMouseDown={preventFocus}
              {...(actions ? helpProps(HELP.swapTone) : {})}
              style={{ ...actionButtonStyle, color: '#ffffff' }}
            >
              <ArrowLeftRight size={14} />
            </button>
            <button
              onClick={actions?.onRemove}
              onMouseDown={preventFocus}
              {...(actions ? helpProps(HELP.removeBlock) : {})}
              style={{ ...actionButtonStyle, color: '#ffffff' }}
            >
              <Trash2 size={14} />
            </button>
          </div>
        </div>

        {/* Clip latch lives outside the overflow:hidden face so it stacks
            above the inset glow; red dot only while clipped. */}
      </div>

      <BlockEnergyBorder meterId={outMeterId} borderRadius={12} />
      <div style={{ position: 'absolute', bottom: '8px', right: '8px', zIndex: 4 }}>
        <BlockLed meterId={outMeterId} size={10} />
      </div>
    </div>
  );
};

interface GalleryBlockProps {
  block: ToneBlock;
  /** Tile edge, px. */
  size: number;
  /** Open the detail takeover for this block. */
  onOpen: (blockId: string) => void;
}

/** Memoized — a lane re-render (e.g. another tile's optimistic state) only
    reaches tiles whose block snapshot actually changed. Mutations come from
    the ChainActions context, so there are no per-render callback props to
    defeat the memo. */
export const GalleryBlock: React.FC<GalleryBlockProps> = React.memo(({ block, size, onOpen }) => {
  const { blockId, params } = block;
  const actions = useChainActions();

  // Optimistic power state; native converges via the chainChanged resync
  // (same pattern as the detail card).
  const [enabled, setEnabled] = useState(params.enabled);
  useEffect(() => setEnabled(params.enabled), [params.enabled]);

  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id: blockId,
  });

  const handleTogglePower = useCallback(
    (e: React.MouseEvent) => {
      e.stopPropagation();
      setEnabled((prev) => {
        actions.setBlockParam(blockId, 'enabled', !prev);
        return !prev;
      });
    },
    [actions, blockId]
  );

  return (
    <div
      ref={setNodeRef}
      style={{
        // Translate only (no scale) — scale transforms cause subpixel jitter
        // on the overlaid controls.
        transform: CSS.Translate.toString(transform),
        transition: isDragging ? 'none' : transition,
        // The DragOverlay ghost is the moving copy; hiding the original
        // reveals the plus-circle rail behind the vacated slot.
        opacity: isDragging ? 0 : 1,
        flexShrink: 0,
      }}
    >
      <TileSurface
        block={block}
        size={size}
        enabled={enabled}
        actions={{
          onOpen: () => onOpen(blockId),
          onTogglePower: handleTogglePower,
          onSwap: (e) => {
            e.stopPropagation();
            actions.swapBlock(blockId);
          },
          onRemove: (e) => {
            e.stopPropagation();
            actions.removeBlock(blockId);
          },
          onRetryLoad: () => actions.retryLoad(blockId),
          grip: { ...attributes, ...listeners },
        }}
      />
    </div>
  );
});
GalleryBlock.displayName = 'GalleryBlock';

/** Radius of the PlusCircle glyph — routing lines run edge-to-circle. */
const PLUS_CIRCLE_RADIUS = 20;

/** Which tile edges get a routing line into the plus circle (signal-flow
    continuation of the lane's connector lines). */
export type AddTileRouting = 'left' | 'right' | 'both' | 'none';

/** Shared face of the insert slot tile (also used by the drag ghost). */
const addTileFaceStyle = (size: number): React.CSSProperties => ({
  width: `${size}px`,
  height: `${size}px`,
  borderRadius: '12px',
  backgroundColor: SURFACE_RAISED,
  // https://kovart.github.io/dashed-border-generator/
  backgroundImage: `url("data:image/svg+xml,%3csvg width='100%25' height='100%25' xmlns='http://www.w3.org/2000/svg'%3e%3crect width='100%25' height='100%25' fill='none' rx='12' ry='12' stroke='%238D8D93FF' stroke-width='2' stroke-dasharray='6%2c 10' stroke-dashoffset='9' stroke-linecap='square'/%3e%3c/svg%3e")`,
  position: 'relative',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  color: '#ffffff',
  flexShrink: 0,
  boxSizing: 'border-box',
});

/** Header chrome for an insert tile, mirroring the tone tiles' layout: the
    drag grip at the top-left. No translucent strip — there's no artwork to
    read against, only the tile's flat surface. Revealed on hover via the
    shared `.gallery-tile .tile-chrome` CSS (always visible on touch-only
    devices — see index.css); the drag ghost pins it with tile-chrome-visible. */
const AddTileHeader: React.FC<{ grip?: React.HTMLAttributes<HTMLDivElement> }> = ({ grip }) => (
  <div
    className="tile-chrome"
    style={{
      position: 'absolute',
      top: 0,
      left: 0,
      right: 0,
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      padding: '4px',
    }}
  >
    <div
      {...(grip ?? {})}
      onClick={(e) => e.stopPropagation()}
      {...(grip ? helpProps(HELP.dragGrip) : {})}
      // touch-action: none — otherwise touch devices claim the gesture
      // for lane scrolling and pointercancel kills the drag instantly.
      style={{
        ...actionButtonStyle,
        cursor: grip ? 'grab' : 'grabbing',
        color: '#ffffff',
        touchAction: 'none',
      }}
    >
      <GripVertical size={14} />
    </div>
  </div>
);

interface AddTileProps {
  /** Insert slot block id. */
  id: string;
  size: number;
  routing: AddTileRouting;
  onClick: () => void;
}

/** The insert slot as a dashed add tile — sortable so the insert point can be
    repositioned within its lane, like any other block. Routing lines continue
    the lane's connector line through to the plus circle. */
export const AddTile: React.FC<AddTileProps> = ({ id, size, routing, onClick }) => {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id,
  });

  const routingLine = (edge: 'left' | 'right') => (
    <div
      style={{
        position: 'absolute',
        top: '50%',
        [edge]: 0,
        width: `${size / 2 - PLUS_CIRCLE_RADIUS}px`,
        height: '1px',
        backgroundColor: '#ffffff',
      }}
    />
  );

  return (
    <div
      ref={setNodeRef}
      onClick={onClick}
      className="gallery-tile"
      {...helpProps(HELP.addTile)}
      style={{
        ...addTileFaceStyle(size),
        transform: CSS.Translate.toString(transform),
        transition: isDragging ? 'none' : transition,
        opacity: isDragging ? 0 : 1,
        cursor: 'pointer',
      }}
    >
      {!isDragging && (routing === 'left' || routing === 'both') && routingLine('left')}
      {!isDragging && (routing === 'right' || routing === 'both') && routingLine('right')}
      <AddTileHeader grip={{ ...attributes, ...listeners }} />
      <PlusCircle size={40} strokeWidth={1} />
    </div>
  );
};

/** Tile clone for the DragOverlay: the full tile surface at reduced opacity
    (identical chrome, dimmed like the old chain's dragged card), following
    the pointer so drags can cross lanes without being clipped by the lane's
    overflow. */
export const GalleryTileGhost: React.FC<{
  item: ChainItem;
  size: number;
}> = ({ item, size }) => {
  if (isInsertSlot(item)) {
    return (
      <div
        className="gallery-tile tile-chrome-visible"
        style={{ ...addTileFaceStyle(size), opacity: DRAG_GHOST_OPACITY, cursor: 'grabbing' }}
      >
        <AddTileHeader />
        <PlusCircle size={40} strokeWidth={1} />
      </div>
    );
  }

  return (
    <div style={{ opacity: DRAG_GHOST_OPACITY, cursor: 'grabbing' }}>
      <TileSurface block={item} size={size} enabled={item.params.enabled} />
    </div>
  );
};
