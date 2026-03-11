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
import type { ChainBlockData } from '../types/tone';
import { SelectButton } from './SelectButton';
import { PlusCircle } from 'lucide-react';

interface ChainViewProps {
  blocks: ChainBlockData[];
  onAddModel: () => void;
  onRemoveBlock: (id: string) => void;
  onReorderBlocks: (activeId: string, overId: string) => void;
  onSwitchModel?: (blockId: string, modelId: number) => Promise<void>;
}

export const ChainView: React.FC<ChainViewProps> = ({
  blocks,
  onAddModel,
  onRemoveBlock,
  onReorderBlocks,
  onSwitchModel,
}) => {
  const sensors = useSensors(
    useSensor(PointerSensor),
    useSensor(KeyboardSensor, {
      coordinateGetter: sortableKeyboardCoordinates,
    })
  );

  const handleDragEnd = (event: DragEndEvent) => {
    const { active, over } = event;

    if (over && active.id !== over.id) {
      onReorderBlocks(active.id as string, over.id as string);
    }
  };

  const backgroundArray = Array.from(
    { length: blocks.length === 0 ? 2 : blocks.length + 1 },
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
            items={blocks.map((block) => block.blockId)}
            strategy={verticalListSortingStrategy}
          >
            {blocks.map((block) => (
              <ChainBlock
                key={block.blockId}
                block={block}
                onRemove={onRemoveBlock}
                onSwitchModel={onSwitchModel}
              />
            ))}
          </SortableContext>

          {blocks.length === 0 && <SelectButton onClick={onAddModel} routing={-1} />}
          <SelectButton onClick={onAddModel} routing={1} />
        </div>
      </DndContext>
    </div>
  );
};
