// Nintendo 3DS runtime hardening and diagnostics for Armagetron Advanced.
//
// Three things live here, all of which have to exist before the canonical
// Armagetron code starts running:
//
// 1. The main thread stack size. libctru defaults to 32 KiB, which Armagetron
//    overruns during rendering and menu work. The overrun is silent because
//    the main stack is carved out of the bottom of the heap, so it corrupts
//    heap metadata and globals instead of faulting. That produces exactly the
//    observed symptoms: intermittent faults under Citra and a black screen on
//    real hardware.
// 2. A CPU exception handler, so a hard fault produces a register dump on the
//    SD card and on the debug output channel instead of a silent return to
//    the homebrew launcher.
// 3. A logging channel that works on real hardware (SD card) and under Citra
//    (svcOutputDebugString, which Citra prints to its log).

#include "aa3ds_runtime.h"

#include <3ds.h>
#include <3ds/allocator/mappable.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

// Overrides the weak 32 KiB default in libctru's stack_adjust.s. The stack is
// taken from the start of the heap, so this trades one megabyte of malloc
// space for headroom the canonical renderer and menu code genuinely need.
extern "C" u32 __stacksize__ = 1024 * 1024;

extern "C" unsigned int aa3ds_poll_calls = 0;

// Filled in by __system_allocateHeaps, which runs before anything can log.
extern "C" u32 aa3ds_bootRemaining = 0;
extern "C" u32 aa3ds_bootHeap = 0;
extern "C" u32 aa3ds_bootLinear = 0;

extern "C"
{
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap_size;
}

// Replaces libctru's weak default split. The default caps the application heap
// at 24 MiB and hands everything above that to the linear heap, which is the
// wrong way round here: a match uses roughly 20 MiB of application heap and
// 17 MiB of linear heap, so on an Old 3DS the default leaves the heap with
// almost no headroom while the linear heap sits mostly idle. This caps the
// linear heap instead and gives the remainder to the application heap.
//
// It also degrades instead of panicking. libctru calls svcBreak when the
// requested split does not fit, which is exactly the silent black screen this
// port was showing on hardware.
extern "C" void __system_allocateHeaps(void)
{
    // A match measured 20 MiB of application heap and 17 MiB of linear heap,
    // so the cap leaves a few megabytes of linear headroom without starving
    // the application heap on an Old 3DS.
    constexpr u32 kLinearCap = 24 << 20;
    constexpr u32 kLinearFloor = 6 << 20;

    Handle resourceLimit = 0;
    if (R_FAILED(svcGetResourceLimit(&resourceLimit, CUR_PROCESS_HANDLE)))
        svcBreak(USERBREAK_PANIC);

    s64 maxCommit = 0;
    s64 currentCommit = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, resourceLimit, &type, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, resourceLimit, &type, 1);
    svcCloseHandle(resourceLimit);

    const u32 remaining = static_cast<u32>(maxCommit - currentCommit) & ~0xFFFu;
    aa3ds_bootRemaining = remaining;

    u32 linearSize = remaining * 2 / 5;
    if (linearSize > kLinearCap)
        linearSize = kLinearCap;
    if (linearSize < kLinearFloor)
        linearSize = remaining / 2;
    linearSize &= ~0xFFFu;

    u32 heapSize = (remaining - linearSize) & ~0xFFFu;

    // Back off in steps rather than panicking if the split does not fit.
    while (linearSize >= 0x1000 &&
           R_FAILED(svcControlMemory(
               &__ctru_linear_heap, 0, 0, linearSize,
               MEMOP_ALLOC_LINEAR,
               static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE))))
    {
        linearSize = (linearSize / 2) & ~0xFFFu;
    }
    if (linearSize < 0x1000)
        svcBreak(USERBREAK_PANIC);

    while (heapSize >= 0x1000 &&
           R_FAILED(svcControlMemory(
               &__ctru_heap, OS_HEAP_AREA_BEGIN, 0, heapSize,
               MEMOP_ALLOC,
               static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE))))
    {
        heapSize = (heapSize - (heapSize / 8)) & ~0xFFFu;
    }
    if (heapSize < 0x1000)
        svcBreak(USERBREAK_PANIC);

    __ctru_heap_size = heapSize;
    __ctru_linear_heap_size = linearSize;
    aa3ds_bootHeap = heapSize;
    aa3ds_bootLinear = linearSize;

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    fake_heap_start = reinterpret_cast<char*>(__ctru_heap);
    fake_heap_end = fake_heap_start + heapSize;
}

namespace
{

constexpr const char* kCrashPath = "sdmc:/3ds/armagetronad/crash.log";

alignas(8) u8 exceptionStack[0x2000];
ERRF_ExceptionData exceptionData;

// The main stack is painted at startup so the real peak depth can be measured
// later rather than only sampled at whatever call depth happens to ask.
constexpr u32 kStackPaint = 0xa55a5aa5u;
constexpr u32 kStackGuard = 4096;
constexpr u32 kStackSlack = 512;

u32 stackTop = 0;
u32 stackPaintBase = 0;
u32 stackPaintEnd = 0;

u32* socBuffer = nullptr;

// Writes a whole file without going through stdio. The exception handler uses
// this, so it must not allocate and must not touch the stderr stream that the
// rest of the port shares.
void writeRaw(const char* path, const char* text)
{
    int handle = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (handle < 0)
        return;
    write(handle, text, std::strlen(text));
    close(handle);
}

// The SD card log is the stderr stream that main() redirects, so ordinary
// diagnostics go through stdio to keep a single writer and a single offset.
void emit(const char* text)
{
    svcOutputDebugString(text, static_cast<int>(std::strlen(text)));
    std::fputs(text, stderr);
}

char* appendText(char* cursor, char* end, const char* text)
{
    while (cursor < end - 1 && *text)
        *cursor++ = *text++;
    *cursor = '\0';
    return cursor;
}

char* appendHex(char* cursor, char* end, u32 value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[9];
    for (int index = 7; index >= 0; --index)
    {
        buffer[index] = digits[value & 0xf];
        value >>= 4;
    }
    buffer[8] = '\0';
    return appendText(cursor, end, buffer);
}

const char* exceptionName(ERRF_ExceptionType type)
{
    switch (type)
    {
    case ERRF_EXCEPTION_PREFETCH_ABORT: return "prefetch abort";
    case ERRF_EXCEPTION_DATA_ABORT: return "data abort";
    case ERRF_EXCEPTION_UNDEFINED: return "undefined instruction";
    case ERRF_EXCEPTION_VFP: return "VFP exception";
    default: return "unknown exception";
    }
}

// Runs on its own stack after a CPU fault. Everything here is allocation free
// and reentrancy free on purpose; the faulting stack may be unusable.
void handleException(ERRF_ExceptionInfo* excep, CpuRegisters* regs)
{
    static char report[1024];
    char* cursor = report;
    char* end = report + sizeof(report);

    cursor = appendText(cursor, end, "\n--- Armagetron 3DS fatal fault ---\ntype: ");
    cursor = appendText(cursor, end, exceptionName(excep->type));
    cursor = appendText(cursor, end, "\nfsr: 0x");
    cursor = appendHex(cursor, end, excep->fsr);
    cursor = appendText(cursor, end, "  far: 0x");
    cursor = appendHex(cursor, end, excep->far);
    cursor = appendText(cursor, end, "\npc: 0x");
    cursor = appendHex(cursor, end, regs->pc);
    cursor = appendText(cursor, end, "  lr: 0x");
    cursor = appendHex(cursor, end, regs->lr);
    cursor = appendText(cursor, end, "  sp: 0x");
    cursor = appendHex(cursor, end, regs->sp);
    cursor = appendText(cursor, end, "  cpsr: 0x");
    cursor = appendHex(cursor, end, regs->cpsr);
    cursor = appendText(cursor, end, "\nstack top: 0x");
    cursor = appendHex(cursor, end, stackTop);
    cursor = appendText(cursor, end, "  size: 0x");
    cursor = appendHex(cursor, end, __stacksize__);

    for (int index = 0; index < 13; ++index)
    {
        cursor = appendText(cursor, end, (index % 4) == 0 ? "\nr" : "  r");
        char number[3];
        number[0] = static_cast<char>('0' + index / 10);
        number[1] = static_cast<char>('0' + index % 10);
        number[2] = '\0';
        cursor = appendText(cursor, end, index < 10 ? number + 1 : number);
        cursor = appendText(cursor, end, ": 0x");
        cursor = appendHex(cursor, end, regs->r[index]);
    }
    appendText(cursor, end, "\n--- end fault ---\n");

    svcOutputDebugString(report, static_cast<int>(std::strlen(report)));
    writeRaw(kCrashPath, report);

    ERRF_ExceptionHandler(excep, regs);
}

void handleTerminate()
{
    const char* description = "unknown";
    try
    {
        std::exception_ptr pending = std::current_exception();
        if (pending)
            std::rethrow_exception(pending);
    }
    catch (const std::exception& error)
    {
        description = error.what();
    }
    catch (...)
    {
    }

    aa3ds_log("fatal: unhandled C++ exception: %s", description);
    aa3ds_log_memory("terminate");
    svcBreak(USERBREAK_PANIC);
    for (;;)
    {
    }
}

}

namespace
{

constexpr const char* kBootLogPath = "sdmc:/3ds/armagetronad/boot.log";

void bootTrace(const char* stage)
{
    svcOutputDebugString(stage, static_cast<int>(std::strlen(stage)));

    int handle = open(kBootLogPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (handle < 0)
        return;
    write(handle, stage, std::strlen(stage));
    write(handle, "\n", 1);
    close(handle);
}

void bootTraceNumber(const char* label, unsigned long value)
{
    char line[96];
    char* cursor = line;
    char* end = line + sizeof(line);
    cursor = appendText(cursor, end, label);
    cursor = appendText(cursor, end, " 0x");
    appendHex(cursor, end, static_cast<u32>(value));
    bootTrace(line);
}

// Priority 101 is the earliest a user constructor can ask for, so this runs
// before Armagetron's own static initialisation.
__attribute__((constructor(101))) void aa3ds_boot_trace_init()
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/armagetronad", 0777);

    // Truncate any trace from the previous run.
    int handle = open(kBootLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (handle >= 0)
        close(handle);

    bootTrace("boot: reached static initialisation");
    bootTraceNumber("boot: memory granted", aa3ds_bootRemaining);
    bootTraceNumber("boot: application heap", aa3ds_bootHeap);
    bootTraceNumber("boot: linear heap", aa3ds_bootLinear);

    // Install the fault handler now rather than in main, so a fault during
    // static initialisation is still reported.
    threadOnException(handleException, exceptionStack + sizeof(exceptionStack), &exceptionData);
    std::set_terminate(handleTerminate);

    bootTrace("boot: fault handler installed, running static constructors");
}

}

extern "C" void aa3ds_runtime_init(void)
{
    stackTop = reinterpret_cast<u32>(__builtin_frame_address(0));

    // Paint everything between the bottom of the stack and the current frame.
    // A guard page of slack at the bottom keeps the paint inside the region
    // libctru actually carved out, because stackTop is a frame or two below
    // the true top of the stack.
    stackPaintBase = stackTop - __stacksize__ + kStackGuard;
    stackPaintEnd = (stackTop - kStackSlack) & ~3u;
    for (u32 address = stackPaintBase; address < stackPaintEnd; address += 4)
        *reinterpret_cast<volatile u32*>(address) = kStackPaint;

    threadOnException(handleException, exceptionStack + sizeof(exceptionStack), &exceptionData);
    std::set_terminate(handleTerminate);

    bootTrace("boot: reached main");

    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);

    // A New 3DS boots homebrew at the Old 3DS clock with the extra L2 cache
    // disabled. Armagetron needs every cycle it can get, and this is a no-op
    // on an Old 3DS.
    if (isNew3DS)
        osSetSpeedupEnable(true);

    aa3ds_log(
        "runtime: model=%s stack=%luKiB heap=%luKiB linear=%luKiB",
        isNew3DS ? "new3ds" : "old3ds",
        static_cast<unsigned long>(__stacksize__ / 1024),
        static_cast<unsigned long>(envGetHeapSize() / 1024),
        static_cast<unsigned long>(envGetLinearHeapSize() / 1024));
    aa3ds_log_memory("startup");
}

extern "C" void aa3ds_log(const char* format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(line, sizeof(line) - 2, format, arguments);
    va_end(arguments);
    if (written < 0)
        return;
    if (written > static_cast<int>(sizeof(line) - 3))
        written = static_cast<int>(sizeof(line) - 3);
    line[written] = '\n';
    line[written + 1] = '\0';
    emit(line);
}

extern "C" unsigned int aa3ds_stack_used(void)
{
    if (stackPaintBase == 0)
        return 0;

    u32 address = stackPaintBase;
    while (address < stackPaintEnd &&
           *reinterpret_cast<volatile u32*>(address) == kStackPaint)
        address += 4;

    return stackTop - address;
}

extern "C" unsigned int aa3ds_stack_size(void)
{
    return __stacksize__;
}

extern "C" int aa3ds_network_init(void)
{
    if (socBuffer)
        return 1;

    // SOC wants a page aligned buffer it keeps for the lifetime of the
    // service. 128 KiB is the size the libctru examples use and is enough for
    // the handful of sockets this client opens.
    constexpr u32 kSocBufferSize = 0x20000;
    socBuffer = static_cast<u32*>(memalign(0x1000, kSocBufferSize));
    if (!socBuffer)
    {
        aa3ds_log("network: out of memory for the SOC buffer");
        return 0;
    }

    Result result = socInit(socBuffer, kSocBufferSize);
    if (R_FAILED(result))
    {
        aa3ds_log("network: socInit failed (0x%08lx)", (unsigned long)result);
        free(socBuffer);
        socBuffer = nullptr;
        return 0;
    }

    // gethostid returns the address the console was assigned, so the log says
    // whether the system actually joined a network rather than only whether
    // the service started.
    const u32 address = gethostid();
    aa3ds_log(
        "network: ready, address %lu.%lu.%lu.%lu",
        (unsigned long)(address & 0xff),
        (unsigned long)((address >> 8) & 0xff),
        (unsigned long)((address >> 16) & 0xff),
        (unsigned long)((address >> 24) & 0xff));
    return 1;
}

extern "C" void aa3ds_network_shutdown(void)
{
    if (!socBuffer)
        return;
    socExit();
    // The SOC buffer is deliberately leaked: the service may still be tearing
    // down when this returns, and it is freed with the process anyway.
    socBuffer = nullptr;
}

// libctru's own shutdown, called by the C runtime once main returns. It is not
// in a public header, but it is weakly defined in the library, so declaring it
// here links against the real one.
extern "C" void __libctru_exit(int rc) __attribute__((noreturn));

namespace
{
bool closeRequested = false;
u64 closeRequestedAt = 0;

// How long the client gets to shut itself down after the system asks. Long
// enough for the configuration to be written to the SD card, short enough that
// nobody thinks the console has hung.
constexpr u64 kCloseDeadlineMs = 5000;
}

extern "C" void aa3ds_apt_frame(void)
{
    if (aptMainLoop())
        return;

    if (!closeRequested)
    {
        closeRequested = true;
        closeRequestedAt = osGetTime();
        aa3ds_log("apt: system asked the client to close");
        return;
    }

    if (osGetTime() - closeRequestedAt < kCloseDeadlineMs)
        return;

    // The HOME menu is sitting on "Closing software..." until this process is
    // gone, so an orderly shutdown that does not finish is worse than one that
    // never starts.
    //
    // This is the same routine the C runtime calls once main returns, so the
    // system services are closed properly; it simply skips the static
    // destructors, which is where a shutdown that overran this long is stuck.
    // Plain _exit would do neither and leave the services to the kernel.
    static bool giveUpLogged = false;
    if (!giveUpLogged)
    {
        giveUpLogged = true;
        aa3ds_log("apt: shutdown overran %u ms, ending the process", (unsigned)kCloseDeadlineMs);
        aa3ds_log_memory("forced exit");
    }
    __libctru_exit(0);
}

extern "C" int aa3ds_close_requested(void)
{
    return closeRequested ? 1 : 0;
}

extern "C" int aa3ds_debug_input(unsigned int* buttons, int* touchX, int* touchY)
{
    if (!aa3ds_trace_enabled())
        return 0;

    static unsigned int cachedButtons = 0;
    static int cachedX = -1;
    static int cachedY = -1;

    {
        int handle = open("sdmc:/3ds/armagetronad/input.cmd", O_RDONLY);
        if (handle >= 0)
        {
            char line[64] = {0};
            ssize_t got = read(handle, line, sizeof(line) - 1);
            close(handle);
            if (got > 0)
            {
                unsigned int parsedButtons = 0;
                int parsedX = -1;
                int parsedY = -1;
                if (sscanf(line, "%x %d %d", &parsedButtons, &parsedX, &parsedY) >= 1)
                {
                    cachedButtons = parsedButtons;
                    cachedX = parsedX;
                    cachedY = parsedY;
                }
            }
        }
    }

    *buttons = cachedButtons;
    *touchX = cachedX;
    *touchY = cachedY;
    return 1;
}

extern "C" int aa3ds_trace_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        struct stat info;
        cached = stat("sdmc:/3ds/armagetronad/debug_input", &info) == 0 ? 1 : 0;
    }
    return cached;
}

extern "C" void aa3ds_log_memory(const char* tag)
{
    unsigned int peak = aa3ds_stack_used();
    struct mallinfo heap = mallinfo();
    aa3ds_log(
        "memory[%s]: heapUsed=%luKiB/%luKiB linearFree=%luKiB/%luKiB "
        "stackPeak=%luB/%luB",
        tag,
        static_cast<unsigned long>(heap.uordblks / 1024),
        static_cast<unsigned long>(envGetHeapSize() / 1024),
        static_cast<unsigned long>(linearSpaceFree() / 1024),
        static_cast<unsigned long>(envGetLinearHeapSize() / 1024),
        static_cast<unsigned long>(peak),
        static_cast<unsigned long>(__stacksize__));

    if (stackPaintBase != 0 && peak >= __stacksize__ - kStackGuard - kStackSlack)
        aa3ds_log("warning: main thread stack exhausted, raise __stacksize__");
}
