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
import { ChainBlock } from './ChainBlock';
import type { ChainBlockData, ChainItem } from '../types/tone';
import { isChainInsertBlock } from '../types/tone';
import { SortableSelectButton, SELECT_BUTTON_ID } from './SortableSelectButton';
import { PlusCircle } from 'lucide-react';
import { SelectButton } from './SelectButton';

interface ChainViewProps {
  chain: ChainItem[];
  onAddModel: () => void;
  onRemoveBlock: (id: string) => void;
  onReorderItems: (orderedIds: string[]) => void;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
}

export const ChainView: React.FC<ChainViewProps> = ({
  chain,
  onAddModel,
  onRemoveBlock,
  onReorderItems,
  onSwitchModel,
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
              height: 254 + 2,
              position: 'relative',
            }}
          >
            {i > 0 && (
              <div
                style={{
                  height: 252,
                  backgroundColor: '#ffffff',
                  width: '1px',
                  position: 'absolute',
                  top: -142,
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
              if (isChainInsertBlock(item)) {
                const isFirst = sortableItems.indexOf(SELECT_BUTTON_ID) === 0;
                const isLast = sortableItems.indexOf(SELECT_BUTTON_ID) === sortableItems.length - 1;
                return (
                  <SortableSelectButton
                    key={SELECT_BUTTON_ID}
                    onClick={onAddModel}
                    routing={isFirst ? -1 : isLast ? 1 : 2}
                  />
                );
              }
              return (
                <ChainBlock
                  key={item.blockId}
                  block={item as ChainBlockData}
                  onRemove={onRemoveBlock}
                  onSwitchModel={onSwitchModel}
                />
              );
            })}
          </SortableContext>
        </div>
      </DndContext>
    </div>
  );
};
