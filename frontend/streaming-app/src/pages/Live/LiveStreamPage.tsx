import { useApp } from "../../context/AppContext";
import Hls from "hls.js";

export default function LiveStreamPage() {
  const { selected } = useApp();
    const videoRef = useRef<HTMLVideoElement | null>(null);
    useEffect(() => {
  if (!stream) return;

  const video = videoRef.current;
  if (!video) return;

  if (Hls.isSupported()) {
    const hls = new Hls();
    hls.loadSource(`/hls/${stream.id}.m3u8`);
    hls.attachMedia(video);

    return () => hls.destroy();
  } else {
    video.src = `/hls/${stream.id}.m3u8`;
  }
}, [stream]);
  return (
    <div>
      <h1>{selected?.title}</h1>
     <video ref={videoRef} controls width="100%" style={{ borderRadius: "12px" }} />
    </div>
  );
}