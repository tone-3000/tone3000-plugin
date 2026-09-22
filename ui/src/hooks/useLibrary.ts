import { useCallback, useEffect, useRef, useState } from 'react';
import { useNativeFunction } from './useFunction';
import {
  MAX_FOLDER_MODELS,
  MAX_LOCAL_FILE_BYTES,
  isModelFile,
  readDirectoryTree,
  readFileBase64,
} from './localFiles';
import type { LibraryListing } from '../types/library';

/**
 * The tone library's UI side: one folder of the on-disk library at a time,
 * plus the actions that change it. Native owns the files (see
 * plugin/docs/library.md); this hook is a thin cursor over them that
 * re-lists after every mutation, the way the preset browser re-reads its
 * folder.
 *
 * Paths are root-relative and `/`-separated, with '' for the library root.
 * Native validates every one of them, so a stale path from a folder deleted
 * in Finder comes back as an error rather than a wrong listing; the hook
 * falls back to the root when that happens.
 */

/** The folder the browser was last in. Remembered across sessions so a
    player who keeps everything under "Live rig" lands there, and so
    "Save to Library" files tones where they were last browsing. */
const FOLDER_STORAGE_KEY = 't3k.libraryFolder';

/** The folder tile menus file tones into: wherever the browser was last
    left, the library root until it has been used. */
export const readLibraryFolder = (): string => {
  try {
    return localStorage.getItem(FOLDER_STORAGE_KEY) ?? '';
  } catch {
    return '';
  }
};

const rememberLibraryFolder = (path: string) => {
  try {
    localStorage.setItem(FOLDER_STORAGE_KEY, path);
  } catch {
    // Private-mode storage failures are not worth failing navigation over.
  }
};

/** One thing the user dropped, snapshotted synchronously from the event (a
    DataTransferItem goes inert as soon as the handler yields). */
export interface DroppedItem {
  entry: FileSystemEntry | null;
  file: File | null;
}

/** Files per bridge call while filing a drop. The bytes travel as base64
    strings, so a whole folder in one call would peak at several times its
    size in memory; this bounds that without a call per file. */
const IMPORT_BATCH = 20;

/** Native's { error } | payload result, narrowed. */
const errorOf = (result: { error?: string } | null, fallback: string): string | null =>
  result === null ? fallback : (result.error ?? null);

export function useLibrary() {
  const listLibrary = useNativeFunction<LibraryListing>('listLibrary');
  const createFolderNative = useNativeFunction<{ path?: string; error?: string }>(
    'createLibraryFolder'
  );
  const renameNative = useNativeFunction<{ path?: string; error?: string }>('renameLibraryItem');
  const moveNative = useNativeFunction<{ path?: string; error?: string }>('moveLibraryItem');
  const removeNative = useNativeFunction<boolean>('removeLibraryItem');
  const importFilesNative = useNativeFunction<{ copied?: number; error?: string }>(
    'importFilesToLibrary'
  );
  const importPickNative = useNativeFunction<{
    path?: string;
    error?: string;
    cancelled?: boolean;
  }>('importToLibrary');
  const revealNative = useNativeFunction<string>('revealLibraryFolder');

  const [path, setPath] = useState(readLibraryFolder);
  const [listing, setListing] = useState<LibraryListing | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  // Guards against a slow listing landing after a newer one (fast clicks
  // through folders).
  const requestRef = useRef(0);

  const list = useCallback(
    async (target: string) => {
      const request = ++requestRef.current;
      setLoading(true);
      const result = await listLibrary(target);
      if (request !== requestRef.current) return;

      if (!result || result.error) {
        // The folder is gone (deleted in Finder since we were last here):
        // fall back to the root rather than stranding the browser.
        if (target !== '') {
          setPath('');
          rememberLibraryFolder('');
          void list('');
          return;
        }
        setListing(null);
        setError(result?.error ?? "Couldn't read the library folder");
        setLoading(false);
        return;
      }
      setListing(result);
      setError(null);
      setLoading(false);
    },
    [listLibrary]
  );

  useEffect(() => {
    void list(path);
  }, [list, path]);

  /** Navigate to a folder (the breadcrumb, a folder row, `''` for the root). */
  const open = useCallback((target: string) => {
    rememberLibraryFolder(target);
    setPath(target);
  }, []);

  const refresh = useCallback(() => list(path), [list, path]);

  const createFolder = useCallback(
    async (name: string): Promise<string | null> => {
      const result = await createFolderNative(path, name);
      await refresh();
      return errorOf(result, "Couldn't create the folder");
    },
    [createFolderNative, path, refresh]
  );

  const rename = useCallback(
    async (itemPath: string, newName: string): Promise<string | null> => {
      const result = await renameNative(itemPath, newName);
      await refresh();
      return errorOf(result, "Couldn't rename that");
    },
    [renameNative, refresh]
  );

  /** File a tone or folder into another library folder ('' is the root).
      Native uniques a taken name rather than overwriting. */
  const move = useCallback(
    async (itemPath: string, destFolder: string): Promise<string | null> => {
      const result = await moveNative(itemPath, destFolder);
      await refresh();
      return errorOf(result, "Couldn't move that");
    },
    [moveNative, refresh]
  );

  const remove = useCallback(
    async (itemPath: string): Promise<string | null> => {
      const removed = await removeNative(itemPath);
      await refresh();
      return removed ? null : "Couldn't remove that";
    },
    [removeNative, refresh]
  );

  /** The Import action: native opens the OS picker and copies the pick into
      the folder on show. Also the route that works on Linux, where OS file
      drags never reach the webview. */
  const importPick = useCallback(
    async (kind: 'file' | 'folder'): Promise<string | null> => {
      const result = await importPickNative(path, kind === 'folder');
      await refresh();
      if (result?.cancelled || result?.path) return null;
      return result?.error ?? "Couldn't add that to the library";
    },
    [importPickNative, path, refresh]
  );

  /**
   * Everything dropped on the browser, filed into the folder on show.
   *
   * A dropped folder keeps its name *and its shape*: its files ride the
   * bridge under relative paths and native recreates the subfolders, so a
   * drop and an Add Folder leave the same thing on disk. A drop can carry
   * several files or folders, and they are read and shipped in batches —
   * the bytes travel as base64, and a whole capture pack encoded at once
   * would take the webview down with it.
   */
  const importDrop = useCallback(
    async (items: DroppedItem[]): Promise<string | null> => {
      try {
        // Collect the whole drop first, so the caps below judge it as one.
        const files: { name: string; file: File }[] = [];
        for (const item of items) {
          if (item.entry?.isDirectory) {
            const folderName = item.entry.name;
            const tree = await readDirectoryTree(item.entry as FileSystemDirectoryEntry);
            for (const { file, path: relative } of tree)
              if (isModelFile(file.name)) files.push({ name: `${folderName}/${relative}`, file });
          } else if (item.file && isModelFile(item.file.name)) {
            files.push({ name: item.file.name, file: item.file });
          }
        }

        if (files.length === 0) return 'Only .nam and .wav files are supported';
        if (files.some(({ file }) => file.size > MAX_LOCAL_FILE_BYTES))
          return 'A file is too large';
        // Add Folder has no such cap: it copies on the native side, where
        // nothing has to be encoded or held in memory.
        if (files.length > MAX_FOLDER_MODELS)
          return `Too many files (max ${MAX_FOLDER_MODELS}) \u2014 use Add Folder instead`;

        // Each dropped folder is created once, up front and uniqued, so a
        // second drop of the same pack sits beside the first instead of
        // merging into it. (Native uniques file names; the folder is ours.)
        const roots = new Map<string, string>();
        for (const { name } of files) {
          const top = name.includes('/') ? name.slice(0, name.indexOf('/')) : null;
          if (top === null || roots.has(top)) continue;
          const created = await createFolderNative(path, top, true);
          if (!created?.path) return created?.error ?? "Couldn't create the folder";
          roots.set(top, created.path.slice(created.path.lastIndexOf('/') + 1));
        }

        let copied = 0;
        let firstError: string | null = null;
        const created = [...roots.values()].map((name) => (path ? `${path}/${name}` : name));
        for (let i = 0; i < files.length; i += IMPORT_BATCH) {
          const payload = await Promise.all(
            files.slice(i, i + IMPORT_BATCH).map(async ({ name, file }) => {
              // Retarget onto the (possibly uniqued) folder we just made.
              const top = name.includes('/') ? name.slice(0, name.indexOf('/')) : null;
              const renamed = top ? `${roots.get(top)}${name.slice(top.length)}` : name;
              return { name: renamed, data: await readFileBase64(file) };
            })
          );
          const result = await importFilesNative(path, payload);
          copied += result?.copied ?? 0;
          if (firstError === null && result?.error) firstError = result.error;
        }
        // A drop where nothing survived validation must not leave the
        // folders it was going to fill sitting there empty.
        if (copied === 0) for (const folder of created) await removeNative(folder);
        await refresh();
        // Some files landing is a success; the message is for a drop where
        // nothing did (which for a single file is exactly that file's).
        return copied > 0 ? null : (firstError ?? "Couldn't add those files");
      } catch (err) {
        console.error('Library drop failed:', err);
        return "Couldn't read the dropped files";
      }
    },
    [createFolderNative, importFilesNative, path, refresh, removeNative]
  );

  /** Open the current folder in Finder/Explorer. */
  const reveal = useCallback(async () => {
    await revealNative(path);
  }, [revealNative, path]);

  return {
    path,
    listing,
    loading,
    error,
    open,
    refresh,
    createFolder,
    rename,
    move,
    remove,
    importPick,
    importDrop,
    reveal,
  };
}
