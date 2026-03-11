import React from 'react';

interface ToggleControlProps {
  label: string;
  value: boolean;
  onChange: (value: boolean) => void;
  accentColor?: string;
}

export const ToggleControl: React.FC<ToggleControlProps> = ({
  label,
  value,
  onChange,
  accentColor = '#ffffff',
}) => {
  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    onChange(e.target.checked);
  };

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: '10px',
        padding: '10px',
      }}
    >
      <div
        style={{
          width: '24px',
          height: '24px',
          backgroundColor: value ? accentColor : '#000000',
          border: `2px solid #ffffff`,
          cursor: 'pointer',
          transition: 'all 0.1s ease',
        }}
        onClick={() => onChange(!value)}
      />
      <input
        type="checkbox"
        checked={value}
        onChange={handleChange}
        style={{
          width: '20px',
          height: '20px',
          cursor: 'pointer',
          accentColor: accentColor,
          display: 'none',
        }}
      />
      <span
        style={{
          fontSize: '10px',
          fontWeight: 'bold',
          textAlign: 'center',
          width: '60px',
          color: '#ffffff',
          fontFamily: 'Courier New, monospace',
          letterSpacing: '1px',
        }}
      >
        {label}
      </span>
    </div>
  );
};
