// Nintendo 3DS runtime hardening and diagnostics for Armagetron Advanced.
//
// This header is only used by the 3DS target. It exposes the small runtime
// services the port needs before and around the canonical Armagetron main
// loop: crash reporting, a debug log that survives a hard fault, and stack
// headroom accounting.

#ifndef AA3DS_RUNTIME_H
#define AA3DS_RUNTIME_H

#ifdef __cplusplus
extern "C"
{
#endif

// Installs the CPU exception handler, the C++ terminate handler and records
// the base of the main thread stack. Call this as early as possible in main,
// before any other 3DS service is used.
void aa3ds_runtime_init(void);

// Writes one line to the SD card runtime log and to the emulator/debugger
// output channel. Safe to call before the SD log has been opened.
void aa3ds_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

// Reports the deepest observed main-thread stack usage, in bytes, and the
// configured stack size. Both are zero before aa3ds_runtime_init ran.
unsigned int aa3ds_stack_used(void);
unsigned int aa3ds_stack_size(void);

// Logs a one line memory report: free application memory, heap and linear
// heap usage, and the current stack watermark.
void aa3ds_log_memory(const char* tag);

// Brings up the SOC service so the BSD socket layer works, and reports the
// address the console was given. Returns non-zero on success. Failure is not
// fatal; the client simply cannot reach the network.
int aa3ds_network_init(void);
void aa3ds_network_shutdown(void);

// Non-zero when sdmc:/3ds/armagetronad/debug_input exists. Verbose input and
// renderer tracing is gated on this so a retail run stays quiet but a console
// can be put into tracing mode by creating one empty file on the SD card.
int aa3ds_trace_enabled(void);

// Services the system's application state once per frame: sleep mode, the
// HOME button, and the request to close. Call this with no GPU frame open.
//
// Nothing used to call it, which is why choosing Close on the HOME menu left
// the console on "Closing software..." indefinitely: the request was never
// acknowledged. If the client then fails to shut itself down promptly this
// gives up waiting and ends the process, because the system holds the console
// hostage until it does.
void aa3ds_apt_frame(void);

// Non-zero once the system has asked the client to close.
int aa3ds_close_requested(void);

// Counts SDL_PollEvent round trips, so a trace can distinguish "the game never
// pumped events" from "the event pump produced nothing".
extern unsigned int aa3ds_poll_calls;

// Scripted input bridge, active only while tracing is enabled.
//
// Reads sdmc:/3ds/armagetronad/input.cmd, whose single line is
//   <hid button mask in hex> <touch x> <touch y>
// with a negative touch coordinate meaning "not touching". This lets a test
// harness drive the client without synthesising host keyboard events, which
// would otherwise fight with whatever else the user is doing.
//
// Returns non-zero when the bridge is active; buttons and touch are written
// to the out parameters.
int aa3ds_debug_input(unsigned int* buttons, int* touchX, int* touchY);

#ifdef __cplusplus
}
#endif

#endif
