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

#include <tools/gen.hxx>
#include <tools/debug.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <rtl/ustring.hxx>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <comphelper/accessibleeventnotifier.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <editeng/AccessibleEditableTextPara.hxx>
#include <editeng/eerdll.hxx>

#include <editeng/editdata.hxx>
#include <editeng/outliner.hxx>
#include <editeng/editrids.hrc>
#include <editeng/unoedsrc.hxx>
#include <svtools/colorcfg.hxx>

#include "AccessibleImageBullet.hxx"

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;

namespace accessibility
{

AccessibleImageBullet::AccessibleImageBullet(uno::Reference<XAccessible> xParent,
                                             sal_Int64 nIndexInParent) :
    mnParagraphIndex( 0 ),
    mnIndexInParent(nIndexInParent),
    mpEditSource( nullptr ),
    maEEOffset( 0, 0 ),
    mxParent(std::move( xParent ))
{
    // these are always on
    mnStateSet = AccessibleStateType::VISIBLE;
    mnStateSet |= AccessibleStateType::SHOWING;
    mnStateSet |= AccessibleStateType::ENABLED;
    mnStateSet |= AccessibleStateType::SENSITIVE;
}

sal_Int64 SAL_CALL  AccessibleImageBullet::getAccessibleChildCount()
{
    return 0;
}

uno::Reference< XAccessible > SAL_CALL  AccessibleImageBullet::getAccessibleChild( sal_Int64 )
{
    throw lang::IndexOutOfBoundsException(u"No children available"_ustr,
                                          getXWeak() );
}

uno::Reference< XAccessible > SAL_CALL  AccessibleImageBullet::getAccessibleParent()
{
    return mxParent;
}

sal_Int64 SAL_CALL  AccessibleImageBullet::getAccessibleIndexInParent()
{
    return mnIndexInParent;
}

sal_Int16 SAL_CALL  AccessibleImageBullet::getAccessibleRole()
{
    return AccessibleRole::GRAPHIC;
}

OUString SAL_CALL  AccessibleImageBullet::getAccessibleDescription()
{
    // Get the string from the resource for the specified id.
    return EditResId(RID_SVXSTR_A11Y_IMAGEBULLET_DESCRIPTION);
}

OUString SAL_CALL  AccessibleImageBullet::getAccessibleName()
{
    // Get the string from the resource for the specified id.
    return EditResId(RID_SVXSTR_A11Y_IMAGEBULLET_NAME);
}

uno::Reference< XAccessibleRelationSet > SAL_CALL AccessibleImageBullet::getAccessibleRelationSet()
{
    // no relations, therefore empty
    return uno::Reference< XAccessibleRelationSet >();
}

sal_Int64 SAL_CALL AccessibleImageBullet::getAccessibleStateSet()
{
    SolarMutexGuard aGuard;

    // Create a copy of the state set and return it.

    return mnStateSet;
}

lang::Locale SAL_CALL AccessibleImageBullet::getLocale()
{
    SolarMutexGuard aGuard;

    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleImageBullet::getLocale: paragraph index value overflow");

    // return locale of first character in the paragraph
    return LanguageTag(GetTextForwarder().GetLanguage( GetParagraphIndex(), 0 )).getLocale();
}

uno::Reference< XAccessible > SAL_CALL AccessibleImageBullet::getAccessibleAtPoint( const awt::Point& /*aPoint*/ )
{
    // as we have no children, empty reference
    return uno::Reference< XAccessible >();
}

awt::Rectangle AccessibleImageBullet::implGetBounds(  )
{
    DBG_ASSERT(GetParagraphIndex() >= 0,
               "AccessibleEditableTextPara::implGetBounds: index value overflow");

    SvxTextForwarder& rCacheTF = GetTextForwarder();
    EBulletInfo aBulletInfo = rCacheTF.GetBulletInfo( GetParagraphIndex() );
    tools::Rectangle aParentRect = rCacheTF.GetParaBounds( GetParagraphIndex() );

    if( aBulletInfo.nParagraph != EE_PARA_MAX &&
        aBulletInfo.bVisible &&
        aBulletInfo.nType == SVX_NUM_BITMAP )
    {
        tools::Rectangle aRect = aBulletInfo.aBounds;

        // subtract paragraph position (bullet pos is absolute in EditEngine/Outliner)
        aRect.Move( -aParentRect.Left(), -aParentRect.Top() );

        // convert to screen coordinates
        tools::Rectangle aScreenRect = AccessibleEditableTextPara::LogicToPixel( aRect,
                                                                          rCacheTF.GetMapMode(),
                                                                          GetViewForwarder() );

        // offset from shape/cell
        Point aOffset = maEEOffset;

        return awt::Rectangle( aScreenRect.Left() + aOffset.X(),
                               aScreenRect.Top() + aOffset.Y(),
                               aScreenRect.GetSize().Width(),
                               aScreenRect.GetSize().Height() );
    }

    return awt::Rectangle();
}

void SAL_CALL AccessibleImageBullet::grabFocus(  )
{
    throw uno::RuntimeException(u"Not focusable"_ustr,
                                uno::Reference< uno::XInterface >
                                ( static_cast< XAccessible* > (this) ) );   // disambiguate hierarchy
}

sal_Int32 SAL_CALL AccessibleImageBullet::getForeground(  )
{
    // #104444# Added to XAccessibleComponent interface
    svtools::ColorConfig aColorConfig;
    Color nColor = aColorConfig.GetColorValue( svtools::FONTCOLOR ).nColor;
    return static_cast<sal_Int32>(nColor);
}

sal_Int32 SAL_CALL AccessibleImageBullet::getBackground(  )
{
    // #104444# Added to XAccessibleComponent interface
    Color aColor( Application::GetSettings().GetStyleSettings().GetWindowColor() );

    // the background is transparent
    aColor.SetAlpha(0);

    return static_cast<sal_Int32>( aColor );
}

OUString SAL_CALL AccessibleImageBullet::getImplementationName()
{
    return u"AccessibleImageBullet"_ustr;
}

sal_Bool SAL_CALL AccessibleImageBullet::supportsService (const OUString& sServiceName)
{

    return cppu::supportsService(this, sServiceName);
}

uno::Sequence< OUString > SAL_CALL AccessibleImageBullet::getSupportedServiceNames()
{
    return { u"com.sun.star.accessibility.AccessibleContext"_ustr };
}

void AccessibleImageBullet::SetEEOffset( const Point& rOffset )
{
    maEEOffset = rOffset;
}

void SAL_CALL AccessibleImageBullet::dispose()
{
    mxParent = nullptr;
    mpEditSource = nullptr;

    OAccessible::dispose();
}

void AccessibleImageBullet::SetEditSource( SvxEditSource* pEditSource )
{
    mpEditSource = pEditSource;

    if( !mpEditSource )
    {
        // going defunc
        UnSetState( AccessibleStateType::SHOWING );
        UnSetState( AccessibleStateType::VISIBLE );
        SetState( AccessibleStateType::INVALID );
        SetState( AccessibleStateType::DEFUNC );
    }
}

void AccessibleImageBullet::SetState( const sal_Int64 nStateId )
{
    if( !(mnStateSet & nStateId) )
    {
        mnStateSet |= nStateId;
        NotifyAccessibleEvent(AccessibleEventId::STATE_CHANGED, uno::Any(), uno::Any(nStateId));
    }
}

void AccessibleImageBullet::UnSetState( const sal_Int64 nStateId )
{
    if( mnStateSet & nStateId )
    {
        mnStateSet &= ~nStateId;
        NotifyAccessibleEvent(AccessibleEventId::STATE_CHANGED, uno::Any(nStateId), uno::Any());
    }
}


void AccessibleImageBullet::SetParagraphIndex( sal_Int32 nIndex )
{
    uno::Any aOldDesc;
    uno::Any aOldName;

    try
    {
        aOldDesc <<= getAccessibleDescription();
        aOldName <<= getAccessibleName();
    }
    catch( const uno::Exception& ) {} // optional behaviour

    sal_Int32 nOldIndex = mnParagraphIndex;

    mnParagraphIndex = nIndex;

    try
    {
        if( nOldIndex != nIndex )
        {
            // index and therefore description changed
            NotifyAccessibleEvent(AccessibleEventId::DESCRIPTION_CHANGED, aOldDesc, uno::Any(getAccessibleDescription()));
            NotifyAccessibleEvent(AccessibleEventId::NAME_CHANGED, aOldName, uno::Any(getAccessibleName()));
        }
    }
    catch( const uno::Exception& ) {} // optional behaviour
}


SvxEditSource& AccessibleImageBullet::GetEditSource() const
{
    if( !mpEditSource )
        throw uno::RuntimeException(u"No edit source, object is defunct"_ustr,
                                    cppu::getXWeak
                                      ( const_cast< AccessibleImageBullet* > (this) ) );  // disambiguate hierarchy
    return *mpEditSource;
}

SvxTextForwarder& AccessibleImageBullet::GetTextForwarder() const
{
    SvxEditSource& rEditSource = GetEditSource();
    SvxTextForwarder* pTextForwarder = rEditSource.GetTextForwarder();

    if( !pTextForwarder )
        throw uno::RuntimeException(u"Unable to fetch text forwarder, object is defunct"_ustr,
                                    cppu::getXWeak
                                      ( const_cast< AccessibleImageBullet* > (this) ) );  // disambiguate hierarchy

    if( !pTextForwarder->IsValid() )
        throw uno::RuntimeException(u"Text forwarder is invalid, object is defunct"_ustr,
                                    cppu::getXWeak
                                      ( const_cast< AccessibleImageBullet* > (this) ) );  // disambiguate hierarchy
    return *pTextForwarder;
}

SvxViewForwarder& AccessibleImageBullet::GetViewForwarder() const
{
    SvxEditSource& rEditSource = GetEditSource();
    SvxViewForwarder* pViewForwarder = rEditSource.GetViewForwarder();

    if( !pViewForwarder )
    {
        throw uno::RuntimeException(u"Unable to fetch view forwarder, object is defunct"_ustr,
                                    cppu::getXWeak
                                      ( const_cast< AccessibleImageBullet* > (this) ) );  // disambiguate hierarchy
    }

    if( !pViewForwarder->IsValid() )
        throw uno::RuntimeException(u"View forwarder is invalid, object is defunct"_ustr,
                                    cppu::getXWeak
                                      ( const_cast< AccessibleImageBullet* > (this) ) ); // disambiguate hierarchy
    return *pViewForwarder;
}


} // end of namespace accessibility

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
