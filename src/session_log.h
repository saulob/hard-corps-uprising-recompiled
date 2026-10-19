#pragma once

namespace hcu {

// Registers the clean-exit finalizer once. With Detailed logging off, a normal process exit then
// discards this run's log; with it on, the log is kept. Call after logging is initialized.
void InstallSessionLogCleanup();

// Discards this run's log when Detailed logging is off. Idempotent and never deletes a log that
// already contains a CRASH REPORT. Call at a final boundary only: the normal exit through atexit,
// or immediately before the hard exit of the host window close.
void FinalizeSessionLog();

}  // namespace hcu
