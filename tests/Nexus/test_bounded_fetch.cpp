// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "../../src/System/Store/BoundedFetch.h"
#include <atomic>

namespace {
struct Fixture { HANDLE started, gate, finished; std::atomic<int> destroyed{0}; };
HRESULT WINAPI blocked_fetch(void *arg, LONG *cancelled, void **result)
{
    auto *f = static_cast<Fixture *>(arg);
    SetEvent(f->started);
    WaitForSingleObject(f->gate, 5000);
    if (InterlockedCompareExchange(cancelled, 0, 0)) return E_ABORT;
    *result = new int(42);
    return S_OK;
}
void destroy_request(void *arg)
{
    auto *f = static_cast<Fixture *>(arg);
    ++f->destroyed;
    SetEvent(f->finished);
}
void destroy_result(void *p) { delete static_cast<int *>(p); }
}

TEST(BoundedFetch, TimeoutReturnsWithoutWaitingForWorkerAndWorkerOwnsItsInput)
{
    Fixture f{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    void *result = reinterpret_cast<void *>(1);
    EXPECT_EQ(nexus_bounded_fetch(&f, blocked_fetch, destroy_request, destroy_result, 50, &result), HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(WaitForSingleObject(f.started, 1000), WAIT_OBJECT_0);
    EXPECT_EQ(f.destroyed.load(), 0);
    SetEvent(f.gate);
    EXPECT_EQ(WaitForSingleObject(f.finished, 1000), WAIT_OBJECT_0);
    EXPECT_EQ(f.destroyed.load(), 1);
    CloseHandle(f.started); CloseHandle(f.gate); CloseHandle(f.finished);
}

TEST(BoundedFetch, SuccessfulResultTransfersToCaller)
{
    Fixture f{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, TRUE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    void *result = nullptr;
    EXPECT_EQ(nexus_bounded_fetch(&f, blocked_fetch, destroy_request, destroy_result, 1000, &result), S_OK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*static_cast<int *>(result), 42);
    destroy_result(result);
    EXPECT_EQ(WaitForSingleObject(f.finished, 1000), WAIT_OBJECT_0);
    EXPECT_EQ(f.destroyed.load(), 1);
    CloseHandle(f.started); CloseHandle(f.gate); CloseHandle(f.finished);
}

TEST(BoundedFetch, TimedOutWorkerKeepsItsDllLoadedUntilItExits)
{
    struct ModuleFixture { HANDLE started, gate, finished; } fixture{
        CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HMODULE module = LoadLibraryW(L"bounded_fetch_fixture.dll");
    ASSERT_NE(module, nullptr);
    auto run = reinterpret_cast<HRESULT (WINAPI *)(ModuleFixture *)>(GetProcAddress(module, "bounded_fixture_run"));
    if (!run) run = reinterpret_cast<HRESULT (WINAPI *)(ModuleFixture *)>(GetProcAddress(module, "bounded_fixture_run@4"));
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run(&fixture), HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    EXPECT_EQ(WaitForSingleObject(fixture.started, 1000), WAIT_OBJECT_0);
    ASSERT_TRUE(FreeLibrary(module));
    EXPECT_NE(GetModuleHandleW(L"bounded_fetch_fixture.dll"), nullptr);
    SetEvent(fixture.gate);
    EXPECT_EQ(WaitForSingleObject(fixture.finished, 1000), WAIT_OBJECT_0);
    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (GetModuleHandleW(L"bounded_fetch_fixture.dll") && GetTickCount64() < deadline) Sleep(1);
    EXPECT_EQ(GetModuleHandleW(L"bounded_fetch_fixture.dll"), nullptr);
    CloseHandle(fixture.started); CloseHandle(fixture.gate); CloseHandle(fixture.finished);
}

TEST(BoundedFetch, OutstandingTimedOutRequestsAreLimitedAndCleanedUp)
{
    Fixture f{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    for (LONG i = 0; i < NEXUS_BOUNDED_FETCH_MAX_WORKERS; ++i)
    {
        void *result = nullptr;
        EXPECT_EQ(nexus_bounded_fetch(&f, blocked_fetch, destroy_request, destroy_result, 25, &result), HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        EXPECT_EQ(result, nullptr);
    }
    void *extra = nullptr;
    EXPECT_EQ(nexus_bounded_fetch(&f, blocked_fetch, destroy_request, destroy_result, 25, &extra), HRESULT_FROM_WIN32(ERROR_BUSY));
    EXPECT_EQ(f.destroyed.load(), 1);
    SetEvent(f.gate);
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (f.destroyed.load() != NEXUS_BOUNDED_FETCH_MAX_WORKERS + 1 && GetTickCount64() < deadline) Sleep(1);
    EXPECT_EQ(f.destroyed.load(), NEXUS_BOUNDED_FETCH_MAX_WORKERS + 1);
    EXPECT_EQ(InterlockedCompareExchange(&nexus_bounded_fetch_workers, 0, 0), 0);
    CloseHandle(f.started); CloseHandle(f.gate); CloseHandle(f.finished);
}
