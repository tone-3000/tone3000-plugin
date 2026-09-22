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

export interface Make {
  id: number;
  name: string;
}

export interface Tone {
  id: number;
  user_id: string;
  title: string;
  description: string | null;
  created_at: string;
  updated_at: string;
  /** When the tone was published; absent on some older payloads. */
  published_at?: string;
  gear: string;
  images: string[];
  is_public: boolean;
  links: string[];
  format: string;
  models_count: number;
  /** Per-architecture breakdowns (always returned; NAM only meaningfully). */
  a1_models_count?: number;
  a2_models_count?: number;
  custom_models_count?: number;
  favorites_count: number;
  /** Present on authenticated GET /tones responses (and after expand sync). */
  is_favorite?: boolean;
  downloads_count: number;
  license: string;
  sizes: string[];
  user: User;
  models: Model[];
  /** Gear makes/models; the detail card's info panel lists their names. */
  makes: Make[];
  tags: Tag[];
  /** Canonical public page URL (title slug + id); the share action copies it. */
  url: string;
}

/** Models this plugin actually loads: A2 for NAM, otherwise `models_count`
    (IR and other formats). NAM's `models_count` is architecture-filtered and
    excludes A2 by default, so the folder stat / picker must use `a2_models_count`. */
export function catalogModelCount(tone: {
  format?: string;
  models_count?: number;
  a2_models_count?: number;
}): number {
  return tone.format?.toLowerCase() === 'nam'
    ? (tone.a2_models_count ?? 0)
    : (tone.models_count ?? 0);
}

/** Where `activeModelId` sits in a tone's model list, with the models either
    side of it: what the gallery tile's arrows step through. Null when there
    is nothing to step through: fewer than two models, or an active model the
    list doesn't contain (stepping from index -1 would offer the first model
    as "next"). */
export function modelStep<T extends { id: number }>(
  models: readonly T[],
  activeModelId: number
): { index: number; count: number; prev: T | undefined; next: T | undefined } | null {
  const index = models.findIndex((m) => m.id === activeModelId);
  if (models.length < 2 || index < 0) return null;
  return { index, count: models.length, prev: models[index - 1], next: models[index + 1] };
}

export interface T3kDownloadEvent {
  type: 't3k.download.tone';
  tone: Tone;
}
