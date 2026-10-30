// SPDX-License-Identifier: MIT
#include "../../src/System/Store/BoundedFetch.h"
struct Fixture { HANDLE started, gate, finished; };
static HRESULT WINAPI wait_for_gate(void *arg, LONG *, void **)
{
    auto *fixture = static_cast<Fixture *>(arg);
    SetEvent(fixture->started);
    WaitForSingleObject(fixture->gate, 5000);
    SetEvent(fixture->finished);
    return E_ABORT;
}
static void no_op(void *) {}
extern "C" __declspec(dllexport) HRESULT WINAPI bounded_fixture_run(Fixture *fixture)
{
    void *result = nullptr;
    return nexus_bounded_fetch(fixture, wait_for_gate, no_op, no_op, 50, &result);
}
