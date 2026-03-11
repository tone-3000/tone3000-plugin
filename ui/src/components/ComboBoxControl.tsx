import React from 'react';

interface ComboBoxControlProps {
  selectedLabel: string;
  onOpenOverlay: () => void;
}

export const ComboBoxControl: React.FC<ComboBoxControlProps> = ({
  selectedLabel,
  onOpenOverlay,
}) => {
  return (
    <div
      onClick={onOpenOverlay}
      tabIndex={0}
      style={{
        cursor: 'pointer',
        userSelect: 'none',
        outline: 'none',
        height: '100%',
        display: 'flex',
        alignItems: 'center',
      }}
    >
      <div
        style={{
          padding: '15px',
          backgroundColor: '#000000',
          border: '1px solid #ffffff',
          fontSize: '12px',
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          overflow: 'hidden',
          whiteSpace: 'nowrap',
          textOverflow: 'ellipsis',
          width: '100%',
          height: '100%',
          fontFamily: 'Courier New, monospace',
          fontWeight: 'bold',
          color: '#ffffff',
          letterSpacing: '0.5px',
        }}
      >
        <span
          style={{
            overflow: 'hidden',
            whiteSpace: 'nowrap',
            textOverflow: 'ellipsis',
            flex: 1,
          }}
        >
          {selectedLabel}
        </span>
        <span
          style={{
            fontSize: '16px',
            paddingLeft: '10px',
            color: '#ffffff',
          }}
        >
          ▼
        </span>
      </div>
    </div>
  );
};
