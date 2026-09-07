/**
 * Reading local files out of the DOM, shared by the two flows that take
 * them: loading a drop into the chain (useToneLoadFlow) and filing a drop
 * into the tone library (LibraryBrowser).
 *
 * The stock OS webviews never expose file paths to the DOM (no
 * Electron-style `webUtils.getPathForFile`), so anything the user drops
 * crosses the native bridge as base64 bytes instead. Native validates and
 * stores them; see plugin/docs/local-models.md.
 */

/** Sanity cap for dropped files; real .nam files and IRs are a few MB, and
    the bytes ride the native bridge as base64 strings. */
export const MAX_LOCAL_FILE_BYTES = 50 * 1024 * 1024;

/** Cap on models taken from one dropped folder (matches the catalog's
    per-tone model limit). */
export const MAX_FOLDER_MODELS = 300;

/** One `{ name, data }` entry as native's local-load and library importers
    expect it. */
export interface LocalFilePayload {
  name: string;
  data: string;
}

/** FileReader emits base64 directly (as a data URL), skipping a manual
    ArrayBuffer-to-string pass over multi-MB files. */
export const readFileBase64 = (file: File) =>
  new Promise<string>((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve((reader.result as string).split(',', 2)[1] ?? '');
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });

export const extensionOf = (name: string) => name.slice(name.lastIndexOf('.') + 1).toLowerCase();

export const stripExtension = (name: string) => {
  const dot = name.lastIndexOf('.');
  return dot > 0 ? name.slice(0, dot) : name;
};

/** The files the plugin can load: NAM captures and IR wavs. */
export const isModelFile = (name: string) => {
  const extension = extensionOf(name);
  return extension === 'nam' || extension === 'wav';
};

/** All files under a dropped directory, subfolders included. readEntries
    hands out batches (Chromium caps them at 100), so each reader drains in
    a loop. */
export const readDirectoryFiles = async (root: FileSystemDirectoryEntry): Promise<File[]> =>
  (await readDirectoryTree(root)).map((entry) => entry.file);

/** A file from a dropped folder, with where it sat inside it. */
export interface DroppedFile {
  file: File;
  /** Path relative to the dropped folder ("Marshall/JCM800.nam"). */
  path: string;
}

/** Like readDirectoryFiles, but keeping each file's place in the tree, for
    the callers that reproduce the folder rather than flattening it. */
export const readDirectoryTree = async (root: FileSystemDirectoryEntry): Promise<DroppedFile[]> => {
  const files: DroppedFile[] = [];
  const pending: { dir: FileSystemDirectoryEntry; path: string }[] = [{ dir: root, path: '' }];
  while (pending.length > 0) {
    const { dir, path } = pending.pop()!;
    const reader = dir.createReader();
    for (;;) {
      const entries = await new Promise<FileSystemEntry[]>((resolve, reject) =>
        reader.readEntries(resolve, reject)
      );
      if (entries.length === 0) break;
      for (const entry of entries) {
        const entryPath = path === '' ? entry.name : `${path}/${entry.name}`;
        if (entry.isDirectory) {
          pending.push({ dir: entry as FileSystemDirectoryEntry, path: entryPath });
        } else {
          const file = await new Promise<File>((resolve, reject) =>
            (entry as FileSystemFileEntry).file(resolve, reject)
          );
          files.push({ file, path: entryPath });
        }
      }
    }
  }
  return files;
};

/** Read files into the bridge payload, in natural name order (directory
    read order is unspecified, and "amp 2" belongs before "amp 10"). */
export const toPayload = async (files: File[]): Promise<LocalFilePayload[]> => {
  const sorted = [...files].sort((a, b) =>
    a.name.localeCompare(b.name, undefined, { numeric: true })
  );
  return Promise.all(sorted.map(async (f) => ({ name: f.name, data: await readFileBase64(f) })));
};
