/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define INITGUID
#include "../../src/private.h"
#include "../../src/HttpClient.h"
#include "../../src/System/User/AuthRejectionMetadata.h"
#include <cstdio>
static unsigned checks,failures;
#define CHECK(e) do { ++checks;if(!(e)) { ++failures;printf("FAIL line %u\n",__LINE__); } } while(0)
static void body(const char *text, BOOL expected, DWORD value) {
 BOOL present=TRUE;DWORD actual=rejection_xerr((const BYTE *)text,strlen(text),&present);
 CHECK(present==expected && actual==value);
}
int main() {
 CHECK(SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED)));
 CHECK(rejection_audience_hash("")==0xcbf29ce484222325ULL);
 CHECK(rejection_audience_hash("a")==0xaf63dc4c8601ec8cULL);
 CHECK(!rejection_audience_hash(nullptr));
 CHECK(rejection_audience_kind("http://xboxlive.com")==1);
 CHECK(rejection_audience_kind("https://example.invalid")==2);
 CHECK(rejection_audience_kind("http://example.invalid")==3);
 CHECK(rejection_audience_kind("rp://example.invalid")==4);
 CHECK(rejection_audience_kind("SECRET")==5);
 body("{\"XErr\":2148916233}",TRUE,2148916233UL);
 body("{\"XErr\":4294967295}",TRUE,MAXDWORD);
 body("{\"XErr\":0}",TRUE,0);
 body("{\"XErr\":-1}",FALSE,0);
 body("{\"XErr\":1.5}",FALSE,0);
 body("{\"XErr\":4294967296}",FALSE,0);
 body("{\"XErr\":\"SECRET\"}",FALSE,0);
 body("{\"Message\":\"SECRET\"}",FALSE,0);
 body("{\"XErr\":",FALSE,0);
 BOOL present=TRUE;
 CHECK(!rejection_xerr(nullptr,0,&present)&&!present);
 BYTE too_large=0;
 CHECK(!rejection_xerr(&too_large,65537,&present)&&!present);
 char path[MAX_PATH],dir[MAX_PATH];GetTempPathA(sizeof(dir),dir);GetTempFileNameA(dir,"rej",0,path);DeleteFileA(path);
 SetEnvironmentVariableA("XUSER_AUTH_REJECTION_LOG",nullptr);
 SetLastError(1234);auth_rejection_metadata("SECRET",400,nullptr,0);
 CHECK(GetLastError()==1234 && GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES);
 SetEnvironmentVariableA("XUSER_AUTH_REJECTION_LOG",path);
 SetLastError(5678);auth_rejection_metadata("SECRET",400,nullptr,0);
 CHECK(GetLastError()==5678 && GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES);
 HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(file!=INVALID_HANDLE_VALUE);CloseHandle(file);
 const char json[]="{\"XErr\":2148916233,\"Message\":\"SECRET_TOKEN\",\"Redirect\":\"https://SECRET.invalid\"}";
 SetLastError(9012);auth_rejection_metadata("https://SECRET.invalid/private?TOKEN=SECRET",400,(const BYTE *)json,strlen(json));
 CHECK(GetLastError()==9012);
 char output[32769];DWORD count=0;
 file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 CHECK(ReadFile(file,output,sizeof(output)-1,&count,nullptr));CloseHandle(file);output[count]=0;
 CHECK(strstr(output,"xerr=2148916233")&&strstr(output,"audience_kind=2"));
 CHECK(!strstr(output,"SECRET")&&!strstr(output,"https")&&!strstr(output,"TOKEN")&&!strstr(output,"private"));
 for(unsigned i=0;i<1000;++i)auth_rejection_metadata("a",400,(const BYTE *)json,strlen(json));
 file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);LARGE_INTEGER length={};
 CHECK(GetFileSizeEx(file,&length)&&length.QuadPart<=32768);CloseHandle(file);
 DeleteFileA(path);SetEnvironmentVariableA("XUSER_AUTH_REJECTION_LOG",nullptr);RoUninitialize();
 printf("CHECKS %u FAILURES %u\n",checks,failures);return failures?1:0;
}
