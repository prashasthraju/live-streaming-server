import type { Video } from "../types/video";

export default function VideoCard({ video, onClick }: any) {
  return (
    <div onClick={onClick}>
      <h3>{video.title}</h3>
    </div>
  );
}