/** One subfolder of a library folder. `models` counts the loadable files
    under it (subfolders included), i.e. how many models loading it adds. */
export interface LibraryFolder {
  name: string;
  path: string;
  models: number;
}

/** One tone file in the library. `name` has no extension; `kind` is what
    the plugin branches on (a NAM capture vs an impulse response). */
export interface LibraryModel {
  name: string;
  path: string;
  kind: 'nam' | 'ir';
  size: number;
  /** Last-modified time, epoch ms. */
  modified: number;
}

/** One folder's contents, as native lists it. `parent` is absent at the
    library root; `path` is '' there. */
export interface LibraryListing {
  path: string;
  name: string;
  parent?: string;
  /** What loading this folder as one block would add: every loadable file
      under it of the majority extension (NAM vs IR). Not the length of
      `models`, which is only this folder's direct children. */
  loadable: number;
  folders: LibraryFolder[];
  models: LibraryModel[];
  error?: string;
}
