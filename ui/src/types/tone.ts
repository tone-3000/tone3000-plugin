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
  format: string;
  models_count: number;
  /** Per-architecture breakdowns (always returned; NAM only meaningfully). */
  a1_models_count?: number;
  a2_models_count?: number;
  custom_models_count?: number;
  favorites_count: number;
  downloads_count: number;
  license: string;
  sizes: string[];
  user: User;
  models: Model[];
  makes: any[];
  tags: Tag[];
}

export interface T3kDownloadEvent {
  type: 't3k.download.tone';
  tone: Tone;
}
