import React from 'react';
import {
  DndContext,
  closestCenter,
  KeyboardSensor,
  PointerSensor,
  useSensor,
  useSensors,
} from '@dnd-kit/core';
import type { DragEndEvent } from '@dnd-kit/core';
import {
  SortableContext,
  sortableKeyboardCoordinates,
  verticalListSortingStrategy,
} from '@dnd-kit/sortable';
import { ChainBlock, CARD_HEIGHT } from './ChainBlock';
import type { BlockParamName, ChainItem, EqBand, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';
import { SortableSelectButton } from './SortableSelectButton';
import { PlusCircle } from 'lucide-react';
import { SelectButton } from './SelectButton';

interface ChainViewProps {
  chain: ChainItem[];
  onAddModel: () => void;
  onRemoveBlock: (id: string) => void;
  onSwapBlock: (id: string) => void;
  onShareBlock: (block: ToneBlock) => Promise<boolean>;
  onReorderItems: (orderedIds: string[]) => void;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
  onSetBlockParam: (blockId: string, param: BlockParamName, value: number | boolean) => void;
  onSetBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => void;
  onSetBlockEqEnabled: (blockId: string, enabled: boolean) => void;
  onResetBlockEq: (blockId: string) => void;
  sampleRate: number;
}

// Decorative connector geometry, derived from the card height so the ghost
// plus-circles/lines stay aligned with the real cards (32px column gap).
const BG_SPAN_HEIGHT = CARD_HEIGHT + 2;
const BG_CIRCLE_RADIUS = 20;
const BG_LINE_BOTTOM = BG_SPAN_HEIGHT / 2 - BG_CIRCLE_RADIUS;
const BG_LINE_TOP = -(32 + BG_SPAN_HEIGHT / 2 - BG_CIRCLE_RADIUS);
const BG_LINE_HEIGHT = BG_LINE_BOTTOM - BG_LINE_TOP;

export const ChainView: React.FC<ChainViewProps> = ({
  chain,
  onAddModel,
  onRemoveBlock,
  onSwapBlock,
  onShareBlock,
  onReorderItems,
  onSwitchModel,
  onSetBlockParam,
  onSetBlockEqBand,
  onSetBlockEqEnabled,
  onResetBlockEq,
  sampleRate,
}) => {
  const sensors = useSensors(
    useSensor(PointerSensor),
    useSensor(KeyboardSensor, {
      coordinateGetter: sortableKeyboardCoordinates,
    })
  );

  // Chain order from backend (includes insert block)
  const sortableItems = chain.map((item) => item.blockId);

  const handleDragEnd = (event: DragEndEvent) => {
    const { active, over } = event;

    if (!over || active.id === over.id) return;

    const oldIndex = sortableItems.indexOf(active.id as string);
    const newIndex = sortableItems.indexOf(over.id as string);
    if (oldIndex === -1 || newIndex === -1) return;

    const newItems = [...sortableItems];
    const [removed] = newItems.splice(oldIndex, 1);
    newItems.splice(newIndex, 0, removed);

    onReorderItems(newItems);
  };

  const totalItems = sortableItems.length;
  const backgroundArray = Array.from(
    { length: totalItems === 1 ? 2 : totalItems },
    (_, i) => i
  );
  return (
    <div
      style={{
        width: '100%',
        padding: '12px',
        backgroundColor: '#000000',
        position: 'relative',
        display: 'flex',
        justifyContent: 'center',
      }}
    >
      {/* Background blocks */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          gap: '32px',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'absolute',
          top: 0,
          left: 0,
          right: 0,
          zIndex: 1,
          pointerEvents: 'none',
        }}
      >
        {backgroundArray.map((_, i) => (
          <span
            key={i + '-bg'}
            style={{
              lineHeight: '1',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              height: BG_SPAN_HEIGHT,
              position: 'relative',
            }}
          >
            {i > 0 && (
              <div
                style={{
                  height: BG_LINE_HEIGHT,
                  backgroundColor: '#ffffff',
                  width: '1px',
                  position: 'absolute',
                  top: BG_LINE_TOP,
                }}
              />
            )}
            <PlusCircle size={40} strokeWidth={1} />
          </span>
        ))}
      </div>
      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleDragEnd}>
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '32px',
            alignItems: 'center',
            justifyContent: 'center',
            position: 'relative',
            zIndex: 2,
          }}
        >
          <SortableContext
            items={sortableItems}
            strategy={verticalListSortingStrategy}
          >
            {chain.length === 1 && (
              <>
                <SelectButton onClick={onAddModel} routing={-1} />
                <SelectButton onClick={onAddModel} routing={1} />
              </>
            )}
            {chain.length > 1 && chain.map((item) => {
              if (isInsertSlot(item)) {
                const isFirst = sortableItems.indexOf(item.blockId) === 0;
                const isLast = sortableItems.indexOf(item.blockId) === sortableItems.length - 1;
                return (
                  <SortableSelectButton
                    key={item.blockId}
                    id={item.blockId}
                    onClick={onAddModel}
                    routing={isFirst ? -1 : isLast ? 1 : 2}
                  />
                );
              }
              return (
                <ChainBlock
                  key={item.blockId}
                  block={item}
                  onRemove={onRemoveBlock}
                  onSwap={onSwapBlock}
                  onShare={onShareBlock}
                  onSwitchModel={onSwitchModel}
                  onSetParam={onSetBlockParam}
                  onSetEqBand={onSetBlockEqBand}
                  onSetEqEnabled={onSetBlockEqEnabled}
                  onResetEq={onResetBlockEq}
                  sampleRate={sampleRate}
                />
              );
            })}
          </SortableContext>
        </div>
      </DndContext>
    </div>
  );
};
