import type { Stream } from "../types/stream";

export default function StreamCard({ stream, onClick }: any) {
  return (
    <div onClick={onClick}>
      <h3>{stream.title}</h3>
    </div>
  );
}