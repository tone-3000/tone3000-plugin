import React, { useEffect, useRef, useState } from 'react';
import {
  DndContext,
  DragOverlay,
  closestCenter,
  pointerWithin,
  KeyboardSensor,
  PointerSensor,
  useSensor,
  useSensors,
} from '@dnd-kit/core';
import type {
  CollisionDetection,
  DragEndEvent,
  DragOverEvent,
  DragStartEvent,
} from '@dnd-kit/core';
import {
  SortableContext,
  arrayMove,
  sortableKeyboardCoordinates,
  horizontalListSortingStrategy,
} from '@dnd-kit/sortable';
import { ArrowUpDown, ChevronLeft, Link, PlusCircle } from 'lucide-react';
import { ChainBlock } from './ChainBlock';
import { GalleryBlock, AddTile, GalleryTileGhost } from './GalleryBlock';
import type { AddTileRouting } from './GalleryBlock';
import { KnobControl } from './KnobControl';
import { PillIconButton } from './SpreadControls';
import { CARD_WIDTH } from './chainLayout';
import { useParameter } from '../hooks/useParameter';
import type { BlockParamName, ChainItem, ChainSide, EqBand, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';

/**
 * Chain gallery: blocks render as square image tiles in horizontal,
 * left-to-right lanes over a static ghost rail of plus circles joined by
 * connector lines — the old vertical chain's link UI, rotated. Dragging a
 * tile away reveals the rail behind its slot.
 *
 * Mono shows one lane; stereo shows both L/R lanes in a single shared
 * scroll area with the pan/link/swap rail on the left. One drag context
 * spans both lanes and the lane lists are mirrored into optimistic local
 * state, so cross-lane drags reflow the target lane live (onDragOver) and
 * drops land without any snap-back while the native roundtrip completes.
 * Clicking a tile opens the detail takeover (the full card view) with a
 * Back button.
 */

const TILE_SIZE = 224;
/** Stereo shows two lanes, so its tiles shrink to fit the fixed height. */
const STEREO_TILE_SIZE = 176;
/** Gap between tiles — the visible run of each connector line. */
const TILE_GAP = 24;
/** Vertical gap between the two stereo lanes. */
const LANE_GAP = 24;
/** Radius of the ghost rail's PlusCircle glyphs (size 40). */
const RAIL_CIRCLE_RADIUS = 20;
/** Gutter inside the scroll area — tiles fade out under it while scrolling. */
const EDGE_FADE_WIDTH = 32;

const MUTED = 'rgba(235, 235, 245, 0.60)';

interface ChainViewProps {
  /** Left lane (the only lane in mono mode). */
  chain: ChainItem[];
  /** Right lane, or null while mono. */
  chainRight: ChainItem[] | null;
  /** Launch the Select flow, adding to the given lane's insert slot. */
  onAddModel: (side: ChainSide) => void;
  onRemoveBlock: (id: string) => void;
  onSwapBlock: (id: string) => void;
  onShareBlock: (block: ToneBlock) => Promise<boolean>;
  /** Reorder one lane; the ids identify which lane natively. */
  onReorderItems: (orderedIds: string[]) => void;
  /** Move a block into the other lane at the given index (stereo drag). */
  onMoveBlock: (blockId: string, side: ChainSide, index: number) => void;
  onSwapChains: () => void;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
  onSetBlockParam: (blockId: string, param: BlockParamName, value: number | boolean) => void;
  onSetBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => void;
  onSetBlockEqEnabled: (blockId: string, enabled: boolean) => void;
  onResetBlockEq: (blockId: string) => void;
  sampleRate: number;
}

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
const GalleryLane: React.FC<{
  items: ChainItem[];
  tileSize: number;
  onOpen: (blockId: string) => void;
  onAdd: () => void;
  onRemove: (id: string) => void;
  onSwap: (id: string) => void;
  onSetEnabled: (id: string, enabled: boolean) => void;
}> = ({ items, tileSize, onOpen, onAdd, onRemove, onSwap, onSetEnabled }) => {
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
                <GalleryBlock
                  key={item.blockId}
                  block={item}
                  size={tileSize}
                  onOpen={onOpen}
                  onRemove={onRemove}
                  onSwap={onSwap}
                  onSetEnabled={onSetEnabled}
                />
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
const StereoPanRail: React.FC<{ onSwapChains: () => void }> = ({ onSwapChains }) => {
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
          onClick={onSwapChains}
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
            color: MUTED,
            background: 'transparent',
          }}
        >
          <ArrowUpDown size={14} />
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
        />
      </div>
    </div>
  );
};

/** Fade the lanes out under the gutters as they scroll — content slides
    behind a smooth ramp to the background instead of hard-clipping. */
const EdgeFade: React.FC<{ side: 'left' | 'right' }> = ({ side }) => (
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

/**
 * Prefer the tile actually under the pointer; only fall back to nearest-
 * center when the pointer is in a gap. closestCenter alone oscillates during
 * cross-lane drags: right after a move shifts the layout, stale rects can
 * make the "nearest" target flip back to the old lane.
 */
const galleryCollisionDetection: CollisionDetection = (args) => {
  const pointerCollisions = pointerWithin(args);
  return pointerCollisions.length > 0 ? pointerCollisions : closestCenter(args);
};

type Lanes = Record<ChainSide, ChainItem[]>;

export const ChainView: React.FC<ChainViewProps> = ({
  chain,
  chainRight,
  onAddModel,
  onRemoveBlock,
  onSwapBlock,
  onShareBlock,
  onReorderItems,
  onMoveBlock,
  onSwapChains,
  onSwitchModel,
  onSetBlockParam,
  onSetBlockEqBand,
  onSetBlockEqEnabled,
  onResetBlockEq,
  sampleRate,
}) => {
  const [detailBlockId, setDetailBlockId] = useState<string | null>(null);
  /** The item under drag — drives the DragOverlay ghost. */
  const [activeDrag, setActiveDrag] = useState<ChainItem | null>(null);

  /**
   * Optimistic mirror of both lanes. Drag gestures mutate this immediately
   * (live cross-lane reflow via onDragOver, final order on drop) so nothing
   * snaps back while the native mutation + poll roundtrip completes; it
   * resyncs from props whenever native reports a new state and no drag is
   * in flight.
   */
  const [lanes, setLanes] = useState<Lanes>({ left: chain, right: chainRight ?? [] });
  const draggingRef = useRef(false);
  /**
   * Set for one frame after a cross-lane move. dnd-kit fires another
   * onDragOver as soon as the layout shifts, before it has re-measured — and
   * with stale rects the nearest target can resolve back to the old lane,
   * bouncing the item between lanes forever (React's "maximum update depth"
   * crash). Cross-lane moves are skipped until the next animation frame,
   * when the new measurements are in.
   */
  const justCrossedRef = useRef(false);

  useEffect(() => {
    if (!draggingRef.current) setLanes({ left: chain, right: chainRight ?? [] });
    requestAnimationFrame(() => {
      justCrossedRef.current = false;
    });
  }, [chain, chainRight, lanes]);

  const sensors = useSensors(
    // A few px of travel before a drag engages: plain clicks (open detail,
    // tile buttons) stay clicks, and there's no transform jitter on press.
    useSensor(PointerSensor, { activationConstraint: { distance: 6 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates })
  );

  const setEnabled = (id: string, enabled: boolean) => onSetBlockParam(id, 'enabled', enabled);

  /** Lane containing the id in the optimistic local state. */
  const laneOf = (id: string): ChainSide | null => {
    if (lanes.left.some((item) => item.blockId === id)) return 'left';
    if (lanes.right.some((item) => item.blockId === id)) return 'right';
    return null;
  };
  /** Lane containing the id per native state (the pre-drag origin). */
  const originLaneOf = (id: string): ChainSide | null => {
    if (chain.some((item) => item.blockId === id)) return 'left';
    if (chainRight?.some((item) => item.blockId === id)) return 'right';
    return null;
  };

  const resetLanes = () => setLanes({ left: chain, right: chainRight ?? [] });

  const handleDragStart = (event: DragStartEvent) => {
    draggingRef.current = true;
    const id = String(event.active.id);
    setActiveDrag([...lanes.left, ...lanes.right].find((item) => item.blockId === id) ?? null);
  };

  // Live cross-lane reflow: as the pointer crosses into the other lane, move
  // the dragged item into it so that lane parts to make room, exactly like a
  // same-lane sort. Which side of the hovered tile it lands on follows the
  // dragged tile's center.
  const handleDragOver = (event: DragOverEvent) => {
    const { active, over } = event;
    if (!over) return;
    const activeId = String(active.id);
    const overId = String(over.id);
    const from = laneOf(activeId);
    const to = laneOf(overId);
    if (!from || !to || from === to) return;
    if (justCrossedRef.current) return;

    // Insert slots are lane anchors and stay put.
    const item = lanes[from].find((i) => i.blockId === activeId);
    if (!item || isInsertSlot(item)) return;

    // Land after the hovered tile when the dragged tile's center has passed
    // the hovered tile's center.
    const activeRect = active.rect.current.translated;
    const landAfter =
      activeRect != null &&
      activeRect.left + activeRect.width / 2 > over.rect.left + over.rect.width / 2;

    justCrossedRef.current = true;
    setLanes((prev) => {
      const fromItems = prev[from].filter((i) => i.blockId !== activeId);
      const toItems = [...prev[to]];
      const overIndex = toItems.findIndex((i) => i.blockId === overId);
      const insertIndex =
        overIndex === -1 ? toItems.length : overIndex + (landAfter ? 1 : 0);
      toItems.splice(insertIndex, 0, item);
      return { ...prev, [from]: fromItems, [to]: toItems };
    });
  };

  const handleDragEnd = (event: DragEndEvent) => {
    draggingRef.current = false;
    setActiveDrag(null);
    const { active, over } = event;
    if (!over) {
      resetLanes();
      return;
    }

    const activeId = String(active.id);
    const overId = String(over.id);
    const side = laneOf(activeId);
    if (!side) return;

    // Final same-lane placement (cross-lane moves already happened in
    // onDragOver, so active and over share a lane by now).
    let laneItems = lanes[side];
    const oldIndex = laneItems.findIndex((i) => i.blockId === activeId);
    const newIndex = laneItems.findIndex((i) => i.blockId === overId);
    if (oldIndex !== -1 && newIndex !== -1 && oldIndex !== newIndex) {
      laneItems = arrayMove(laneItems, oldIndex, newIndex);
      setLanes((prev) => ({ ...prev, [side]: laneItems }));
    }

    // Commit to native: a lane change is one moveBlockToChain (exact final
    // index); a same-lane shuffle is one reorder. The next poll converges
    // the optimistic state.
    const origin = originLaneOf(activeId);
    if (origin && origin !== side) {
      onMoveBlock(
        activeId,
        side,
        laneItems.findIndex((i) => i.blockId === activeId)
      );
      return;
    }
    const nativeIds = (side === 'left' ? chain : chainRight ?? []).map((i) => i.blockId);
    const localIds = laneItems.map((i) => i.blockId);
    if (nativeIds.join() !== localIds.join()) onReorderItems(localIds);
  };

  const handleDragCancel = () => {
    draggingRef.current = false;
    setActiveDrag(null);
    resetLanes();
  };

  // Resolve the detail block across both lanes; it can disappear underneath
  // us (undo, trash from the detail header), in which case we fall back to
  // the gallery.
  const detailBlock =
    detailBlockId != null
      ? [...chain, ...(chainRight ?? [])].find(
          (item): item is ToneBlock => !isInsertSlot(item) && item.blockId === detailBlockId
        ) ?? null
      : null;

  if (detailBlock) {
    return (
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          gap: '12px',
          height: '100%',
          justifyContent: 'center',
          padding: '0 12px',
          boxSizing: 'border-box',
        }}
      >
        <div style={{ width: `${CARD_WIDTH}px`, maxWidth: '100%' }}>
          <button
            onClick={() => setDetailBlockId(null)}
            title="Back to chain"
            style={{
              display: 'flex',
              alignItems: 'center',
              background: 'transparent',
              border: 'none',
              color: '#ffffff',
              cursor: 'pointer',
              padding: '6px 8px 6px 0',
            }}
          >
            <ChevronLeft size={20} />
          </button>
        </div>
        {/* The detail card calls useSortable, so give it an inert drag scope. */}
        <DndContext>
          <SortableContext items={[detailBlock.blockId]}>
            <ChainBlock
              block={detailBlock}
              dragHandle={false}
              onRemove={(id) => {
                setDetailBlockId(null);
                onRemoveBlock(id);
              }}
              onSwap={onSwapBlock}
              onShare={onShareBlock}
              onSwitchModel={onSwitchModel}
              onSetParam={onSetBlockParam}
              onSetEqBand={onSetBlockEqBand}
              onSetEqEnabled={onSetBlockEqEnabled}
              onResetEq={onResetBlockEq}
              sampleRate={sampleRate}
            />
          </SortableContext>
        </DndContext>
      </div>
    );
  }

  const stereo = chainRight != null;
  const tileSize = stereo ? STEREO_TILE_SIZE : TILE_SIZE;

  const lane = (side: ChainSide) => (
    <GalleryLane
      items={lanes[side]}
      tileSize={tileSize}
      onOpen={setDetailBlockId}
      onAdd={() => onAddModel(side)}
      onRemove={onRemoveBlock}
      onSwap={onSwapBlock}
      onSetEnabled={setEnabled}
    />
  );

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'stretch',
        gap: '20px',
        height: '100%',
        boxSizing: 'border-box',
        padding: '0 24px',
      }}
    >
      <DndContext
        sensors={sensors}
        collisionDetection={galleryCollisionDetection}
        onDragStart={handleDragStart}
        onDragOver={handleDragOver}
        onDragEnd={handleDragEnd}
        onDragCancel={handleDragCancel}
      >
        {/* One shared scroll area — both lanes pan together, fading out under
            the edge gradients as they scroll. */}
        <div style={{ position: 'relative', flex: 1, minWidth: 0, display: 'flex' }}>
          <div
            className="hide-scrollbar"
            style={{
              flex: 1,
              minWidth: 0,
              overflowX: 'auto',
              overflowY: 'hidden',
              display: 'flex',
              flexDirection: 'column',
              justifyContent: 'center',
            }}
          >
            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                gap: `${LANE_GAP}px`,
                width: 'max-content',
                minWidth: '100%',
                padding: `0 ${EDGE_FADE_WIDTH}px`,
                boxSizing: 'border-box',
                // Vertical centering can leave the content on a half-pixel
                // boundary; keeping it on its own composited layer snaps it
                // to the pixel grid once, so tiles can't shift ~1px when a
                // drag or opacity fade promotes them to their own layers.
                transform: 'translateZ(0)',
              }}
            >
              {lane('left')}
              {stereo && lane('right')}
            </div>
          </div>
          <EdgeFade side="left" />
          <EdgeFade side="right" />
        </div>
        {/* No drop animation: the default one measures the original element
            before the optimistic lane commit paints, so the ghost would fly
            back to the old slot before the tile appears at the new one. */}
        <DragOverlay dropAnimation={null}>
          {activeDrag && <GalleryTileGhost item={activeDrag} size={tileSize} />}
        </DragOverlay>
      </DndContext>
      {stereo && <StereoPanRail onSwapChains={onSwapChains} />}
    </div>
  );
};
