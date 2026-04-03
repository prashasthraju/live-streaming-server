export type Plan = "Free" | "Pro";

export interface User {
  id: string;
  username: string;
  password: string;
  email: string;
  avatar: string;
  plan: Plan;
}