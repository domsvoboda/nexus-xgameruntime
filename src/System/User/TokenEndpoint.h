/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Only used when the service endpoint table has no matching policy. Custom
 * relying-party names end in '/', unlike the well-known Xbox service audience.
 * https://learn.microsoft.com/gaming/gdk/docs/services/fundamentals/s2s-auth-calls/s2s-calls/live-title-service-calls-xbox-live
 */
#pragma once
static HRESULT token_fallback_party(const char *url, HSTRING *out)
{
    const char *host, *end, *separator;
    size_t length, host_length;
    char *party;
    WCHAR *wide;
    int count;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!url || !(separator = strstr(url, "://"))) return E_INVALIDARG;
    length = separator - url;
    if (!((length == 4 && !strncasecmp(url, "http", 4)) ||
          (length == 5 && !strncasecmp(url, "https", 5)) ||
          (length == 2 && !strncasecmp(url, "ws", 2)) ||
          (length == 3 && !strncasecmp(url, "wss", 3)))) return E_INVALIDARG;
    host = separator + 3;
    end = host + strcspn(host, "/?#");
    host_length = end - host;
    if (!host_length || host_length > 4096) return E_INVALIDARG;
    for (size_t i = 0; i < host_length; ++i)
        if ((unsigned char)host[i] <= 0x20 || host[i] == '@' || host[i] == '"' || host[i] == '\\')
            return E_INVALIDARG;
    if ((host_length == 12 && !strncasecmp(host, "xboxlive.com", 12)) ||
        (host_length >= 13 && !strncasecmp(host + host_length - 13, ".xboxlive.com", 13)))
        return WindowsCreateString(L"http://xboxlive.com", wcslen(L"http://xboxlive.com"), out);
    // Keep the requested authority in the HTTPS fallback audience.
    if (!(party = (char *)calloc(8 + host_length + 2, 1))) return E_OUTOFMEMORY;
    memcpy(party, "https://", 8);
    memcpy(party + 8, host, host_length);
    party[8 + host_length] = '/';
    length = 8 + host_length + 1;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, party, length, nullptr, 0);
    if (!count) { free(party); return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION); }
    if (!(wide = (WCHAR *)malloc(count * sizeof(*wide)))) { free(party); return E_OUTOFMEMORY; }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, party, length, wide, count) != count)
        hr = HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    else hr = WindowsCreateString(wide, count, out);
    free(wide); free(party);
    return hr;
}
