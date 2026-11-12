/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Optional typed collection metadata returned beside a genuine signed receipt.
 * This is never ownership proof: callers apply it only after receipt/nonce
 * verification and an exact product/SKU join against the verified inventory. */
#pragma once
#include <windows.h>
#include <libxml/tree.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STORE_COLLECTION_MAX_ITEMS 256

struct store_collection_item
{
    char product[13];
    char sku[5];
    INT64 acquired, start, end;
    BOOL has_acquired, has_start, has_end;
};

struct store_collection_metadata
{
    UINT32 count;
    struct store_collection_item items[STORE_COLLECTION_MAX_ITEMS];
};

static BOOL store_collection_alnum( const char *text, SIZE_T length )
{
    SIZE_T i;
    if ( !text || !length ) return FALSE;
    for ( i = 0; i < length; i++ )
        if ( !( ( text[i] >= '0' && text[i] <= '9' ) || ( text[i] >= 'A' && text[i] <= 'Z' ) ||
                ( text[i] >= 'a' && text[i] <= 'z' ) ) )
            return FALSE;
    return TRUE;
}

static char *store_collection_text( xmlNodePtr node, const char *name )
{
    xmlNodePtr child;
    xmlChar *content;
    char *value;

    for ( child = node ? node->children : NULL; child; child = child->next )
    {
        if ( child->type != XML_ELEMENT_NODE || strcmp( ( const char * )child->name, name ) ) continue;
        if ( !( content = xmlNodeGetContent( child ) ) ) return NULL;
        value = strdup( ( const char * )content );
        xmlFree( content );
        return value;
    }
    return NULL;
}

/* Positive decimal INT64 only. The ABI is UTC seconds since the epoch. */
static BOOL store_collection_int64( const char *text, INT64 *out )
{
    char *end;
    long long value;
    if ( !text || !*text || *text == '+' || *text == '-' ) return FALSE;
    errno = 0;
    value = strtoll( text, &end, 10 );
    if ( errno || *end || value <= 0 ) return FALSE;
    *out = ( INT64 )value;
    return TRUE;
}

/* Parse the optional <CollectionMetadata><Item>... adjunct. Absent metadata is
 * valid and leaves count zero. Present but malformed/duplicate/oversized data
 * fails closed so callers never publish partially normalized values. */
static HRESULT store_collection_parse( xmlNodePtr root, struct store_collection_metadata *out )
{
    xmlNodePtr node, item;
    UINT32 i;

    if ( !out ) return E_INVALIDARG;
    out->count = 0;
    for ( node = root ? root->children : NULL; node; node = node->next )
    {
        if ( node->type != XML_ELEMENT_NODE || strcmp( ( const char * )node->name, "CollectionMetadata" ) )
            continue;
        for ( item = node->children; item; item = item->next )
        {
            struct store_collection_item parsed = {};
            char *product = NULL, *sku = NULL, *acquired = NULL, *start = NULL, *end = NULL;
            HRESULT hr = E_UNEXPECTED;

            if ( item->type != XML_ELEMENT_NODE || strcmp( ( const char * )item->name, "Item" ) ) continue;
            if ( out->count == STORE_COLLECTION_MAX_ITEMS ) return E_UNEXPECTED;
            product = store_collection_text( item, "ProductId" );
            sku = store_collection_text( item, "SkuId" );
            acquired = store_collection_text( item, "AcquiredDate" );
            start = store_collection_text( item, "StartDate" );
            end = store_collection_text( item, "EndDate" );
            if ( !product || strlen( product ) != 12 || !store_collection_alnum( product, 12 ) ||
                 !sku || strlen( sku ) != 4 || !store_collection_alnum( sku, 4 ) ||
                 !acquired || !store_collection_int64( acquired, &parsed.acquired ) ||
                 !end || !store_collection_int64( end, &parsed.end ) )
                goto done;
            memcpy( parsed.product, product, 12 );
            memcpy( parsed.sku, sku, 4 );
            parsed.has_acquired = TRUE;
            parsed.has_end = TRUE;
            if ( start && strcmp( start, "0" ) )
            {
                if ( !store_collection_int64( start, &parsed.start ) ) goto done;
                parsed.has_start = TRUE;
            }
            for ( i = 0; i < out->count; i++ )
                if ( !strcmp( out->items[i].product, parsed.product ) &&
                     !strcmp( out->items[i].sku, parsed.sku ) )
                    goto done;
            if ( parsed.acquired > ( INT64 )time( NULL ) ) goto done;
            if ( parsed.has_start && parsed.start > ( INT64 )time( NULL ) ) goto done;
            if ( parsed.end <= ( INT64 )time( NULL ) ) goto done;
            out->items[out->count++] = parsed;
            hr = S_OK;
        done:
            free( product );
            free( sku );
            free( acquired );
            free( start );
            free( end );
            if ( FAILED( hr ) ) return hr;
        }
    }
    return S_OK;
}
