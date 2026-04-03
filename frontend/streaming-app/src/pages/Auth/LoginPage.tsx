import { useState } from "react";
import { useAuth } from "../../context/AuthContext";

export default function LoginPage() {
  const { login } = useAuth();
  const [u, setU] = useState("");
  const [p, setP] = useState("");

  return (
    <div>
      <input onChange={e => setU(e.target.value)} />
      <input type="password" onChange={e => setP(e.target.value)} />
      <button onClick={() => login(u, p)}>Login</button>
    </div>
  );
}