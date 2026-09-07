/**
 * The tone browser's tabs, in one place so the flows that open the browser
 * on a particular tab (see useToneLoadFlow) don't have to import the
 * browser itself.
 *
 * `library` is local: the user's own folder tree of tones on disk. The
 * other four are TONE3000 streams (`trending` is public, the rest need a
 * session).
 */
export type BrowserTab = 'library' | 'trending' | 'downloaded' | 'favorited' | 'created';

/** Remembers the last-viewed tab so the next browse lands on it. */
export const BROWSER_TAB_STORAGE_KEY = 't3k_browser_stream';
