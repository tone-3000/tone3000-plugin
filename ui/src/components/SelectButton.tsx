import React from 'react';
import { PlusCircle } from 'lucide-react';
import { CARD_HEIGHT, CARD_WIDTH } from './chainLayout';

// Routing line runs from the button edge to the plus-circle's edge.
const ROUTING_LINE_HEIGHT = CARD_HEIGHT / 2 - 20;

interface AddModelButtonProps {
  onClick: () => void;
  routing?: 2 | 1 | -1;
}

export const SelectButton: React.FC<AddModelButtonProps> = ({ onClick, routing }) => {
  return (
    <button
      onClick={onClick}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        gap: '8px',
        borderRadius: '20px',
        border: 'none',
        background: '#1C1C1E',
        // https://kovart.github.io/dashed-border-generator/
        backgroundImage: `url("data:image/svg+xml,%3csvg width='100%25' height='100%25' xmlns='http://www.w3.org/2000/svg'%3e%3crect width='100%25' height='100%25' fill='none' rx='20' ry='20' stroke='%238D8D93FF' stroke-width='2' stroke-dasharray='6%2c 10' stroke-dashoffset='9' stroke-linecap='square'/%3e%3c/svg%3e")`,
        padding: '8px 12px',
        margin: '0',
        cursor: 'pointer',
        color: '#ffffff',
        fontSize: '12px',
        fontWeight: 'bold',
        transition: 'all 0.2s ease',
        height: `${CARD_HEIGHT}px`,
        width: `${CARD_WIDTH}px`,
        position: 'relative',
      }}
    >
      {(routing === 1 || routing === 2) && (
        <div
          style={{
            height: ROUTING_LINE_HEIGHT,
            backgroundColor: '#ffffff',
            width: '1px',
            position: 'absolute',
            top: 0,
          }}
        />
      )}
      <span
        style={{
          lineHeight: '1',
          display: 'flex',
          alignItems: 'center',
        }}
      >
        <PlusCircle size={40} strokeWidth={1} />
      </span>
      {(routing === -1 || routing === 2) && (
        <div
          style={{
            height: ROUTING_LINE_HEIGHT,
            backgroundColor: '#ffffff',
            width: '1px',
            position: 'absolute',
            bottom: 0,
          }}
        />
      )}
    </button>
  );
};
