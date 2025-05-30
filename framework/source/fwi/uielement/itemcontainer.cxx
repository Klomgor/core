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

#include <sal/config.h>

#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <uielement/itemcontainer.hxx>
#include <uielement/constitemcontainer.hxx>
#include <comphelper/servicehelper.hxx>
#include <rtl/ref.hxx>

using namespace cppu;
using namespace com::sun::star::uno;
using namespace com::sun::star::lang;
using namespace com::sun::star::beans;
using namespace com::sun::star::container;

constexpr OUString WRONG_TYPE_EXCEPTION
    = u"Type must be css::uno::Sequence< css::beans::PropertyValue >"_ustr;

namespace framework
{

//  XInterface, XTypeProvider

ItemContainer::ItemContainer( const ShareableMutex& rMutex ) :
    m_aShareMutex( rMutex )
{
}

ItemContainer::ItemContainer( const ConstItemContainer& rConstItemContainer, const ShareableMutex& rMutex ) : m_aShareMutex( rMutex )
{
    copyItemContainer( rConstItemContainer.m_aItemVector, rMutex );
}

ItemContainer::ItemContainer( const Reference< XIndexAccess >& rSourceContainer, const ShareableMutex& rMutex ) :
    m_aShareMutex( rMutex )
{
    if ( !rSourceContainer.is() )
        return;

    sal_Int32 nCount = rSourceContainer->getCount();
    try
    {
        for ( sal_Int32 i = 0; i < nCount; i++ )
        {
            Sequence< PropertyValue > aPropSeq;
            if ( rSourceContainer->getByIndex( i ) >>= aPropSeq )
            {
                sal_Int32 nContainerIndex = -1;
                Reference< XIndexAccess > xIndexAccess;
                for ( sal_Int32 j = 0; j < aPropSeq.getLength(); j++ )
                {
                    if ( aPropSeq[j].Name == "ItemDescriptorContainer" )
                    {
                        aPropSeq[j].Value >>= xIndexAccess;
                        nContainerIndex = j;
                        break;
                    }
                }

                if ( xIndexAccess.is() && nContainerIndex >= 0 )
                    aPropSeq.getArray()[nContainerIndex].Value <<= Reference<XIndexAccess>(deepCopyContainer( xIndexAccess, rMutex ));

                m_aItemVector.push_back( aPropSeq );
            }
        }
    }
    catch ( const IndexOutOfBoundsException& )
    {
    }
}

ItemContainer::~ItemContainer()
{
}

// private
void ItemContainer::copyItemContainer( const std::vector< Sequence< PropertyValue > >& rSourceVector, const ShareableMutex& rMutex )
{
    const sal_uInt32 nCount = rSourceVector.size();
    for ( sal_uInt32 i = 0; i < nCount; ++i )
    {
        sal_Int32 nContainerIndex = -1;
        Sequence< PropertyValue > aPropSeq( rSourceVector[i] );
        Reference< XIndexAccess > xIndexAccess;
        for ( sal_Int32 j = 0; j < aPropSeq.getLength(); j++ )
        {
            if ( aPropSeq[j].Name == "ItemDescriptorContainer" )
            {
                aPropSeq[j].Value >>= xIndexAccess;
                nContainerIndex = j;
                break;
            }
        }

        if ( xIndexAccess.is() && nContainerIndex >= 0 )
            aPropSeq.getArray()[nContainerIndex].Value <<= Reference<XIndexAccess>(deepCopyContainer( xIndexAccess, rMutex ));

        m_aItemVector.push_back( aPropSeq );
    }
}

rtl::Reference< ItemContainer > ItemContainer::deepCopyContainer( const Reference< XIndexAccess >& rSubContainer, const ShareableMutex& rMutex )
{
    rtl::Reference< ItemContainer > xReturn;
    if ( rSubContainer.is() )
    {
        ConstItemContainer* pSource = dynamic_cast<ConstItemContainer*>( rSubContainer.get() );
        if ( pSource )
            xReturn = new ItemContainer( *pSource, rMutex );
        else
            xReturn = new ItemContainer( rSubContainer, rMutex );
    }

    return xReturn;
}

// XElementAccess
sal_Bool SAL_CALL ItemContainer::hasElements()
{
    ShareGuard aLock( m_aShareMutex );
    return ( !m_aItemVector.empty() );
}

// XIndexAccess
sal_Int32 SAL_CALL ItemContainer::getCount()
{
    ShareGuard aLock( m_aShareMutex );
    return m_aItemVector.size();
}

Any SAL_CALL ItemContainer::getByIndex( sal_Int32 Index )
{
    ShareGuard aLock( m_aShareMutex );
    if ( sal_Int32( m_aItemVector.size()) <= Index )
        throw IndexOutOfBoundsException( OUString(), static_cast<OWeakObject *>(this) );

    return Any( m_aItemVector[Index] );
}

// XIndexContainer
void SAL_CALL ItemContainer::insertByIndex( sal_Int32 Index, const Any& aItem )
{
    Sequence< PropertyValue > aSeq;
    if ( !(aItem >>= aSeq) )
        throw IllegalArgumentException( WRONG_TYPE_EXCEPTION,
                                        static_cast<OWeakObject *>(this), 2 );

    ShareGuard aLock( m_aShareMutex );
    if ( sal_Int32( m_aItemVector.size()) == Index )
        m_aItemVector.push_back( aSeq );
    else if ( sal_Int32( m_aItemVector.size()) >Index )
    {
        std::vector< Sequence< PropertyValue > >::iterator aIter = m_aItemVector.begin();
        aIter += Index;
        m_aItemVector.insert( aIter, aSeq );
    }
    else
        throw IndexOutOfBoundsException( OUString(), static_cast<OWeakObject *>(this) );
}

void SAL_CALL ItemContainer::removeByIndex( sal_Int32 nIndex )
{
    ShareGuard aLock( m_aShareMutex );
    if ( static_cast<sal_Int32>(m_aItemVector.size()) <= nIndex )
        throw IndexOutOfBoundsException( OUString(), static_cast<OWeakObject *>(this) );

    m_aItemVector.erase(m_aItemVector.begin() + nIndex);
}

void SAL_CALL ItemContainer::replaceByIndex( sal_Int32 Index, const Any& aItem )
{
    Sequence< PropertyValue > aSeq;
    if ( !(aItem >>= aSeq) )
        throw IllegalArgumentException( WRONG_TYPE_EXCEPTION,
                                        static_cast<OWeakObject *>(this), 2 );

    ShareGuard aLock( m_aShareMutex );
    if ( sal_Int32( m_aItemVector.size()) <= Index )
        throw IndexOutOfBoundsException( OUString(), static_cast<OWeakObject *>(this) );

    m_aItemVector[Index] = std::move(aSeq);
}

} // namespace framework

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
