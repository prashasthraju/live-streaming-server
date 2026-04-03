import { createContext, useContext, useState } from "react";
import type{ Video } from "../types/video";
import type { Stream } from "../types/stream";

type View = "home" | "live" | "player" | "stream" | "studio";

interface AppContextType {
  view: View;
  selected: Video | Stream | null;
  navigate: (v: View, item?: any) => void;
}

const AppContext = createContext<AppContextType | null>(null);

export const AppProvider = ({ children }: any) => {
  const [view, setView] = useState<View>("home");
  const [selected, setSelected] = useState<any>(null);

  const navigate = (v: View, item: any = null) => {
    setView(v);
    setSelected(item);
  };

  return (
    <AppContext.Provider value={{ view, selected, navigate }}>
      {children}
    </AppContext.Provider>
  );
};

export const useApp = () => useContext(AppContext)!;