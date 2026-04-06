import { useApp } from "../../context/AppContext";

export default function VideoPlayerPage() {
  const { selected } = useApp();

  return (
    <div>
      <h1>{selected?.title}</h1>
      <video
  controls
  width="100%"
  src={`/videos/${video?.id}.mp4`}
  style={{ borderRadius: "12px" }}
/>
    </div>
  );
}