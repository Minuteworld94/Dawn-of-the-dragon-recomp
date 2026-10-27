// Hang finder (WORK_LOG 101): if no progress is reported for a while, pause every thread,
// walk its native stack and log each frame as the recompiled game function it is in
// (sub_XXXXXXXX, via the generated PPCFuncMappings table). No debugger or PDB needed.

#pragma once

namespace spyro::hang_dump {

// Starts the watchdog (once): after `seconds` without Progress(), dumps all thread stacks to
// the log (at most `max_dumps` times).
void Start(int seconds, int max_dumps = 2);

// Call regularly from the thing that should keep moving (e.g. every Present).
void Progress();

// Names an extra host function for the stack dump: the __imp__sub_X original of a function
// we hook is not in PPCFuncMappings (that table points at our hook). Call before Start().
void AddSymbol(const void* host_function, unsigned guest_address);

}  // namespace spyro::hang_dump
