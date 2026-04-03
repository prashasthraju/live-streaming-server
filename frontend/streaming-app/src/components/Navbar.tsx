import { useAuth } from "../context/AuthContext";
import { useApp } from "../context/AppContext";

export default function Navbar() {
  const { user, logout } = useAuth();
  const { navigate } = useApp();

  return (
    <div style={{ display: "flex", justifyContent: "space-between" }}>
      <div>
        <button onClick={() => navigate("home")}>Home</button>
        <button onClick={() => navigate("live")}>Live</button>
        <button onClick={() => navigate("studio")}>Studio</button>
      </div>
      <div>
        {user?.username}
        <button onClick={logout}>Logout</button>
      </div>
    </div>
  );
}