import type { PointerEvent } from 'react';
import { PointerSensor } from '@dnd-kit/core';
import type { PointerSensorOptions } from '@dnd-kit/core';

/** A few px of travel before a drag engages, so tap/click stays open. */
export const GALLERY_DRAG_DISTANCE_PX = 6;

/**
 * Gallery drag sensor: press + move on the tile (or grip) to reorder.
 * Interactive chrome (`button`, `[data-no-dnd]`) never starts a drag.
 */
export class GalleryPointerSensor extends PointerSensor {
  static activators = [
    {
      eventName: 'onPointerDown' as const,
      handler: (
        { nativeEvent: event }: PointerEvent,
        { onActivation }: PointerSensorOptions
      ) => {
        if (!event.isPrimary || event.button !== 0) return false;
        const target = event.target;
        if (
          target instanceof Element &&
          target.closest('button, a, input, textarea, select, [data-no-dnd]')
        ) {
          return false;
        }
        onActivation?.({ event });
        return true;
      },
    },
  ];
}
