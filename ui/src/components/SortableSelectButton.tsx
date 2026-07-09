import React from 'react';
import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { GripVertical } from 'lucide-react';
import { SelectButton } from './SelectButton';

export const SELECT_BUTTON_ID = 'select-insert';

interface SortableSelectButtonProps {
  onClick: () => void;
  routing?: 2 | 1 | -1;
  /** Insert slot block id ('select-insert' or 'select-insert-right'). */
  id?: string;
}

export const SortableSelectButton: React.FC<SortableSelectButtonProps> = ({
  onClick,
  routing,
  id = SELECT_BUTTON_ID,
}) => {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id,
  });

  const style = {
    transform: CSS.Transform.toString(transform),
    transition,
    opacity: isDragging ? 0.75 : 1,
    position: 'relative' as const,
  };

  return (
    <div
      ref={setNodeRef}
      style={{
        ...style,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
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
          zIndex: 1,
        }}
        title="Drag to reorder"
      >
        <GripVertical size={16} />
      </div>
      <SelectButton onClick={onClick} routing={isDragging ? undefined : routing} />
    </div>
  );
};
