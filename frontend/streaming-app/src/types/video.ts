export type Quality = "4K" | "1080p" | "720p" | "480p";

export interface Video {
  id: string;
  title: string;
  category: string;
  duration: string;
  views: string;
  thumb: string;
  quality: Quality;
  type: "vod";
}