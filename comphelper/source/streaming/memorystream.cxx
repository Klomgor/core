/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <algorithm>
#include <cassert>
#include <memory>


#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/lang/XUnoTunnel.hpp>
#include <com/sun/star/io/IOException.hpp>
//#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/memorystream.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>

#include <string.h>

namespace com::sun::star::uno { class XComponentContext; }

using ::cppu::OWeakObject;
using ::cppu::WeakImplHelper;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;

namespace comphelper
{


UNOMemoryStream::UNOMemoryStream()
: mnCursor(0)
{
    maData.reserve(1 * 1024 * 1024);
}

// XServiceInfo
OUString SAL_CALL UNOMemoryStream::getImplementationName()
{
    return u"com.sun.star.comp.MemoryStream"_ustr;
}

sal_Bool SAL_CALL UNOMemoryStream::supportsService(const OUString& ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

css::uno::Sequence<OUString> SAL_CALL UNOMemoryStream::getSupportedServiceNames()
{
    return { u"com.sun.star.comp.MemoryStream"_ustr };
}

// XStream
Reference< XInputStream > SAL_CALL UNOMemoryStream::getInputStream(  )
{
    return this;
}

Reference< XOutputStream > SAL_CALL UNOMemoryStream::getOutputStream(  )
{
    return this;
}

// XInputStream
sal_Int32 SAL_CALL UNOMemoryStream::readBytes( Sequence< sal_Int8 >& aData, sal_Int32 nBytesToRead )
{
    if( nBytesToRead < 0 )
        throw IOException(u"nBytesToRead < 0"_ustr);

    nBytesToRead = std::min( nBytesToRead, available() );
    aData.realloc( nBytesToRead );

    if( nBytesToRead )
    {
        sal_Int8* pData = &(*maData.begin());
        sal_Int8* pCursor = &(pData[mnCursor]);
        memcpy( aData.getArray(), pCursor, nBytesToRead );

        mnCursor += nBytesToRead;
    }

    return nBytesToRead;
}

// ByteReader
sal_Int32 UNOMemoryStream::readSomeBytes( sal_Int8* aData, sal_Int32 nBytesToRead )
{
    if( nBytesToRead < 0 )
        throw IOException(u"nBytesToRead < 0"_ustr);

    nBytesToRead = std::min( nBytesToRead, available() );

    if( nBytesToRead )
    {
        sal_Int8* pData = &(*maData.begin());
        sal_Int8* pCursor = &(pData[mnCursor]);
        memcpy( aData, pCursor, nBytesToRead );

        mnCursor += nBytesToRead;
    }

    return nBytesToRead;
}

sal_Int32 SAL_CALL UNOMemoryStream::readSomeBytes( Sequence< sal_Int8 >& aData, sal_Int32 nMaxBytesToRead )
{
    return readBytes( aData, nMaxBytesToRead );
}

void SAL_CALL UNOMemoryStream::skipBytes( sal_Int32 nBytesToSkip )
{
    if( nBytesToSkip < 0 )
        throw IOException(u"nBytesToSkip < 0"_ustr);

    mnCursor += std::min( nBytesToSkip, available() );
}

sal_Int32 SAL_CALL UNOMemoryStream::available()
{
    return std::min<sal_Int64>( SAL_MAX_INT32, maData.size() - mnCursor);
}

void SAL_CALL UNOMemoryStream::closeInput()
{
    mnCursor = 0;
}

// XSeekable
void SAL_CALL UNOMemoryStream::seek( sal_Int64 location )
{
    if( (location < 0) || (location > SAL_MAX_INT32) )
        throw IllegalArgumentException(u"this implementation does not support more than 2GB!"_ustr, static_cast<OWeakObject*>(this), 0 );

    // seek operation should be able to resize the stream
    if ( o3tl::make_unsigned(location) > maData.size() )
        maData.resize( static_cast< sal_Int32 >( location ) );

    mnCursor = static_cast< sal_Int32 >( location );
}

sal_Int64 SAL_CALL UNOMemoryStream::getPosition()
{
    return static_cast< sal_Int64 >( mnCursor );
}

sal_Int64 SAL_CALL UNOMemoryStream::getLength()
{
    return static_cast< sal_Int64 >( maData.size() );
}

// XOutputStream
void SAL_CALL UNOMemoryStream::writeBytes( const Sequence< sal_Int8 >& aData )
{
    writeBytes(aData.getConstArray(), aData.getLength());
}

void UNOMemoryStream::writeBytes( const sal_Int8* pInData, sal_Int32 nBytesToWrite )
{
    assert(nBytesToWrite >= 0);
    if( !nBytesToWrite )
        return;

    sal_Int64 nNewSize = static_cast<sal_Int64>(mnCursor) + nBytesToWrite;
    if( nNewSize > SAL_MAX_INT32 )
    {
        OSL_ASSERT(false);
        throw IOException(u"this implementation does not support more than 2GB!"_ustr, static_cast<OWeakObject*>(this) );
    }

    if( o3tl::make_unsigned( nNewSize ) > maData.size() )
        maData.resize( nNewSize );

    sal_Int8* pData = &(*maData.begin());
    sal_Int8* pCursor = &(pData[mnCursor]);
    memcpy(pCursor, pInData, nBytesToWrite);

    mnCursor += nBytesToWrite;
}

void SAL_CALL UNOMemoryStream::flush()
{
}

void SAL_CALL UNOMemoryStream::closeOutput()
{
    mnCursor = 0;
}

//XTruncate
void SAL_CALL UNOMemoryStream::truncate()
{
    maData.clear();
    mnCursor = 0;
}

} // namespace comphelper

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_MemoryStream(
    css::uno::XComponentContext *,
    css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new ::comphelper::UNOMemoryStream());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
