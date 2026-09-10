#include "Guard.h"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <ctime>
#include <Windows.h>

using namespace RC;
using namespace RC::Unreal;

namespace Guard {

static std::atomic<uint64_t> g_Faults{0};

uint64_t FaultCount() {
    return g_Faults.load(std::memory_order_relaxed);
}

static int Filter(unsigned int code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return EXCEPTION_EXECUTE_HANDLER;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

static void ReportFault(const wchar_t* label, unsigned int code) {
    auto seen = g_Faults.fetch_add(1, std::memory_order_relaxed) + 1;

    static std::time_t lastLog = 0;
    auto now = std::time(nullptr);
    if (now - lastLog < 5 && seen > 3) return;
    lastLog = now;

    Output::send(
        STR("[SphereZones/Guard] caught fault 0x{:X} in {} - handler skipped, server kept alive (total {})\n"),
        code, label ? label : STR("unknown"), seen);
}

bool RunRaw(void (*fn)(void*), void* ctx, const wchar_t* label) {
    if (!fn) return false;

    unsigned int code = 0;
    __try {
        fn(ctx);
        return true;
    } __except (code = GetExceptionCode(), Filter(code)) {
        ReportFault(label, code);
        return false;
    }
}

bool IsReadable(const void* addr, size_t size) {
    if (!addr || size == 0) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    auto start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    auto end = start + mbi.RegionSize;
    auto want = reinterpret_cast<uintptr_t>(addr) + size;
    return want <= end;
}

bool IsReadableObject(const UObject* obj, size_t bytesNeeded) {
    auto address = reinterpret_cast<uintptr_t>(obj);
    if (address == 0) return false;

    if (address & 0x7) return false;
    if (address < 0x10000) return false;
    if (address >= 0x7FFFFFFFFFFF) return false;

    return IsReadable(obj, bytesNeeded);
}

}
