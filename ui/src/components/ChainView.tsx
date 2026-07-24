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
import { arrayMove, sortableKeyboardCoordinates } from '@dnd-kit/sortable';
import { ChainBlock } from './ChainBlock';
import { GalleryTileGhost } from './GalleryBlock';
import {
  EdgeFade,
  GalleryLane,
  LANE_GAP,
  STEREO_TILE_SIZE,
  StereoPanRail,
  TILE_SIZE,
  EDGE_FADE_WIDTH,
} from './GalleryLane';
import { useChainActions } from '../hooks/useChainActions';
import type { ChainItem, ChainSide, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';

/**
 * Chain gallery: blocks render as square image tiles in horizontal,
 * left-to-right lanes over a static ghost rail of plus circles joined by
 * connector lines — the old vertical chain's link UI, rotated. Dragging a
 * tile away reveals the rail behind its slot. (Lane internals live in
 * GalleryLane.tsx; this component owns the drag orchestration.)
 *
 * Mono shows one lane; stereo shows both L/R lanes in a single shared
 * scroll area with the pan/link/swap rail on the left. One drag context
 * spans both lanes and the lane lists are mirrored into optimistic local
 * state, so cross-lane drags reflow the target lane live (onDragOver) and
 * drops land without any snap-back while the native roundtrip completes.
 * Clicking a tile opens the detail takeover (the full card view) with a
 * Back button.
 */

/**
 * The block whose detail takeover is open, persisted so it survives this
 * component unmounting while the tone browser (and its OAuth redirect) is up.
 */
const DETAIL_BLOCK_STORAGE_KEY = 't3k.detailBlockId';

interface ChainViewProps {
  /** Left lane (the only lane in mono mode). */
  chain: ChainItem[];
  /** Right lane, or null while mono. */
  chainRight: ChainItem[] | null;
  sampleRate: number;
}

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

export const ChainView: React.FC<ChainViewProps> = ({ chain, chainRight, sampleRate }) => {
  const actions = useChainActions();
  // Persisted so the detail takeover survives this component unmounting — a
  // swap from the detail view opens the tone browser (which replaces the whole
  // chain view, and may bounce through the tone3000.com OAuth redirect). The
  // swap keeps the same blockId, so we reopen the detail view for it on return.
  // Cleared when the user backs out, so gallery-initiated swaps land on the
  // gallery, not a stale detail view.
  const [detailBlockId, setDetailBlockId] = useState<string | null>(() =>
    sessionStorage.getItem(DETAIL_BLOCK_STORAGE_KEY)
  );
  useEffect(() => {
    if (detailBlockId) sessionStorage.setItem(DETAIL_BLOCK_STORAGE_KEY, detailBlockId);
    else sessionStorage.removeItem(DETAIL_BLOCK_STORAGE_KEY);
  }, [detailBlockId]);
  /** The item under drag — drives the DragOverlay ghost. */
  const [activeDrag, setActiveDrag] = useState<ChainItem | null>(null);

  /**
   * Optimistic mirror of both lanes. Drag gestures mutate this immediately
   * (live cross-lane reflow via onDragOver, final order on drop) so nothing
   * snaps back while the native mutation + resync roundtrip completes; it
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

  // Resync the optimistic lanes only when native actually reports new state
  // (and no drag is in flight). `lanes` must NOT be a dependency here — the
  // old version included it and unconditionally set a fresh object, which
  // re-triggered itself in a silent render loop.
  useEffect(() => {
    if (!draggingRef.current) setLanes({ left: chain, right: chainRight ?? [] });
  }, [chain, chainRight]);

  const sensors = useSensors(
    // A few px of travel before a drag engages: plain clicks (open detail,
    // tile buttons) stay clicks, and there's no transform jitter on press.
    useSensor(PointerSensor, { activationConstraint: { distance: 6 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates })
  );

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
    // Clear the guard once dnd-kit has re-measured the shifted layout.
    requestAnimationFrame(() => {
      justCrossedRef.current = false;
    });
    setLanes((prev) => {
      const fromItems = prev[from].filter((i) => i.blockId !== activeId);
      const toItems = [...prev[to]];
      const overIndex = toItems.findIndex((i) => i.blockId === overId);
      const insertIndex = overIndex === -1 ? toItems.length : overIndex + (landAfter ? 1 : 0);
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

    // Commit to native: a lane change is one moveBlock (exact final index);
    // a same-lane shuffle is one reorder. The chainChanged resync converges
    // the optimistic state.
    const origin = originLaneOf(activeId);
    if (origin && origin !== side) {
      actions.moveBlock(
        activeId,
        side,
        laneItems.findIndex((i) => i.blockId === activeId)
      );
      return;
    }
    const nativeIds = (side === 'left' ? chain : (chainRight ?? [])).map((i) => i.blockId);
    const localIds = laneItems.map((i) => i.blockId);
    if (nativeIds.join() !== localIds.join()) actions.reorderBlocks(localIds);
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
      ? ([...chain, ...(chainRight ?? [])].find(
          (item): item is ToneBlock => !isInsertSlot(item) && item.blockId === detailBlockId
        ) ?? null)
      : null;

  if (detailBlock) {
    return (
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          height: '100%',
          justifyContent: 'flex-start',
          boxSizing: 'border-box',
          // try to get it to match the back btn on the tone browser 24px + 4px
          paddingTop: '28px',
        }}
      >
        <ChainBlock
          block={detailBlock}
          sampleRate={sampleRate}
          onBack={() => setDetailBlockId(null)}
        />
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
      onAdd={(insertBlockId) => actions.addModel(side, insertBlockId)}
    />
  );

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'stretch',
        height: '100%',
        boxSizing: 'border-box',
        padding: '0 24px',
      }}
    >
      {stereo && <StereoPanRail />}
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
    </div>
  );
};
