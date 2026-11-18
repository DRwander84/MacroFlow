#pragma once
#include <cstdint>
#include "core/MacroTypes.h"

namespace mmr::winprobe {

// Recording side -------------------------------------------------------------------------------
// Top-level window under the screen point (physical pixels). false: none / it belongs to this app.
bool windowAt(int x, int y, WindowAnchor& out);
// True when the topmost window at the screen point belongs to this app, i.e. the point is really on our window and
// not on another program that was brought in front of it. Never blocks (reads window data only): safe in a hook.
bool ownWindowOnTopAt(int x, int y);
// Class hash of the window that currently has keyboard focus. 0: none / it belongs to this app.
uint32_t foregroundHash();

// Playback side --------------------------------------------------------------------------------
// Ready: a visible, non-minimised window of the recorded class exists; dx/dy = its movement since recording
// (0,0 when it is exactly where it was). The window nearest to the recorded position wins.
// NotReady: no such window (yet).
AnchorState locateWindow(const WindowAnchor& a, int& dx, int& dy);
AnchorState focusMatches(uint32_t classHash);
// Ready: the top-level window under the point answers messages (or is this app's / unknown / not checkable).
// NotReady: it is hung or still starting up, so input must not be driven into it yet. Playback thread only.
AnchorState targetReady(int x, int y, bool discrete);

}  // namespace mmr::winprobe
