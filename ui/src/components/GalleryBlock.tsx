import React, { useCallback, useEffect, useState } from 'react';
import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { ArrowLeftRight, GripVertical, PlusCircle, Power, Trash2 } from 'lucide-react';
import { BlockMeter } from './BlockMeter';
import { meterId } from '../hooks/useMeters';
import { useChainActions } from '../hooks/useChainActions';
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

const NoImageFallback: React.FC = () => (
  <div
    style={{
      width: '100%',
      height: '100%',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      backgroundColor: SURFACE_RAISED,
      color: GRAY,
      fontSize: '11px',
      letterSpacing: '0.06em',
    }}
  >
    NO IMAGE
  </div>
);

/** Interactive wiring for a tile's chrome; omitted for the drag ghost. */
interface TileActions {
  onOpen: () => void;
  onTogglePower: (e: React.MouseEvent) => void;
  onSwap: (e: React.MouseEvent) => void;
  onRemove: (e: React.MouseEvent) => void;
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

  return (
    <div
      // Header reveals on :hover via CSS (see index.css) — JS hover state
      // dies across drag re-renders. The inert drag ghost pins it visible.
      className={actions ? 'gallery-tile' : 'gallery-tile tile-chrome-visible'}
      onClick={actions?.onOpen}
      title={tone.title}
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
      {/* Tone image (dimmed while powered off or still loading) */}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          opacity: enabled && block.loaded ? 1 : 0.35,
          transition: 'opacity 0.2s ease',
        }}
      >
        {tone.images?.[0] ? (
          <img
            src={tone.images[0]}
            alt={tone.title}
            draggable={false}
            style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }}
          />
        ) : (
          <NoImageFallback />
        )}
      </div>

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
          title="Drag to reorder"
          style={{ ...actionButtonStyle, cursor: 'grab', color: '#ffffff' }}
        >
          <GripVertical size={14} />
        </div>
        <div style={{ flex: 1 }} />
        <button
          onClick={actions?.onTogglePower}
          onMouseDown={preventFocus}
          title={enabled ? 'Turn block off' : 'Turn block on'}
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
          title="Swap tone"
          style={{ ...actionButtonStyle, color: '#ffffff' }}
        >
          <ArrowLeftRight size={14} />
        </button>
        <button
          onClick={actions?.onRemove}
          onMouseDown={preventFocus}
          title="Remove block"
          style={{ ...actionButtonStyle, color: '#ffffff' }}
        >
          <Trash2 size={14} />
        </button>
      </div>

      {/* Bottom strip: simplified horizontal output level + latching clip */}
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          position: 'absolute',
          bottom: 0,
          left: 0,
          right: 0,
          display: 'flex',
          justifyContent: 'center',
          padding: '6px 0 7px',
        }}
      >
        <BlockMeter
          meterId={meterId.blockOut(blockId)}
          orientation="horizontal"
          length={size - 20}
        />
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

interface AddTileProps {
  /** Insert slot block id ('select-insert' or 'select-insert-right'). */
  id: string;
  size: number;
  routing: AddTileRouting;
  /** Reorderable via its grip. Off for an empty lane's tiles (nothing to
      reorder around) — the grip hides too. */
  draggable?: boolean;
  /** Accepts drops (cross-lane moves land before it). Off for the purely
      decorative second tile of an empty lane's pair. */
  droppable?: boolean;
  onClick: () => void;
}

/** The insert slot as a dashed add tile — sortable so the insert point can be
    repositioned within its lane, like any other block. Routing lines continue
    the lane's connector line through to the plus circle. */
export const AddTile: React.FC<AddTileProps> = ({
  id,
  size,
  routing,
  draggable = true,
  droppable = true,
  onClick,
}) => {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id,
    disabled: { draggable: !draggable, droppable: !droppable },
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
      title="Add tone"
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
      {draggable && (
        <div
          {...attributes}
          {...listeners}
          onClick={(e) => e.stopPropagation()}
          title="Drag to reorder"
          style={{
            position: 'absolute',
            top: '4px',
            left: '4px',
            ...actionButtonStyle,
            cursor: 'grab',
            color: '#ffffff',
          }}
        >
          <GripVertical size={14} />
        </div>
      )}
      <PlusCircle size={40} strokeWidth={1} />
    </div>
  );
};

/** Tile clone for the DragOverlay: the full tile surface at reduced opacity
    (identical chrome, dimmed like the old chain's dragged card), following
    the pointer so drags can cross lanes without being clipped by the lane's
    overflow. */
export const GalleryTileGhost: React.FC<{ item: ChainItem; size: number }> = ({ item, size }) => {
  if (isInsertSlot(item)) {
    return (
      <div style={{ ...addTileFaceStyle(size), opacity: DRAG_GHOST_OPACITY, cursor: 'grabbing' }}>
        <div
          style={{
            position: 'absolute',
            top: '4px',
            left: '4px',
            ...actionButtonStyle,
            cursor: 'grabbing',
            color: '#ffffff',
          }}
        >
          <GripVertical size={14} />
        </div>
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
