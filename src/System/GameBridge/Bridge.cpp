// SPDX-License-Identifier: LGPL-2.1-or-later
#include "Bridge.h"
#include <charconv>
#include <cstring>
#include <new>

namespace nexus::bridge
{
static bool Settings( UINT16 &port, char key[65] ) noexcept
{
    char number[8];
    const auto size = GetEnvironmentVariableA( "NEXUS_GAME_BRIDGE_PORT", number, sizeof( number ) );
    if ( !size || size >= sizeof( number ) || GetEnvironmentVariableA( "NEXUS_GAME_BRIDGE_KEY", key, 65 ) != 64 )
        return false;
    unsigned value = 0;
    const auto parsed = std::from_chars( number, number + size, value );
    if ( parsed.ec != std::errc() || parsed.ptr != number + size || !value || value > 65535 ) return false;
    for ( unsigned i = 0; i < 64; ++i )
        if ( !( key[i] >= '0' && key[i] <= '9' ) && !( key[i] >= 'a' && key[i] <= 'f' ) ) return false;
    port = static_cast<UINT16>( value );
    return true;
}
static void Put( std::vector<BYTE> &bytes, UINT32 value )
{
    for ( unsigned i = 0; i < 4; ++i ) bytes.push_back( static_cast<BYTE>( value >> ( i * 8 ) ) );
}
static UINT32 Read( const BYTE *bytes ) noexcept
{
    return bytes[0] | ( UINT32( bytes[1] ) << 8 ) | ( UINT32( bytes[2] ) << 16 ) | ( UINT32( bytes[3] ) << 24 );
}
static bool Transfer( SOCKET socket, BYTE *data, SIZE_T size, bool write ) noexcept
{
    while ( size )
    {
        const int done = write ? send( socket, reinterpret_cast<const char *>( data ), static_cast<int>( size ), 0 )
                               : recv( socket, reinterpret_cast<char *>( data ), static_cast<int>( size ), 0 );
        if ( done <= 0 ) return false;
        data += done;
        size -= done;
    }
    return true;
}
// One connection per exchange: key, "NXSI", length, then a list of UTF-8
// strings. The reply carries the HRESULT before the same list encoding.
class LoopbackTransport final : public Transport
{
public:
    HRESULT Exchange( const std::vector<std::string> &request, std::vector<std::string> &reply ) noexcept override
    {
        reply.clear();
        UINT16 port;
        char key[65];
        if ( !Settings( port, key ) ) return E_NOTIMPL;
        WSADATA data;
        if ( WSAStartup( MAKEWORD( 2, 2 ), &data ) ) return E_FAIL;
        const SOCKET connection = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        HRESULT hr = E_FAIL;
        try
        {
            if ( connection != INVALID_SOCKET && request.size() <= 16 )
            {
                DWORD timeout = 5000;
                setsockopt( connection, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>( &timeout ),
                            sizeof( timeout ) );
                setsockopt( connection, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>( &timeout ),
                            sizeof( timeout ) );
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
                address.sin_port = htons( port );
                std::vector<BYTE> body;
                Put( body, static_cast<UINT32>( request.size() ) );
                for ( const auto &value : request )
                {
                    if ( value.size() > 8192 || value.find( '\0' ) != std::string::npos ) throw E_INVALIDARG;
                    Put( body, static_cast<UINT32>( value.size() ) );
                    body.insert( body.end(), value.begin(), value.end() );
                }
                if ( body.size() > 32768 ) throw E_INVALIDARG;
                std::vector<BYTE> bytes( key, key + 64 );
                bytes.insert( bytes.end(), { 'N', 'X', 'S', 'I' } );
                Put( bytes, static_cast<UINT32>( body.size() ) );
                bytes.insert( bytes.end(), body.begin(), body.end() );
                BYTE header[8];
                if ( !connect( connection, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) &&
                     Transfer( connection, bytes.data(), bytes.size(), true ) &&
                     Transfer( connection, header, sizeof( header ), false ) )
                {
                    const auto size = Read( header );
                    if ( size < 8 || size > 32772 ) throw E_INVALIDARG;
                    body.resize( size - 4 );
                    if ( !Transfer( connection, body.data(), body.size(), false ) ) throw E_FAIL;
                    SIZE_T offset = 4;
                    const auto count = Read( body.data() );
                    if ( count > 16 ) throw E_INVALIDARG;
                    for ( unsigned i = 0; i < count; ++i )
                    {
                        if ( offset + 4 > body.size() ) throw E_INVALIDARG;
                        const auto length = Read( body.data() + offset );
                        offset += 4;
                        if ( length > 8192 || offset + length > body.size() ) throw E_INVALIDARG;
                        std::string value( reinterpret_cast<const char *>( body.data() + offset ), length );
                        offset += length;
                        if ( value.find( '\0' ) != std::string::npos ||
                             ( length &&
                               !MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, nullptr, 0 ) ) )
                            throw E_INVALIDARG;
                        reply.push_back( std::move( value ) );
                    }
                    if ( offset != body.size() ) throw E_INVALIDARG;
                    hr = static_cast<HRESULT>( Read( header + 4 ) );
                }
            }
        }
        catch ( HRESULT error )
        {
            hr = error;
        }
        catch ( ... )
        {
            hr = E_OUTOFMEMORY;
        }
        if ( connection != INVALID_SOCKET ) closesocket( connection );
        WSACleanup();
        SecureZeroMemory( key, sizeof( key ) );
        if ( FAILED( hr ) ) reply.clear();
        return hr;
    }
};
Transport *Get() noexcept
{
    static LoopbackTransport value;
    UINT16 port;
    char key[65];
    const bool available = Settings( port, key );
    SecureZeroMemory( key, sizeof( key ) );
    return available ? &value : nullptr;
}
}
