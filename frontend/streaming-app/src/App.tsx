import { useAuth } from "./context/AuthContext";
import { useApp } from "./context/AppContext";

import Navbar from "./components/Navbar";
import LoginPage from "./pages/Auth/LoginPage";
import HomePage from "./pages/Home/HomePage";
import LiveListPage from "./pages/Live/LiveListPage";
import LiveStreamPage from "./pages/Live/LiveStreamPage";
import VideoPlayerPage from "./pages/Player/VideoPlayerPage";
import StudioPage from "./pages/Studio/StudioPage";

export default function App() {
  const { user } = useAuth();
  const { view } = useApp();

  if (!user) return <LoginPage />;

  return (
    <div>
      <Navbar />

      {view === "home" && <HomePage />}
      {view === "live" && <LiveListPage />}
      {view === "stream" && <LiveStreamPage />}
      {view === "player" && <VideoPlayerPage />}
      {view === "studio" && <StudioPage />}
    </div>
  );
}