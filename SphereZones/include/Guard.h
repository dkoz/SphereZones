#pragma once

#include <Unreal/UObject.hpp>

#include <cstdint>
#include <utility>

namespace Guard {

bool RunRaw(void (*fn)(void*), void* ctx, const wchar_t* label);

uint64_t FaultCount();

template <typename F>
bool Run(const wchar_t* label, F&& f) {
    auto thunk = [](void* p) { (*static_cast<F*>(p))(); };
    return RunRaw(thunk, &f, label);
}

bool IsReadableObject(const RC::Unreal::UObject* obj, size_t bytesNeeded = 8);

bool IsReadable(const void* addr, size_t size);

}
