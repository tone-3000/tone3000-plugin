import React from 'react';
import { PlusCircle } from 'lucide-react';

interface AddModelButtonProps {
  onClick: () => void;
  routing?: 1 | -1;
}

export const SelectButton: React.FC<AddModelButtonProps> = ({ onClick, routing = 1 }) => {
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
        height: '254px',
        width: '640px',
        position: 'relative',
      }}
    >
      {routing === 1 && (
        <div
          style={{
            height: 110,
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
      {routing === -1 && (
        <div
          style={{
            height: 110,
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
