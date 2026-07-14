import React from 'react';
import { SortableContext, horizontalListSortingStrategy } from '@dnd-kit/sortable';
import { ArrowUpDown, Link, PlusCircle } from 'lucide-react';
import { GalleryBlock, AddTile } from './GalleryBlock';
import type { AddTileRouting } from './GalleryBlock';
import { KnobControl } from './KnobControl';
import { panScale } from './knobScale';

const PAN_LEFT_SCALE = panScale('left');
const PAN_RIGHT_SCALE = panScale('right');
import { PillIconButton } from './SpreadControls';
import { useParameter } from '../hooks/useParameter';
import { useChainActions } from '../hooks/useChainActions';
import type { ChainItem } from '../types/chain';
import { isInsertSlot } from '../types/chain';
/**
 * Lane-level pieces of the chain gallery (see ChainView for the drag
 * orchestration that owns them): the ghost rail, a single lane of tiles,
 * the scroll-edge fades and the stereo pan rail.
 */

export const TILE_SIZE = 224;
/** Stereo shows two lanes, so its tiles shrink to fit the fixed height. */
export const STEREO_TILE_SIZE = 176;
/** Gap between tiles — the visible run of each connector line. */
export const TILE_GAP = 24;
/** Vertical gap between the two stereo lanes. */
export const LANE_GAP = 24;
/** Radius of the ghost rail's PlusCircle glyphs (size 40). */
const RAIL_CIRCLE_RADIUS = 20;
/** Gutter inside the scroll area — tiles fade out under it while scrolling. */
export const EDGE_FADE_WIDTH = 32;

/** Signal-flow routing lines for an add tile at the given lane position. */
const addTileRouting = (index: number, count: number): AddTileRouting => {
  if (count <= 1) return 'none';
  if (index === 0) return 'right';
  if (index === count - 1) return 'left';
  return 'both';
};

/**
 * Static ghost rail behind a lane: one plus circle per slot, connector lines
 * between them. The circles sit hidden behind the (opaque) tiles and appear
 * when a slot is vacated mid-drag; only the line runs inside the gaps are
 * visible otherwise. Mirrors the old vertical chain's background exactly.
 */
const GhostRail: React.FC<{ slots: number; tileSize: number }> = ({ slots, tileSize }) => (
  <div
    style={{
      position: 'absolute',
      inset: 0,
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      gap: `${TILE_GAP}px`,
      pointerEvents: 'none',
      zIndex: 1,
    }}
  >
    {Array.from({ length: slots }, (_, i) => (
      <span
        key={`${i}-rail`}
        style={{
          width: `${tileSize}px`,
          height: `${tileSize}px`,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'relative',
          flexShrink: 0,
        }}
      >
        {i > 0 && (
          <div
            style={{
              position: 'absolute',
              left: `${-(TILE_GAP + tileSize / 2 - RAIL_CIRCLE_RADIUS)}px`,
              top: '50%',
              width: `${TILE_GAP + tileSize - 2 * RAIL_CIRCLE_RADIUS}px`,
              height: '1px',
              backgroundColor: '#ffffff',
            }}
          />
        )}
        <PlusCircle size={40} strokeWidth={1} />
      </span>
    ))}
  </div>
);

/** One lane of tiles over its ghost rail (no scroll of its own — both lanes
    share the outer scroll area). An empty lane (just its insert slot) shows
    the classic pair of add tiles joined by a connector, like the old chain. */
export const GalleryLane: React.FC<{
  items: ChainItem[];
  tileSize: number;
  onOpen: (blockId: string) => void;
  onAdd: () => void;
}> = ({ items, tileSize, onOpen, onAdd }) => {
  const emptyLane = items.length === 1 && isInsertSlot(items[0]);

  return (
    <div style={{ position: 'relative', width: 'max-content' }}>
      <GhostRail slots={emptyLane ? 2 : items.length} tileSize={tileSize} />
      <SortableContext
        items={items.map((item) => item.blockId)}
        strategy={horizontalListSortingStrategy}
      >
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: `${TILE_GAP}px`,
            position: 'relative',
            zIndex: 2,
          }}
        >
          {emptyLane ? (
            <>
              {/* The real insert slot stays droppable so cross-lane drags can
                  land in an empty lane; its twin is purely decorative. */}
              <AddTile
                id={items[0].blockId}
                size={tileSize}
                routing="right"
                draggable={false}
                onClick={onAdd}
              />
              <AddTile
                id={`${items[0].blockId}-pair`}
                size={tileSize}
                routing="left"
                draggable={false}
                droppable={false}
                onClick={onAdd}
              />
            </>
          ) : (
            items.map((item, index) =>
              isInsertSlot(item) ? (
                <AddTile
                  key={item.blockId}
                  id={item.blockId}
                  size={tileSize}
                  routing={addTileRouting(index, items.length)}
                  onClick={onAdd}
                />
              ) : (
                <GalleryBlock key={item.blockId} block={item} size={tileSize} onOpen={onOpen} />
              )
            )
          )}
        </div>
      </SortableContext>
    </div>
  );
};

/**
 * Right rail for stereo: per-lane pan knobs (each centered on its lane), with
 * the link toggle and whole-chain swap on the seam between them. Constant-
 * power pan positions (0 = hard left, 1 = hard right): Pan L covers hard
 * left..center on a half track, Pan R center..hard right. Linked (default)
 * mirrors the knobs so width changes stay symmetric.
 */
export const StereoPanRail: React.FC = () => {
  const { swapChains } = useChainActions();
  const [panLeft, setPanLeft] = useParameter('chainPanLeft', 'slider');
  const [panRight, setPanRight] = useParameter('chainPanRight', 'slider');
  const [linked, setLinked] = useParameter('chainPanLinked', 'toggle');

  const handlePanLeft = (value: number) => {
    setPanLeft(value);
    if (linked) setPanRight(1 - value);
  };
  const handlePanRight = (value: number) => {
    setPanRight(value);
    if (linked) setPanLeft(1 - value);
  };
  const handleToggleLink = () => {
    const next = !linked;
    setLinked(next);
    // Re-linking snaps back to a symmetric image, anchored on the left pan.
    if (next) setPanRight(1 - panLeft);
  };

  const centered: React.CSSProperties = {
    flex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
  };

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        alignSelf: 'center',
        height: `${STEREO_TILE_SIZE * 2 + LANE_GAP}px`,
        flexShrink: 0,
      }}
    >
      <div style={centered}>
        <KnobControl
          label="Pan L"
          value={panLeft}
          onChange={handlePanLeft}
          variant="panLeft"
          min={0}
          max={0.5}
          size={30}
          labelSize={10}
          scale={PAN_LEFT_SCALE}
          defaultValue={0}
        />
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '8px' }}>
        <PillIconButton
          on={linked}
          title={linked ? 'Unlink pans (uneven image)' : 'Link pans (mirrored)'}
          onClick={handleToggleLink}
          offsetY={0}
        >
          <Link size={12} />
        </PillIconButton>
        <button
          onClick={swapChains}
          title="Swap Left and Right chains"
          style={{
            width: '22px',
            height: '22px',
            borderRadius: '6px',
            border: 'none',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            cursor: 'pointer',
            padding: 0,
            color: '#ffffff',
            background: 'transparent',
          }}
        >
          <ArrowUpDown size={12} />
        </button>
      </div>
      <div style={centered}>
        <KnobControl
          label="Pan R"
          value={panRight}
          onChange={handlePanRight}
          variant="panRight"
          min={0.5}
          max={1}
          size={30}
          labelSize={10}
          scale={PAN_RIGHT_SCALE}
          defaultValue={1}
        />
      </div>
    </div>
  );
};

/** Fade the lanes out under the gutters as they scroll — content slides
    behind a smooth ramp to the background instead of hard-clipping. */
export const EdgeFade: React.FC<{ side: 'left' | 'right' }> = ({ side }) => (
  <div
    style={{
      position: 'absolute',
      top: 0,
      bottom: 0,
      [side]: 0,
      width: `${EDGE_FADE_WIDTH}px`,
      background: `linear-gradient(to ${side === 'left' ? 'right' : 'left'}, #000000, rgba(0, 0, 0, 0))`,
      pointerEvents: 'none',
      zIndex: 3,
    }}
  />
);
