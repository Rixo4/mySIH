import React, { useState } from 'react';
import { TopWorkspaceBar } from './layout/TopWorkspaceBar';
import { CommandPalette } from './layout/CommandPalette';
import { ActiveJobsDrawer } from './layout/ActiveJobsDrawer';

interface AppLayoutProps {
  pageTitle: string;
  backendConnected: boolean;
  engineOnline: boolean;
  validationRunning?: boolean;
  children: React.ReactNode;
}

export function AppLayout({
  pageTitle,
  backendConnected,
  engineOnline,
  children,
}: AppLayoutProps) {
  const [isCommandPaletteOpen, setIsCommandPaletteOpen] = useState(false);
  const [isActiveJobsOpen, setIsActiveJobsOpen] = useState(false);
  const [_isMobileMenuOpen, setIsMobileMenuOpen] = useState(false);

  return (
    <div className="flex h-screen w-screen overflow-hidden observatory-bg text-slate-100 antialiased font-sans">
      {/* Main Research Console Area (Full Viewport Width) */}
      <div className="flex flex-1 flex-col h-full min-w-0 overflow-hidden transition-all duration-300">
        {/* Top Workspace Bar with Integrated Research Navigation */}
        <TopWorkspaceBar
          pageTitle={pageTitle}
          backendConnected={backendConnected}
          engineOnline={engineOnline}
          onOpenCommandPalette={() => setIsCommandPaletteOpen(true)}
          onOpenActiveJobs={() => setIsActiveJobsOpen(true)}
          onToggleMobileMenu={() => setIsMobileMenuOpen(true)}
        />

        {/* Workspace Main Display Content */}
        <main className="flex-1 min-h-0 overflow-y-auto relative" style={{ padding: 0 }}>
          <div className="w-full min-h-full">
            {children}
          </div>
        </main>
      </div>

      {/* Command Palette Modal (Ctrl + K) */}
      <CommandPalette
        isOpen={isCommandPaletteOpen}
        onClose={() => setIsCommandPaletteOpen(false)}
      />

      {/* Active Jobs Drawer */}
      <ActiveJobsDrawer
        isOpen={isActiveJobsOpen}
        onClose={() => setIsActiveJobsOpen(false)}
      />
    </div>
  );
}
