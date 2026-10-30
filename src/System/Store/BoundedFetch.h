// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <windows.h>
#include <new>

// The task owns its request and result independently of the calling thread.
// In particular, no caller-owned async block or Store handle crosses this seam.
static LONG nexus_bounded_fetch_workers;
constexpr LONG NEXUS_BOUNDED_FETCH_MAX_WORKERS = 4;

using NexusFetch = HRESULT (WINAPI *)(void *, LONG *, void **);
using NexusFetchRelease = void (*)(void *);
struct NexusFetchTask
{
    LONG refs = 2;
    LONG canceled = 0;
    HANDLE finished = nullptr;
    HMODULE module = nullptr;
    void *request = nullptr;
    void *result = nullptr;
    NexusFetch fetch = nullptr;
    NexusFetchRelease release_request = nullptr;
    NexusFetchRelease release_result = nullptr;
    HRESULT status = E_FAIL;
};
static void nexus_fetch_release(NexusFetchTask *task)
{
    if (InterlockedDecrement(&task->refs)) return;
    if (task->result) task->release_result(task->result);
    task->release_request(task->request);
    if (task->finished) CloseHandle(task->finished);
    delete task;
}
static DWORD WINAPI nexus_fetch_worker(void *arg)
{
    auto *task = static_cast<NexusFetchTask *>(arg);
    task->status = task->fetch(task->request, &task->canceled, &task->result);
    SetEvent(task->finished);
    InterlockedDecrement(&nexus_bounded_fetch_workers);
    HMODULE module = task->module;
    nexus_fetch_release(task);
    if (module) FreeLibraryAndExitThread(module, 0);
    return 0;
}
static HRESULT nexus_bounded_fetch(void *request, NexusFetch fetch, NexusFetchRelease release_request,
                                   NexusFetchRelease release_result, DWORD timeout, void **out)
{
    if (!out || !fetch || !release_request || !release_result) return E_INVALIDARG;
    *out = nullptr;
    auto *task = new (std::nothrow) NexusFetchTask;
    if (!task) { release_request(request); return E_OUTOFMEMORY; }
    task->request = request;
    task->fetch = fetch;
    task->release_request = release_request;
    task->release_result = release_result;
    task->finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!task->finished)
    {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        task->refs = 1;
        nexus_fetch_release(task);
        return hr;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&nexus_fetch_worker), &module))
    {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        task->refs = 1;
        nexus_fetch_release(task);
        return hr;
    }
    // The main executable is never unloaded; only hold a reference to a DLL.
    if (module == GetModuleHandleW(nullptr)) module = nullptr;
    task->module = module;
    if (InterlockedIncrement(&nexus_bounded_fetch_workers) > NEXUS_BOUNDED_FETCH_MAX_WORKERS)
    {
        InterlockedDecrement(&nexus_bounded_fetch_workers);
        if (task->module) FreeLibrary(task->module);
        task->refs = 1;
        nexus_fetch_release(task);
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    HANDLE worker = CreateThread(nullptr, 0, nexus_fetch_worker, task, 0, nullptr);
    if (!worker)
    {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        InterlockedDecrement(&nexus_bounded_fetch_workers);
        if (task->module) FreeLibrary(task->module);
        task->refs = 1;
        nexus_fetch_release(task);
        return hr;
    }
    CloseHandle(worker);
    const DWORD waited = WaitForSingleObject(task->finished, timeout);
    HRESULT hr;
    if (waited == WAIT_OBJECT_0)
    {
        hr = task->status;
        if (SUCCEEDED(hr)) { *out = task->result; task->result = nullptr; }
    }
    else
    {
        hr = waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError());
        InterlockedExchange(&task->canceled, TRUE);
    }
    nexus_fetch_release(task);
    return hr;
}
