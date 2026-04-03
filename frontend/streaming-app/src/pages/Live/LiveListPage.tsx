import { STREAMS } from "../../data/mockData";
import { useApp } from "../../context/AppContext";
import StreamCard from "../../components/StreamCard";

export default function LiveListPage() {
  const { navigate } = useApp();

  return (
    <div>
      {STREAMS.map(s => (
        <StreamCard key={s.id} stream={s} onClick={() => navigate("stream", s)} />
      ))}
    </div>
  );
}