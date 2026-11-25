/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameUI
 *
 * Copyright 2026 Olivia Ryan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "XGameUi.h"
#include "GameInvite/Events.h"

HRESULT WINAPI XGameUiImpl::XGameUiShowMessageDialogAsync( XAsyncBlock *async, const char *titleText,
                                                           const char *contentText, const char *firstButtonText,
                                                           const char *secondButtonText, const char *thirdButtonText,
                                                           XGameUiMessageDialogButton defaultButton,
                                                           XGameUiMessageDialogButton cancelButton )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowMessageDialogResult( XAsyncBlock *async,
                                                            XGameUiMessageDialogButton *resultButton )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowSendGameInviteAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                            const char *sessionConfigurationId,
                                                            const char *sessionTemplateName, const char *sessionId,
                                                            const char *invitationText,
                                                            const char *customActivationContext )
{
    ( void )0;
    return nexus::invites::BridgeAvailable() ? nexus::invites::ShowInvite( async, requestingUser, sessionConfigurationId, sessionTemplateName, sessionId, invitationText, customActivationContext ) : E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowSendGameInviteResult( XAsyncBlock *async )
{
    ( void )0;
    return nexus::invites::BridgeAvailable() ? nexus::invites::ShowInviteResult( async ) : E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowPlayerProfileCardAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                               UINT64 targetPlayer )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowPlayerProfileCardResult( XAsyncBlock *async )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowAchievementsAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                          UINT32 titleId )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowAchievementsResult( XAsyncBlock *async )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowPlayerPickerAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                          const char *promptText, UINT32 selectFromPlayersCount,
                                                          const UINT64 *selectFromPlayers,
                                                          UINT32 preSelectedPlayersCount, UINT64 *preSelectedPlayers,
                                                          UINT32 minSelectionCount, UINT32 maxSelectionCount )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowPlayerPickerResultCount( XAsyncBlock *async, UINT32 *resultPlayersCount )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowPlayerPickerResult( XAsyncBlock *async, UINT32 resultPlayersCount,
                                                           UINT64 *resultPlayers, UINT32 *resultPlayersUsed )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowErrorDialogAsync( XAsyncBlock *async, HRESULT errorCode, const char *context )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowErrorDialogResult( XAsyncBlock *async )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetNotificationPositionHint( XGameUiNotificationPositionHint position )
{
    ( void )0;
    return E_NOTIMPL;
}

// SPDX-License-Identifier: LGPL-2.1-or-later
// Native desktop fallback for the GDK text-entry contract.
#include <atomic>
#include <bcrypt.h>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct TextEntryContext
{
    std::wstring title, description, initial;
    std::string result;
    UINT32 limit;
    bool password, numeric;
    std::atomic<bool> cancelled{ false };
    SRWLOCK processMutex = SRWLOCK_INIT;
    HANDLE process = nullptr;
    SOCKET connection = INVALID_SOCKET;
};
class TextEntryLock
{
    SRWLOCK *lock;
public:
    explicit TextEntryLock( SRWLOCK *value ) : lock( value ) { AcquireSRWLockExclusive( lock ); }
    ~TextEntryLock() { ReleaseSRWLockExclusive( lock ); }
    TextEntryLock( const TextEntryLock & ) = delete;
    TextEntryLock &operator=( const TextEntryLock & ) = delete;
};
static const char text_entry_identity{};
static bool text_entry_wide( const char *input, std::wstring &output )
{
    if ( !input )
    {
        output.clear();
        return true;
    }
    int n = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, nullptr, 0 );
    if ( !n )
        return false;
    std::vector<WCHAR> buffer( n );
    if ( !MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, buffer.data(), n ) )
        return false;
    output.assign( buffer.data(), n - 1 );
    return true;
}

#include "TextEntryProcess.inc"
static HRESULT WINAPI text_entry_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    auto *ctx = static_cast<TextEntryContext *>( data->context );
    IXThreadingImpl *threading = nullptr;
    HRESULT hr =
        QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &threading ) );
    if ( FAILED( hr ) )
        return hr;
    switch ( op )
    {
    case XAsyncOp::Begin:
        hr = threading->XAsyncSchedule( data->async, 0 );
        break;
    case XAsyncOp::DoWork: {
        HRESULT dialogStatus = HRESULT_FROM_WIN32( ERROR_CANCELLED );
        try
        {
            if ( !ctx->cancelled.load() )
                dialogStatus = text_entry_process( ctx );
        }
        catch ( ... )
        {
            dialogStatus = E_OUTOFMEMORY;
        }
        HRESULT status = ctx->cancelled.load() ? HRESULT_FROM_WIN32( ERROR_CANCELLED ) : dialogStatus;
        fprintf( stderr, "[nexus-text-entry] dialog completed hr=%08lx\n",
                 static_cast<unsigned long>( status ) );
        SIZE_T size = SUCCEEDED( status ) ? ctx->result.size() + 1 : 0;
        // Completion can synchronously consume and free context. Do not touch it afterward.
        threading->XAsyncComplete( data->async, status, size );
        hr = S_OK;
        break;
    }
    case XAsyncOp::GetResult:
        if ( !data->buffer || data->bufferSize < ctx->result.size() + 1 )
            hr = HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
        else
        {
            memcpy( data->buffer, ctx->result.c_str(), ctx->result.size() + 1 );
            hr = S_OK;
        }
        break;
    case XAsyncOp::Cancel: {
        ctx->cancelled.store( true );
        {
            TextEntryLock lock( &ctx->processMutex );
            if ( ctx->connection != INVALID_SOCKET )
                shutdown( ctx->connection, SD_BOTH );
            if ( ctx->process )
                TerminateProcess( ctx->process, ERROR_CANCELLED );
        }
        hr = S_OK;
        break;
    }
    case XAsyncOp::Cleanup:
        delete ctx;
        hr = S_OK;
        break;
    default:
        hr = E_NOTIMPL;
    }
    threading->Release();
    return hr;
}
} // namespace

HRESULT WINAPI XGameUiImpl::XGameUiShowTextEntryAsync( XAsyncBlock *async, const char *titleText,
                                                       const char *descriptionText, const char *defaultText,
                                                       XGameUiTextEntryInputScope inputScope,
                                                       UINT32 maxTextLength )
{
    if ( !async )
        return E_INVALIDARG;
    fprintf( stderr, "[nexus-text-entry] opening native text dialog scope=%u maxLength=%u\n",
             static_cast<unsigned>( inputScope ), maxTextLength );
    if ( static_cast<unsigned>( inputScope ) != 31 && maxTextLength > 2097152 )
        return E_INVALIDARG;
    TextEntryContext *ctx = nullptr;
    try
    {
        ctx = new TextEntryContext;
        ctx->password = static_cast<unsigned>( inputScope ) == 31;
        ctx->numeric = static_cast<unsigned>( inputScope ) == 29;
        ctx->limit = ( !maxTextLength || ctx->password ) ? 1024 : maxTextLength;
        if ( !text_entry_wide( titleText, ctx->title ) ||
             !text_entry_wide( descriptionText, ctx->description ) ||
             !text_entry_wide( defaultText, ctx->initial ) )
        {
            delete ctx;
            return E_INVALIDARG;
        }
    }
    catch ( ... )
    {
        delete ctx;
        return E_OUTOFMEMORY;
    }
    IXThreadingImpl *threading = nullptr;
    HRESULT hr =
        QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &threading ) );
    if ( FAILED( hr ) )
    {
        delete ctx;
        return hr;
    }
    hr = threading->XAsyncBegin( async, ctx, &text_entry_identity, "XGameUiShowTextEntryAsync",
                                 text_entry_provider );
    threading->Release();
    if ( FAILED( hr ) )
        delete ctx;
    return hr;
}
HRESULT WINAPI XGameUiImpl::XGameUiShowTextEntryResultSize( XAsyncBlock *async, UINT32 *size )
{
    if ( !async || !size )
        return E_INVALIDARG;
    *size = 0;
    IXThreadingImpl *threading = nullptr;
    HRESULT hr =
        QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &threading ) );
    if ( FAILED( hr ) )
        return hr;
    SIZE_T bytes = 0;
    hr = threading->XAsyncGetResultSize( async, &bytes );
    threading->Release();
    if ( SUCCEEDED( hr ) )
    {
        if ( bytes > UINT_MAX )
            return E_UNEXPECTED;
        *size = static_cast<UINT32>( bytes );
    }
    return hr;
}
HRESULT WINAPI XGameUiImpl::XGameUiShowTextEntryResult( XAsyncBlock *async, UINT32 size, char *buffer,
                                                        UINT32 *used )
{
    if ( !async || !buffer )
        return E_INVALIDARG;
    if ( used )
        *used = 0;
    IXThreadingImpl *threading = nullptr;
    HRESULT hr =
        QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &threading ) );
    if ( FAILED( hr ) )
        return hr;
    SIZE_T actual = 0;
    hr = threading->XAsyncGetResult( async, &text_entry_identity, size, buffer, &actual );
    threading->Release();
    if ( SUCCEEDED( hr ) && used )
        *used = static_cast<UINT32>( actual );
    return hr;
}

HRESULT WINAPI XGameUiImpl::__PADDING__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_2__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_3__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_4__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowWebAuthenticationAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                               const char *requestUri, const char *completionUri )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowWebAuthenticationResultSize( XAsyncBlock *async, SIZE_T *bufferSize )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowWebAuthenticationResult( XAsyncBlock *async, SIZE_T bufferSize, void *buffer,
                                                                XGameUiWebAuthenticationResultData **ptrToBuffer,
                                                                SIZE_T *bufferUsed )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowWebAuthenticationWithOptionsAsync( XAsyncBlock *async,
                                                                          XUserHandle requestingUser,
                                                                          const char *requestUri,
                                                                          const char *completionUri,
                                                                          XGameUiWebAuthenticationOptions options )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_5__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_6__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowMultiplayerActivityGameInviteAsync( XAsyncBlock *async,
                                                                           XUserHandle requestingUser )
{
    ( void )0;
    return nexus::invites::BridgeAvailable() ? nexus::invites::ShowActivityInvite( async, requestingUser ) : E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowMultiplayerActivityGameInviteResult( XAsyncBlock *async )
{
    ( void )0;
    return nexus::invites::BridgeAvailable() ? nexus::invites::ShowActivityInviteResult( async ) : E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_7__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::__PADDING_8__()
{
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryOpen( const XGameUiTextEntryOptions *options, UINT32 maxLength,
                                                  const char *initialText, UINT32 initialCursorIndex,
                                                  XGameUiTextEntryHandle *handle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryClose( XGameUiTextEntryHandle handle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryGetState( XGameUiTextEntryHandle handle,
                                                      XGameUiTextEntryChangeTypeFlags *changeType, UINT32 *cursorIndex,
                                                      UINT32 *imeClauseStartIndex, UINT32 *imeClauseEndIndex,
                                                      UINT32 bufferSize, char *buffer )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryGetExtents( XGameUiTextEntryHandle handle,
                                                        XGameUiTextEntryExtents *extents )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryUpdatePositionHint( XGameUiTextEntryHandle handle,
                                                                XGameUiTextEntryPositionHint positionHint )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiTextEntryUpdateVisibility( XGameUiTextEntryHandle handle,
                                                              XGameUiTextEntryVisibilityFlags visibilityFlags )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowStateShareAsync( XAsyncBlock *async, XUserHandle requestingUser,
                                                        const char *linkToken )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiShowStateShareResult( XAsyncBlock *async )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetUiCallbacks( const XGameUiUiCallbacks *callbacks, BOOLEAN useSystemUiIfAvailable )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetMessageDialogUiResponse( XGameUiCallbackHandle callbackHandle,
                                                               XGameUiMessageDialogButton response )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetPlayerPickerUiResponse( XGameUiCallbackHandle callbackHandle, UINT32 playerCount,
                                                              const UINT64 *players )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetTextEntryUiResponse( XGameUiCallbackHandle callbackHandle, const char *response )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetPlayerProfileCardUiResponse( XGameUiCallbackHandle callbackHandle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetSendGameInviteUiResponse( XGameUiCallbackHandle callbackHandle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetAchievementsUiResponse( XGameUiCallbackHandle callbackHandle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetMultiplayerActivityGameInviteUiResponse( XGameUiCallbackHandle callbackHandle )
{
    ( void )0;
    return E_NOTIMPL;
}

HRESULT WINAPI XGameUiImpl::XGameUiSetErrorDialogUiResponse( XGameUiCallbackHandle callbackHandle )
{
    ( void )0;
    return E_NOTIMPL;
}

IUnknown *GetGameUiInterface() noexcept
{
    static XGameUiImpl value;
    return &value;
}
