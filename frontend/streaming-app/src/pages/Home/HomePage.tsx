import { VIDEOS } from "../../data/mockData";
import { useApp } from "../../context/AppContext";
import VideoCard from "../../components/VideoCard";

export default function HomePage() {
  const { navigate } = useApp();

  return (
    <div>
      {VIDEOS.map(v => (
        <VideoCard key={v.id} video={v} onClick={() => navigate("player", v)} />
      ))}
    </div>
  );
}