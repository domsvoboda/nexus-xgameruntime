/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Optional bounded numeric rejection metadata. Never records an audience,
 * request URL, response text, account identifier, token or signature.
 */
#pragma once
#include <cmath>
static UINT64 rejection_audience_hash(const char *party)
{
    UINT64 hash = 14695981039346656037ULL;
    if (!party) return 0;
    for (size_t i = 0; i < 4096 && party[i]; ++i)
        hash = (hash ^ (unsigned char)party[i]) * 1099511628211ULL;
    return hash;
}
static unsigned rejection_audience_kind(const char *party)
{
    if (!party) return 0;
    if (!strcmp(party, "http://xboxlive.com")) return 1;
    if (!strncmp(party, "https://", 8)) return 2;
    if (!strncmp(party, "http://", 7)) return 3;
    if (!strncmp(party, "rp://", 5)) return 4;
    return 5;
}
static DWORD rejection_xerr(const BYTE *body, SIZE_T size, BOOL *present)
{
    IJsonObject *object = nullptr;
    IJsonValue *value = nullptr;
    IJsonValueStatics *factory = nullptr;
    HSTRING name = nullptr, json = nullptr, key = nullptr;
    DOUBLE number;
    DWORD result = 0;
    HRESULT com;
    int count;
    WCHAR *wide;
    *present = FALSE;
    if (!body || !size || size > 65536) return 0;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)body, size, nullptr, 0);
    if (!count || !(wide = (WCHAR *)malloc(count * sizeof(WCHAR)))) return 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)body, size, wide, count) != count)
    { free(wide); return 0; }
    com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE)
    {
        if (SUCCEEDED(WindowsCreateString(RuntimeClass_Windows_Data_Json_JsonValue,
                wcslen(RuntimeClass_Windows_Data_Json_JsonValue), &name)) &&
            SUCCEEDED(WindowsCreateString(wide, count, &json)) &&
            SUCCEEDED(WindowsCreateString(L"XErr", 4, &key)) &&
            SUCCEEDED(RoGetActivationFactory(name, __uuidof(IJsonValueStatics), (void **)&factory)) &&
            SUCCEEDED(factory->Parse(json, &value)) && SUCCEEDED(value->GetObject(&object)) &&
            SUCCEEDED(object->GetNamedNumber(key, &number)) && std::isfinite(number) &&
            number >= 0 && number <= MAXDWORD && std::floor(number) == number)
        {
            result = (DWORD)number;
            *present = TRUE;
        }
    }
    free(wide);
    if (object) object->Release();
    if (value) value->Release();
    if (factory) factory->Release();
    WindowsDeleteString(name); WindowsDeleteString(json); WindowsDeleteString(key);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
static void auth_rejection_metadata(const char *party, DWORD status, const BYTE *body, SIZE_T size)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    DWORD saved = GetLastError(), length, written;
    char path[MAX_PATH], line[256];
    HANDLE file;
    LARGE_INTEGER bytes, end;
    BOOL present;
    DWORD xerr;
    length = GetEnvironmentVariableA("XUSER_AUTH_REJECTION_LOG", path, sizeof(path));
    if (!length || length >= sizeof(path)) { SetLastError(saved); return; }
    xerr = rejection_xerr(body, size, &present);
    length = snprintf(line, sizeof(line), "AUTH_REJECT http=%lu xerr_present=%u xerr=%lu audience_kind=%u audience_hash=%016llx thread=%lu\n",
        (unsigned long)status, !!present, (unsigned long)xerr, rejection_audience_kind(party),
        (unsigned long long)rejection_audience_hash(party), (unsigned long)GetCurrentThreadId());
    AcquireSRWLockExclusive(&lock);
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        if (GetFileType(file) == FILE_TYPE_DISK && GetFileSizeEx(file, &bytes) && bytes.QuadPart + length <= 32768)
        {
            end.QuadPart = 0;
            if (SetFilePointerEx(file, end, nullptr, FILE_END)) WriteFile(file, line, length, &written, nullptr);
        }
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
    SetLastError(saved);
}
