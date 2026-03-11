declare module 'react-knob-headless' {
  import * as React from 'react';

  export interface KnobHeadlessProps extends Omit<
    React.ComponentProps<'div'>,
    | 'role'
    | 'aria-valuemin'
    | 'aria-valuemax'
    | 'aria-valuenow'
    | 'aria-valuetext'
    | 'aria-orientation'
    | 'aria-label'
    | 'aria-labelledby'
    | 'tabIndex'
  > {
    'aria-label'?: string;
    'aria-labelledby'?: string;
    valueRaw: number;
    valueMin: number;
    valueMax: number;
    dragSensitivity?: number;
    valueRawRoundFn?: (valueRaw: number) => number;
    valueRawDisplayFn?: (valueRaw: number) => string;
    onValueRawChange: (newValueRaw: number) => void;
    orientation?: 'horizontal' | 'vertical';
    axis?: 'y' | 'x' | 'xy';
    includeIntoTabOrder?: boolean;
    mapTo01?: (x: number, min: number, max: number) => number;
    mapFrom01?: (x: number, min: number, max: number) => number;
  }

  export const KnobHeadless: React.ForwardRefExoticComponent<
    KnobHeadlessProps & React.RefAttributes<HTMLDivElement>
  >;

  export interface KnobHeadlessLabelProps extends Omit<React.ComponentProps<'label'>, 'id'> {
    id: string;
  }
  export const KnobHeadlessLabel: React.ForwardRefExoticComponent<
    KnobHeadlessLabelProps & React.RefAttributes<HTMLLabelElement>
  >;

  export interface KnobHeadlessOutputProps extends Omit<React.ComponentProps<'output'>, 'htmlFor'> {
    htmlFor: string;
  }
  export const KnobHeadlessOutput: React.ForwardRefExoticComponent<
    KnobHeadlessOutputProps & React.RefAttributes<HTMLOutputElement>
  >;

  export function useKnobKeyboardControls(params: {
    valueRaw: number;
    valueMin: number;
    valueMax: number;
    step: number;
    stepLarger: number;
    onValueRawChange: (newValueRaw: number, event: React.KeyboardEvent) => void;
    noDefaultPrevention?: boolean;
  }): { onKeyDown: React.KeyboardEventHandler };
}
