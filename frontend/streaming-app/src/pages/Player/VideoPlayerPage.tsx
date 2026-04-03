import { useApp } from "../../context/AppContext";

export default function VideoPlayerPage() {
  const { selected } = useApp();

  return (
    <div>
      <h1>{selected?.title}</h1>
      <video controls src="/videos/sample.mp4" />
    </div>
  );
}