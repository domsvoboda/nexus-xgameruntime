// SPDX-License-Identifier: LGPL-2.1-or-later
// Isolated Wine bridge: a private Windows pipe becomes a child-only Unix env.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

int main( int argc, char **argv )
{
    if ( argc != 3 || strcmp( argv[1], "--port" ) )
        return 2;
    char *end = nullptr;
    unsigned long port = strtoul( argv[2], &end, 10 );
    if ( !*argv[2] || *end || !port || port > 65535 )
        return 2;
    unsigned char token[32];
    DWORD count = 0, total = 0;
    while ( total < sizeof( token ) )
    {
        if ( !ReadFile( GetStdHandle( STD_INPUT_HANDLE ), token + total, sizeof( token ) - total, &count, nullptr ) ||
             !count )
            return 3;
        total += count;
    }
    CloseHandle( GetStdHandle( STD_INPUT_HANDLE ) );
    char encoded[65];
    const char *hex = "0123456789abcdef";
    for ( unsigned i = 0; i < sizeof( token ); ++i )
    {
        encoded[2 * i] = hex[token[i] >> 4];
        encoded[2 * i + 1] = hex[token[i] & 15];
    }
    encoded[64] = 0;
    SecureZeroMemory( token, sizeof( token ) );
    using UnixPath = char *(CDECL *)( LPCWSTR );
    using SetUnixEnv = LONG( WINAPI * )( const char *, const char * );
    using SpawnUnix = LONG( WINAPI * )( char *const *, int );
    auto path =
        reinterpret_cast<UnixPath>( GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "wine_get_unix_file_name" ) );
    auto setenv =
        reinterpret_cast<SetUnixEnv>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "__wine_set_unix_env" ) );
    auto spawn =
        reinterpret_cast<SpawnUnix>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "__wine_unix_spawnvp" ) );
    if ( !path || !setenv || !spawn )
        return 4;
    WCHAR module[32768];
    DWORD length = GetModuleFileNameW( nullptr, module, 32768 );
    if ( !length || length >= 32768 )
        return 4;
    WCHAR *slash = wcsrchr( module, L'\\' );
    if ( !slash )
        return 4;
    *slash = 0;
    std::wstring helper = std::wstring( module ) + L"\\nexus-text-entry.py";
    char *host = path( helper.c_str() );
    if ( !host )
        return 4;
    if ( setenv( "NEXUS_NATIVE_TEXT_TOKEN", encoded ) < 0 )
    {
        HeapFree( GetProcessHeap(), 0, host );
        return 4;
    }
    SecureZeroMemory( encoded, sizeof( encoded ) );
    char python[] = "/usr/bin/python3", option[] = "--port";
    char *arguments[] = { python, host, option, argv[2], nullptr };
    LONG result = spawn( arguments, TRUE );
    setenv( "NEXUS_NATIVE_TEXT_TOKEN", nullptr );
    HeapFree( GetProcessHeap(), 0, host );
    return result ? 5 : 0;
}
