// SPDX-License-Identifier: LGPL-2.1-or-later
#define __WINESRC__
#include <atomic>
#include <cstdio>
#include <initguid.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <xasyncprovider.h>
#include <xgameui.h>
#include <xtaskqueue.h>
using Query = HRESULT( WINAPI * )( REFCLSID, REFIID, void ** );
int main()
{
    HMODULE mod = LoadLibraryW( L"xgameruntime.dll" );
    if ( !mod )
    {
        printf( "load error %lu\n", GetLastError() );
        return 2;
    }
    auto query = reinterpret_cast<Query>( GetProcAddress( mod, "QueryApiImpl" ) );
    IXGameUiImpl *ui = nullptr;
    IXThreadingImpl *t = nullptr;
    if ( !query || FAILED( query( CLSID_XGameUiImpl, IID_IXGameUiImpl, reinterpret_cast<void **>( &ui ) ) ) ||
         FAILED( query( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &t ) ) ) )
        return 3;
    HRESULT invalid = ui->XGameUiShowTextEntryAsync( nullptr, nullptr, nullptr, nullptr,
                                                     static_cast<XGameUiTextEntryInputScope>( 0 ), 0 );
    printf( "invalid result %08lx\n", invalid );
    fflush( stdout );
    if ( invalid != E_INVALIDARG )
        return 4;

    for ( int scenario = 0; scenario < 11; ++scenario )
    {
        XAsyncBlock async{};
        XTaskQueueHandle queue = nullptr;
        if ( FAILED( t->XTaskQueueCreate( XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool,
                                          &queue ) ) )
            return 5;
        async.queue = queue;
        std::string title = "Case:" + std::to_string( scenario );
        unsigned scope = scenario == 5 ? 29 : scenario == 10 ? 31 : 0;
        unsigned limit = scenario == 3 || scenario == 10  ? 3
                         : scenario == 5                  ? 9
                         : scenario == 6                  ? 0
                         : scenario == 7                  ? 1
                         : scenario == 8 || scenario == 9 ? 10
                                                          : 100;
        HRESULT hr =
            ui->XGameUiShowTextEntryAsync( &async, title.c_str(), scenario == 8 ? "Enter Share Code." : "General text",
                                           "", static_cast<XGameUiTextEntryInputScope>( scope ), limit );
        if ( FAILED( hr ) )
            return 6;
        std::thread canceller;
        if ( scenario == 2 )
            canceller = std::thread( [&] {
                Sleep( 1500 );
                t->XAsyncCancel( &async );
            } );
        hr = t->XAsyncGetStatus( &async, TRUE );
        if ( canceller.joinable() )
            canceller.join();
        if ( scenario == 1 || scenario == 2 )
        {
            if ( hr != HRESULT_FROM_WIN32( ERROR_CANCELLED ) )
            {
                printf( "bad cancel %08lx\n", hr );
                return 7;
            }
        }
        else
        {
            if ( FAILED( hr ) )
            {
                printf( "scenario %d status %08lx\n", scenario, hr );
                return 8;
            }
            UINT32 size = 0;
            if ( FAILED( ui->XGameUiShowTextEntryResultSize( &async, &size ) ) )
                return 9;
            std::vector<char> value( size );
            char small[1];
            if ( size > 1 && ui->XGameUiShowTextEntryResult( &async, 1, small, nullptr ) !=
                                 HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ) )
                return 10;
            UINT32 used = 0;
            if ( FAILED( ui->XGameUiShowTextEntryResult( &async, size, value.data(), &used ) ) )
                return 11;
            std::string expected = scenario == 0   ? "caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8f\x8e"
                                   : scenario == 3 ? "abc"
                                   : scenario == 4 ? ""
                                   : scenario == 5 ? "123456789"
                                   : scenario == 6 ? std::string( 1024, 'x' )
                                   : scenario == 7 ? "\xf0\x9f\x8f\x8e"
                                   : scenario == 8 ? "ab12"
                                   : scenario == 9 ? "ab12"
                                                   : "abcde";
            if ( value.data() != expected || used != expected.size() + 1 )
            {
                printf( "scenario %d result mismatch\n", scenario );
                return 12;
            }
        }
        t->XTaskQueueCloseHandle( queue );
        printf( "PASS native scenario %d\n", scenario );
        fflush( stdout );
    }
    ui->Release();
    t->Release();
    puts( "ALL NATIVE TESTS PASSED" );
    return 0;
}
