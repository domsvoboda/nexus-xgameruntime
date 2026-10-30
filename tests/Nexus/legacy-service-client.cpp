/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Account-free configuration parsing and real sibling file selection.
 */
#define INITGUID
#include "../../src/private.h"
#include "../../src/Runtime/Configuration.h"
#include "../../src/Runtime/LegacyServiceClient.h"
#include <cstdio>
static unsigned checks, failures;
#define CHECK(e) do { ++checks; if (!(e)) { ++failures; printf("FAIL line %u\n",__LINE__); } } while(0)
static void parse(const char *json, bool valid, HRESULT expected = E_INVALIDARG)
{
    std::string client="previous";
    HRESULT hr=nexus::ParseLegacyServiceClient(json,strlen(json),0x1234,client);
    CHECK(valid ? hr==S_OK : expected==S_FALSE ? hr==S_FALSE : FAILED(hr));
    CHECK(valid ? client=="000000001234ABcd" : client.empty());
}
int main()
{
    parse("{\"TitleId\":4660,\"ClientId\":\"000000001234ABcd\"}",true);
    parse("{\"TitleId\":\"4660\",\"ClientId\":\"000000001234ABcd\"}",true);
    parse("{\"TitleId\":\"0x1234\",\"ClientId\":\"000000001234ABcd\"}",true);
    parse("{\"TitleId\":\"0X1234\",\"ClientId\":\"000000001234ABcd\",\"Other\":[true,{},null]}",true);
    for(auto text : {"", "[]", "{", "null", "{\"ClientId\":17}",
        "{\"TitleId\":4661,\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":4660.5,\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":-4660,\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":4294967296,\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":true,\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":\"0x1234suffix\",\"ClientId\":\"000000001234ABcd\"}",
        "{\"TitleId\":4660,\"ClientId\":\"0000000000000000\"}",
        "{\"TitleId\":4660,\"ClientId\":\"000000001234ABCg\"}",
        "{\"TitleId\":4660,\"ClientId\":\"000000001234ABC\"}",
        "{\"TitleId\":4660,\"ClientId\":\"000000001234ABcd0\"}",
        "{\"TitleId\":4660,\"ClientId\":\"000000001234AB\\u0000\"}",
        "{\"TitleId\":4660,\"ClientId\":\"000000001234AB\xff\xff\"}"}) parse(text,false);
    parse("{\"TitleId\":4660}",false,S_FALSE);
    std::string client="previous",large(65537,' ');
    CHECK(FAILED(nexus::ParseLegacyServiceClient(nullptr,1,0x1234,client)) && client.empty());
    CHECK(FAILED(nexus::ParseLegacyServiceClient(large.data(),large.size(),0x1234,client)));
    const char *good="{\"TitleId\":4660,\"ClientId\":\"000000001234ABcd\"}";
    CHECK(FAILED(nexus::ParseLegacyServiceClient(good,strlen(good),0,client)));
    WCHAR temp[MAX_PATH],directory[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp)>0 && GetTempFileNameW(temp,L"xsc",0,directory)>0);
    CHECK(DeleteFileW(directory) && CreateDirectoryW(directory,nullptr));
    nexus::GameConfiguration config;
    config.game.attributes={{"configVersion","0"}};config.titleId=0x1234;config.directory=directory;
    CHECK(nexus::LoadLegacyServiceClient(config)==S_OK && config.msaAppId.empty());
    auto path=config.directory+L"\\xboxservices.config";
    auto write=[&](const char *data){
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD written=0;CHECK(file!=INVALID_HANDLE_VALUE);
        CHECK(WriteFile(file,data,strlen(data),&written,nullptr) && written==strlen(data));CloseHandle(file);
    };
    write(good);
    CHECK(nexus::LoadLegacyServiceClient(config)==S_OK && config.msaAppId=="000000001234ABcd");
    config.msaAppId.clear();config.titleId=0x5678;
    CHECK(FAILED(nexus::LoadLegacyServiceClient(config)) && config.msaAppId.empty());
    config.titleId=0x1234;write("malformed");config.msaAppId="explicit-app";
    CHECK(nexus::LoadLegacyServiceClient(config)==S_OK && config.msaAppId=="explicit-app");
    config.msaAppId.clear();config.game.attributes={{"configVersion","1"}};
    CHECK(nexus::LoadLegacyServiceClient(config)==S_OK && config.msaAppId.empty());
    config.game.attributes={{"configVersion","0"}};
    CHECK(FAILED(nexus::LoadLegacyServiceClient(config)) && config.msaAppId.empty());
    write("{\"TitleId\":4660}");
    CHECK(nexus::LoadLegacyServiceClient(config)==S_OK && config.msaAppId.empty());
    write(good);
    auto manifest=config.directory+L"\\MicrosoftGame.config";
    const char *xml="<Game configVersion='0'><TitleId>1234</TitleId></Game>";
    HANDLE file=CreateFileW(manifest.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written;
    CHECK(file!=INVALID_HANDLE_VALUE && WriteFile(file,xml,strlen(xml),&written,nullptr) && written==strlen(xml));
    CloseHandle(file);
    std::string manifestUtf8;
    CHECK(nexus::ToUtf8(manifest,manifestUtf8)==S_OK);
    INITIALIZE_OPTIONS options{1,FALSE,manifestUtf8.c_str()};
    CHECK(nexus::GetGameConfiguration()==nullptr);
    CHECK(nexus::InitializeGameConfiguration(&options)==S_OK);
    const auto *published=nexus::GetGameConfiguration();
    CHECK(published && published->titleId==0x1234 && published->msaAppId=="000000001234ABcd");
    CHECK(DeleteFileW(manifest.c_str()) && DeleteFileW(path.c_str()) && RemoveDirectoryW(directory));
    printf("CHECKS %u FAILURES %u\n",checks,failures);return failures?1:0;
}
