import { createContext, useContext, useState } from "react";
import { USERS } from "../data/mockData";
import type { User } from "../types/user";

interface AuthContextType {
  user: User | null;
  login: (u: string, p: string) => boolean;
  logout: () => void;
}

const AuthContext = createContext<AuthContextType | null>(null);

export const AuthProvider = ({ children }: any) => {
  const [user, setUser] = useState<User | null>(null);

  const login = (username: string, password: string) => {
    const u = USERS.find(x => x.username === username && x.password === password);
    if (!u) return false;
    setUser(u);
    return true;
  };

  const logout = () => setUser(null);

  return (
    <AuthContext.Provider value={{ user, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
};

export const useAuth = () => useContext(AuthContext)!;