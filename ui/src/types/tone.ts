export interface Model {
  id: number;
  name: string;
  model_url: string;
  created_at: string;
  updated_at?: string;
  size: string;
  user_id: string;
  tone_id?: number;
}

export interface PaginatedResponse<T> {
  data: T[];
  page: number;
  page_size: number;
  total: number;
  total_pages: number;
}

export interface User {
  id: string;
  avatar_url: string;
  username: string;
}

export interface Tag {
  id: number;
  name: string;
}

export interface Tone {
  id: number;
  user_id: string;
  title: string;
  description: string;
  created_at: string;
  updated_at: string;
  gear: string;
  images: string[];
  is_public: boolean;
  links: string[];
  platform: string;
  models_count: number;
  favorites_count: number;
  downloads_count: number;
  license: string;
  sizes: string[];
  user: User;
  models: Model[];
  makes: any[];
  tags: Tag[];
}

/**
 * Chain block data extends Tone with state fields from backend
 */
export interface ChainBlockData extends Tone {
  blockId: string;
  loaded: boolean;
  enabled: boolean;
  outputGain: number;
  mix: number;
  activeModelId: number;
  /** NAM container / slimmable model: show Lite vs Full when true and loaded */
  namSlimmable?: boolean;
  /** 1 = full, 0.5 = lite */
  namSlimmableSize?: number;
}

/**
 * Insert block placeholder from backend (pass-through, no audio effect).
 * Position in chain determines where new tones are added.
 */
export interface ChainInsertBlock {
  blockId: string;
  isInsertBlock: true;
}

/** Chain item: either a real block or the insert placeholder */
export type ChainItem = ChainBlockData | ChainInsertBlock;

export function isChainInsertBlock(item: ChainItem): item is ChainInsertBlock {
  return 'isInsertBlock' in item && item.isInsertBlock === true;
}

/** Which chain is being edited in stereo mode. */
export type ChainSide = 'left' | 'right';

/** Chain status returned by the native `getChainStatus` function. */
export interface ChainStatus {
  chain: ChainItem[];
  /** True when stereo (dual Left/Right chain) mode is active. */
  stereoEnabled: boolean;
  /** The chain currently being edited; only meaningful when stereoEnabled. */
  activeSide: ChainSide;
}

export interface T3kDownloadEvent {
  type: 't3k.download.tone';
  tone: Tone;
}
