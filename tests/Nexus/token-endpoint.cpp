/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define INITGUID
#include "../../src/private.h"
#include "../../src/System/User/TokenEndpoint.h"
#include <cstdio>
static unsigned checks,failures;
#define CHECK(e) do { ++checks;if(!(e)) { ++failures;printf("FAIL line %u\n",__LINE__); } } while(0)
static void valid(const char *url,const WCHAR *expected) {
 HSTRING value=nullptr;CHECK(token_fallback_party(url,&value)==S_OK && value);
 UINT32 length=0;const WCHAR *raw=WindowsGetStringRawBuffer(value,&length);
 CHECK(length==wcslen(expected) && !wmemcmp(raw,expected,length));WindowsDeleteString(value);
}
static void invalid(const char *url) {
 HSTRING value=(HSTRING)1;CHECK(FAILED(token_fallback_party(url,&value)) && !value);
}
int main() {
 valid("https://service.example.invalid/login?secret=not-an-audience",L"https://service.example.invalid/");
 valid("https://service.example.invalid",L"https://service.example.invalid/");
 valid("http://service.example.invalid/path",L"https://service.example.invalid/");
 valid("https://service.example.invalid:8443/path",L"https://service.example.invalid:8443/");
 valid("wss://rta.xboxlive.com/connect",L"http://xboxlive.com");
 valid("HTTPS://PROFILE.XBOXLIVE.COM/users/query",L"http://xboxlive.com");
 valid("https://xboxlive.com/path",L"http://xboxlive.com");
 valid("https://notxboxlive.com/path",L"https://notxboxlive.com/");
 valid("https://xboxlive.com.example.invalid/path",L"https://xboxlive.com.example.invalid/");
 valid("https://service.example.invalid?secret=x",L"https://service.example.invalid/");
 valid("https://service.example.invalid#fragment",L"https://service.example.invalid/");
 invalid(nullptr);invalid("");invalid("example.invalid");invalid("file://example.invalid");
 invalid("https:///missing-host");invalid("https://name@host.invalid/path");invalid("https://host.invalid\r\nX-header: SECRET/path");
 invalid("https://host.invalid\\injected/path");invalid("https://host.invalid\"/path");invalid("https://\xc0\xaf/path");
 CHECK(token_fallback_party("https://example.invalid",nullptr)==E_POINTER);
 // Owned HSTRING remains valid after caller URL mutation and stack churn.
 char input[]="https://persistent.example.invalid/login";HSTRING value=nullptr;
 CHECK(token_fallback_party(input,&value)==S_OK);memset(input,'x',sizeof(input)-1);
 HSTRING copy=nullptr;CHECK(WindowsDuplicateString(value,&copy)==S_OK);WindowsDeleteString(value);
 UINT32 length;const WCHAR *raw=WindowsGetStringRawBuffer(copy,&length);
 CHECK(length==wcslen(L"https://persistent.example.invalid/")&&!wmemcmp(raw,L"https://persistent.example.invalid/",length));
 WindowsDeleteString(copy);
 printf("CHECKS %u FAILURES %u\n",checks,failures);return failures?1:0;
}
