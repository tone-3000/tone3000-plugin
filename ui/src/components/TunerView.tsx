import React, { useEffect, useRef, useState } from 'react';
import { useFunction } from '../hooks/useFunction';

interface TunerReading {
  frequency: number;
  confidence: number;
  level: number;
}

const NOTE_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];

// Cents window considered "in tune" and the full deflection of one side.
const IN_TUNE_CENTS = 5;
const MAX_CENTS = 50;

// Bar colors from the center outward (blue → yellow → red), per screenshot.
const SIDE_COLORS = ['#0000FF', '#FFFF00', '#FFFF00', '#FF0000', '#FF0000', '#FF0000'];
const DIM_OPACITY = 0.14;
const POLL_MS = 50;

const frequencyToNote = (frequency: number) => {
  const midi = 69 + 12 * Math.log2(frequency / 440);
  const nearest = Math.round(midi);
  return {
    name: NOTE_NAMES[((nearest % 12) + 12) % 12],
    octave: Math.floor(nearest / 12) - 1,
    cents: (midi - nearest) * 100,
  };
};

// Number of bars lit on the deflection side: blue only near in-tune, all six
// red at a half-semitone off.
const litCountForCents = (absCents: number): number => {
  if (absCents <= IN_TUNE_CENTS) return 1;
  const t = Math.min(1, (absCents - IN_TUNE_CENTS) / (MAX_CENTS - IN_TUNE_CENTS));
  return Math.min(6, 1 + Math.floor(t * 5 + 0.5));
};

export const TunerView: React.FC = () => {
  const getTunerReading = useFunction<TunerReading>('getTunerReading');
  const [note, setNote] = useState<string | null>(null);
  const [cents, setCents] = useState(0);
  const [hasSignal, setHasSignal] = useState(false);
  const [frequency, setFrequency] = useState(0);
  const smoothedCentsRef = useRef(0);
  const holdTimeoutRef = useRef<number | undefined>(undefined);

  useEffect(() => {
    let cancelled = false;
    let polling = false;

    const poll = async () => {
      if (cancelled || polling) return;
      polling = true;
      try {
        const reading = await getTunerReading.invoke();
        if (cancelled || !reading) return;

        const freq = typeof reading.frequency === 'number' ? reading.frequency : 0;
        const confidence = typeof reading.confidence === 'number' ? reading.confidence : 0;

        if (freq > 0 && confidence > 0.5) {
          const detected = frequencyToNote(freq);
          // Light exponential smoothing so the display doesn't jitter.
          smoothedCentsRef.current = smoothedCentsRef.current * 0.6 + detected.cents * 0.4;
          setNote(detected.name);
          setCents(smoothedCentsRef.current);
          setFrequency(freq);
          setHasSignal(true);
          if (holdTimeoutRef.current) window.clearTimeout(holdTimeoutRef.current);
          // Hold the last note on screen briefly after the signal decays.
          holdTimeoutRef.current = window.setTimeout(() => setHasSignal(false), 900);
        }
      } catch {
        // Ignore individual polling failures.
      } finally {
        polling = false;
      }
    };

    const interval = window.setInterval(poll, POLL_MS);
    poll();

    return () => {
      cancelled = true;
      window.clearInterval(interval);
      if (holdTimeoutRef.current) window.clearTimeout(holdTimeoutRef.current);
    };
  }, []);

  const absCents = Math.abs(cents);
  const inTune = hasSignal && absCents <= IN_TUNE_CENTS;
  const isFlat = hasSignal && cents < -IN_TUNE_CENTS;
  const isSharp = hasSignal && cents > IN_TUNE_CENTS;

  // Flat lights the left side, sharp the right; in tune lights both blues.
  const leftLit = !hasSignal ? 0 : inTune ? 1 : isFlat ? litCountForCents(absCents) : 0;
  const rightLit = !hasSignal ? 0 : inTune ? 1 : isSharp ? litCountForCents(absCents) : 0;

  const renderBars = (side: 'left' | 'right', litCount: number) => {
    // Bars ordered outermost → innermost for the left side, mirrored for right.
    const indices = side === 'left' ? [5, 4, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5];
    return (
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '16px',
          transform: `perspective(700px) rotateY(${side === 'left' ? 30 : -30}deg)`,
        }}
      >
        {indices.map((i) => {
          const lit = i < litCount;
          return (
            <div
              key={i}
              style={{
                width: '36px',
                height: `${170 + i * 18}px`,
                backgroundColor: SIDE_COLORS[i],
                opacity: lit ? 1 : DIM_OPACITY,
                transition: 'opacity 90ms linear',
                flexShrink: 0,
              }}
            />
          );
        })}
      </div>
    );
  };

  const triangle = (direction: 'up' | 'down', lit: boolean) => (
    <div
      style={{
        width: 0,
        height: 0,
        borderLeft: '26px solid transparent',
        borderRight: '26px solid transparent',
        [direction === 'up' ? 'borderBottom' : 'borderTop']: '32px solid #0000FF',
        opacity: lit ? 1 : DIM_OPACITY,
        transition: 'opacity 90ms linear',
      }}
    />
  );

  return (
    <div
      style={{
        position: 'relative',
        flex: 1,
        width: '100%',
        minHeight: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        backgroundColor: '#000000',
        overflow: 'hidden',
      }}
    >
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          justifyContent: 'center',
          gap: '48px',
        }}
      >
        {renderBars('left', leftLit)}

        {/* Center: triangles + note letter */}
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            gap: '18px',
            minWidth: '160px',
          }}
        >
          {/* Top triangle points down: lit when sharp ("tune down") or in tune */}
          {triangle('down', inTune || isSharp)}
          <div
            style={{
              fontSize: '110px',
              lineHeight: 1,
              fontWeight: 700,
              color: '#ffffff',
              opacity: hasSignal ? 1 : 0.25,
              transition: 'opacity 150ms linear',
              textAlign: 'center',
              userSelect: 'none',
              fontVariantNumeric: 'tabular-nums',
            }}
          >
            {note ?? '—'}
          </div>
          <div
            style={{
              height: '14px',
              fontSize: '13px',
              fontFamily: 'monospace',
              color: '#8D8D93',
              opacity: hasSignal ? 1 : 0,
              transition: 'opacity 150ms linear',
            }}
          >
            {`${cents > 0 ? '+' : ''}${Math.round(cents)}¢ · ${frequency.toFixed(1)} Hz`}
          </div>
          {/* Bottom triangle points up: lit when flat ("tune up") or in tune */}
          {triangle('up', inTune || isFlat)}
        </div>

        {renderBars('right', rightLit)}
      </div>
    </div>
  );
};
