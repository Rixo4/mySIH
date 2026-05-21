import type { ReactNode } from 'react';
import { motion } from 'framer-motion';
import { Sidebar } from './Sidebar';
import { Topbar } from './Topbar';
import { MobileNav } from './MobileNav';

interface AppLayoutProps {
  pageTitle: string;
  backendConnected: boolean;
  engineOnline: boolean;
  validationRunning?: boolean;
  children: ReactNode;
}

export function AppLayout({ pageTitle, backendConnected, engineOnline, validationRunning = false, children }: AppLayoutProps) {
  return (
    <div className="min-h-screen bg-midnight-950 text-slate-100">
      <div className="pointer-events-none fixed inset-0 bg-radial-shell opacity-100" />
      <div className="relative flex min-h-screen">
        <div className="hidden xl:block xl:w-72 xl:flex-shrink-0">
          <Sidebar />
        </div>

        <div className="flex min-w-0 flex-1 flex-col">
          <Topbar
            pageTitle={pageTitle}
            backendConnected={backendConnected}
            engineOnline={engineOnline}
            validationRunning={validationRunning}
          />
          <main className="flex-1 px-4 py-5 sm:px-6 xl:px-8">
            <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.35 }}>
              {children}
            </motion.div>
          </main>
        </div>
      </div>

      <div className="fixed inset-x-0 bottom-0 h-24 bg-gradient-to-t from-midnight-950 to-transparent pointer-events-none" />

      <div className="fixed bottom-0 left-0 right-0 z-30 xl:hidden">
        <MobileNav />
      </div>
    </div>
  );
}
