import { useApp } from "../../context/AppContext";

export default function LiveStreamPage() {
  const { selected } = useApp();

  return (
    <div>
      <h1>{selected?.title}</h1>
      <video controls src={`/hls/${selected?.id}.m3u8`} />
    </div>
  );
}