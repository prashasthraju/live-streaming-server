import type { User } from "../types/user";
import type { Video } from "../types/video";
import type { Stream } from "../types/stream";

export const USERS: User[] = [
  { id: "u1", username: "demo", password: "demo123", email: "demo@test.com", avatar: "DM", plan: "Pro" },
];

export const VIDEOS: Video[] = [
  { id: "v1", title: "Alpine Summit", category: "Documentary", duration: "1:24:12", views: "2.1M", thumb: "🏔️", quality: "4K", type: "vod" },
];

export const STREAMS: Stream[] = [
  { id: "s1", title: "Tokyo Live", streamer: "lens", viewers: 2000, thumb: "🗼", category: "IRL", live: true },
];