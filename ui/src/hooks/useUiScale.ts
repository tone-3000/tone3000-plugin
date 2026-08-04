import { useLayoutEffect, useRef } from 'react';

/** Design-space width of the plugin UI. Native sizes the window to this
 * times the user's scale factor (see TONE3000Editor in plugin/include). */
export const DESIGN_WIDTH = 1024;
/** Design-space height of the plugin UI (content only). Figma's 600px
 * artboard includes a 22px mock OS title bar outside JUCE setSize. */
export const DESIGN_HEIGHT = 578;

/**
 * Proportional UI scaling: applies a CSS `zoom` of viewportWidth / 1024 to
 * the ref'd element, so the whole design-space layout (knobs, fonts,
 * spacing) grows with the window. `zoom` re-lays-out and re-rasterizes, so
 * text and SVG stay crisp at fractional scales, unlike `transform: scale`.
 *
 * The zoom follows the *actual* viewport, never a requested size: a host
 * that refuses a resize leaves the page width (and therefore the scale)
 * untouched. Grow-only, clamped at 1x, matching the native size floor.
 *
 * Deliberately imperative (no React state): a live window drag retunes the
 * zoom every frame without re-rendering the tree. ResizeObserver on <html>
 * rather than window `resize`, which fires inconsistently across WebView2 /
 * WKWebView / WebKitGTK. The observed element is outside the zoomed subtree,
 * so its size is the real viewport and no feedback loop is possible.
 *
 * External pages (the TONE3000 select flow) replace this document entirely
 * and render unzoomed, using the larger viewport responsively.
 */
export function useUiScale<T extends HTMLElement>(): React.RefObject<T | null> {
  const ref = useRef<T>(null);

  useLayoutEffect(() => {
    const el = ref.current;
    if (el === null) return;

    const apply = () => {
      const scale = Math.max(1, document.documentElement.clientWidth / DESIGN_WIDTH);
      el.style.setProperty('zoom', String(scale));
    };

    apply();
    const observer = new ResizeObserver(apply);
    observer.observe(document.documentElement);
    return () => observer.disconnect();
  }, []);

  return ref;
}
