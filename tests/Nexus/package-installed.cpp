/* SPDX-License-Identifier: MIT
 * Actual IXPackage ABI and generated layout files. No game, account or license.
 */
#define INITGUID
#include "xasyncprovider.h"
#include "xpackage.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
static LONG checks, failures;
#define CHECK(name, value)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        InterlockedIncrement(&checks);                                                                                 \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            InterlockedIncrement(&failures);                                                                           \
            printf("FAIL %s line=%d\n", name, __LINE__);                                                               \
        }                                                                                                              \
    } while (0)
static IXPackageImpl4 *package;
static IXThreadingImpl *threading;
static WCHAR root[MAX_PATH];
static void file(const WCHAR *folder, const WCHAR *name, const char *text)
{
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls\\%ls", root, folder, name);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written;
    CHECK("fixture-open", h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    CHECK("fixture-write", WriteFile(h, text, strlen(text), &written, nullptr) && written == strlen(text));
    CloseHandle(h);
}
static void fixture(unsigned number, BOOL complete, BOOL related = TRUE)
{
    WCHAR folder[20], path[MAX_PATH], layout[80];
    char xml[2048];
    swprintf(folder, 20, L"dlc%u", number);
    swprintf(path, MAX_PATH, L"%ls\\%ls", root, folder);
    CHECK("fixture-directory", CreateDirectoryW(path, nullptr));
    snprintf(xml, sizeof(xml),
             "<Game><Identity Name=\"Nexus.Dlc%u\" Publisher=\"CN=Probe\" "
             "Version=\"1.0.0.0\"/><StoreId>D%011u</StoreId><DesktopRegistration><MainPackageDependency "
             "Name=\"%s\"/></DesktopRegistration><AllowedProducts><AllowedProduct>B00000000001</AllowedProduct></"
             "AllowedProducts><ShellVisuals DefaultDisplayName=\"Synthetic DLC\"/></Game>",
             number, number, related ? "Nexus.Base" : "Foreign.Base");
    file(folder, L"MicrosoftGame.config", xml);
    snprintf(xml, sizeof(xml),
             "<Package><Identity Name=\"Nexus.Dlc%u\" Publisher=\"CN=Probe\" "
             "Version=\"1.0.0.0\"/><Dependencies><MainPackageDependency Name=\"%s\"/></Dependencies></Package>",
             number, related ? "Nexus.Base" : "Foreign.Base");
    file(folder, L"appxmanifest.xml", xml);
    swprintf(layout, 80, L"layout_%08x-1111-2222-3333-444444444444.xml", number);
    file(folder, layout,
         "<Package><Chunk Id=\"Registration\"><FileGroup SourcePath=\".\" Include=\"MicrosoftGame.config\"/><FileGroup "
         "SourcePath=\".\" Include=\"payload.bin\"/></Chunk></Package>");
    if (complete)
        file(folder, L"payload.bin", "synthetic content, not executable");
}
struct state
{
    XTaskQueueHandle queue;
    XTaskQueueRegistrationToken token;
    LONG calls;
    HANDLE started, release, finished;
    BOOL self;
};
static void CALLBACK installed(void *arg, const XPackageDetails *details)
{
    auto *s = static_cast<state *>(arg);
    InterlockedIncrement(&s->calls);
    CHECK("details", details && details->kind == XPackageKind::Content && !details->installing && details->index == 0 &&
                         details->count == 1);
    CHECK("details-id", details && strlen(details->packageIdentifier) == 32 && strlen(details->storeId) == 12);
    CHECK("details-scope", details && details->displayName && !strcmp(details->displayName, "Synthetic DLC"));
    if (s->self)
        CHECK("self-unregister-no-deadlock", !package->XPackageUnregisterPackageInstalled(s->token, TRUE));
    if (s->started)
    {
        SetEvent(s->started);
        CHECK("callback-release", WaitForSingleObject(s->release, 5000) == WAIT_OBJECT_0);
    }
}
static BOOL dispatch(state &s, DWORD timeout)
{
    return threading->XTaskQueueDispatch(s.queue, XTaskQueuePort::Completion, timeout);
}
static state make()
{
    state s{};
    CHECK("queue", threading->XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual,
                                               &s.queue) == S_OK &&
                       s.queue);
    return s;
}
static DWORD WINAPI dispatcher(void *p)
{
    auto *s = static_cast<state *>(p);
    CHECK("dispatch-thread", dispatch(*s, 4000));
    return 0;
}
static DWORD WINAPI waiter(void *p)
{
    auto *s = static_cast<state *>(p);
    CHECK("wait-unregister", package->XPackageUnregisterPackageInstalled(s->token, TRUE));
    SetEvent(s->finished);
    return 0;
}
int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WCHAR temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    swprintf(root, MAX_PATH, L"%lsnexus-installed-%lu-%lu", temp, GetCurrentProcessId(), GetTickCount());
    CHECK("root", CreateDirectoryW(root, nullptr));
    SetEnvironmentVariableW(L"XODUS_CONTENT_ROOTS", root);
    fixture(1, TRUE);
    HMODULE dll = LoadLibraryW(L"xgameruntime.dll");
    CHECK("load", dll);
    if (!dll)
        return 2;
    auto initialize =
        (HRESULT(WINAPI *)(ULONG, ULONG, char, const void *))(void *)GetProcAddress(dll, "InitializeApiImplEx2");
    auto query = (HRESULT(WINAPI *)(REFCLSID, REFIID, void **))(void *)GetProcAddress(dll, "QueryApiImpl");
    struct
    {
        UINT32 tag;
        BOOL inline_config;
        const char *config;
    } options = {1, TRUE,
                 "<Game><Identity Name=\"Nexus.Base\" Publisher=\"CN=Probe\"/><StoreId>B00000000001</StoreId></Game>"};
    CHECK("initialize", initialize && initialize(0, 0, 0, &options) == S_OK);
    CHECK("package-ABI", query && query(CLSID_XPackageImpl, IID_IXPackageImpl4, (void **)&package) == S_OK && package);
    CHECK("threading-ABI",
          query && query(CLSID_XThreadingImpl, IID_IXThreadingImpl, (void **)&threading) == S_OK && threading);
    if (!package || !threading)
        return 2;
    state s = make();
    CHECK("null-token",
          package->XPackageRegisterPackageInstalledLegacy(s.queue, &s, installed, nullptr) == E_INVALIDARG);
    CHECK("null-callback",
          package->XPackageRegisterPackageInstalledLegacy(s.queue, &s, nullptr, &s.token) == E_INVALIDARG &&
              !s.token.token);
    HRESULT hr = package->XPackageRegisterPackageInstalledLegacy(s.queue, &s, installed, &s.token);
    printf("RegisterLegacy=%08lx\n", hr);
    CHECK("register-legacy", hr == S_OK && s.token.token);
    if (FAILED(hr))
    {
        printf("CHECKS %ld FAILURES %ld\n", checks, failures);
        return 1;
    }
    CHECK("no-registration-replay", !dispatch(s, 1300) && !s.calls);
    fixture(2, FALSE);
    CHECK("incomplete-not-installed", !dispatch(s, 1300) && !s.calls);
    file(L"dlc2", L"payload.bin", "synthetic content");
    CHECK("completed-arrival", dispatch(s, 3000) && s.calls == 1);
    CHECK("no-duplicate", !dispatch(s, 1300) && s.calls == 1);
    fixture(3, TRUE, FALSE);
    CHECK("foreign-package-ignored", !dispatch(s, 1300) && s.calls == 1);
    fixture(4, TRUE);
    Sleep(1300);
    CHECK("cancel-queued", package->XPackageUnregisterPackageInstalled(s.token, TRUE));
    while (dispatch(s, 0))
    {
    }
    CHECK("no-callback-after-unregister", s.calls == 1);
    threading->XTaskQueueCloseHandle(s.queue);
    for (unsigned version = 0; version < 4; ++version)
    {
        state a = make();
        HRESULT result = version == 0
                             ? package->XPackageRegisterPackageInstalledLegacy(a.queue, &a, installed, &a.token)
                         : version == 1 ? package->XPackageRegisterPackageInstalled(a.queue, &a, installed, &a.token)
                         : version == 2 ? package->XPackageRegisterPackageInstalled2(a.queue, &a, installed, &a.token)
                                        : package->XPackageRegisterPackageInstalled3(a.queue, &a, installed, &a.token);
        CHECK("ABI-register-alias", result == S_OK && a.token.token);
        CHECK("ABI-unregister", package->XPackageUnregisterPackageInstalled(a.token, TRUE));
        threading->XTaskQueueCloseHandle(a.queue);
    }
    state d{};
    CHECK("default-queue",
          package->XPackageRegisterPackageInstalledLegacy(nullptr, &d, installed, &d.token) == S_OK && d.token.token);
    CHECK("default-unregister", package->XPackageUnregisterPackageInstalled(d.token, TRUE));
    state b = make();
    b.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    b.release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    b.finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK("register-blocking", package->XPackageRegisterPackageInstalled3(b.queue, &b, installed, &b.token) == S_OK);
    fixture(5, TRUE);
    HANDLE runner = CreateThread(nullptr, 0, dispatcher, &b, 0, nullptr);
    CHECK("callback-entered", WaitForSingleObject(b.started, 4000) == WAIT_OBJECT_0);
    CHECK("nonblocking-unregister", !package->XPackageUnregisterPackageInstalled(b.token, FALSE));
    CHECK("repeated-nonblocking", !package->XPackageUnregisterPackageInstalled(b.token, FALSE));
    HANDLE waiting = CreateThread(nullptr, 0, waiter, &b, 0, nullptr);
    CHECK("wait-blocks", WaitForSingleObject(b.finished, 30) == WAIT_TIMEOUT);
    SetEvent(b.release);
    CHECK("callback-drained", WaitForSingleObject(runner, 3000) == WAIT_OBJECT_0);
    CHECK("waiter-drained", WaitForSingleObject(waiting, 3000) == WAIT_OBJECT_0);
    CHECK("after-drain", package->XPackageUnregisterPackageInstalled(b.token, TRUE));
    CloseHandle(runner);
    CloseHandle(waiting);
    CloseHandle(b.started);
    CloseHandle(b.release);
    CloseHandle(b.finished);
    threading->XTaskQueueCloseHandle(b.queue);
    state self = make();
    self.self = TRUE;
    CHECK("register-self",
          package->XPackageRegisterPackageInstalled3(self.queue, &self, installed, &self.token) == S_OK);
    fixture(6, TRUE);
    CHECK("self-delivery", dispatch(self, 3000) && self.calls == 1);
    CHECK("after-self", package->XPackageUnregisterPackageInstalled(self.token, TRUE));
    threading->XTaskQueueCloseHandle(self.queue);
    package->Release();
    threading->Release();
    SetEnvironmentVariableW(L"XODUS_CONTENT_ROOTS", nullptr);
    printf("CHECKS %ld FAILURES %ld\n", checks, failures);
    return failures ? 1 : 0;
}
