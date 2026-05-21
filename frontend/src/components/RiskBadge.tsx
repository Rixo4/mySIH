import { StatusBadge } from './StatusBadge';

interface RiskBadgeProps {
  value?: string | null;
}

export function RiskBadge({ value }: RiskBadgeProps) {
  return <StatusBadge label="Risk" value={value} />;
}
