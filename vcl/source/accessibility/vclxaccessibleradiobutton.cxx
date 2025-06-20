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

#include <accessibility/vclxaccessibleradiobutton.hxx>

#include <unotools/accessiblerelationsethelper.hxx>
#include <comphelper/accessiblecontexthelper.hxx>
#include <comphelper/accessiblekeybindinghelper.hxx>
#include <comphelper/sequence.hxx>
#include <com/sun/star/awt/KeyModifier.hpp>
#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <vcl/accessibility/strings.hxx>
#include <vcl/window.hxx>
#include <vcl/event.hxx>
#include <vcl/vclevent.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::accessibility;
using namespace ::comphelper;


// VCLXAccessibleRadioButton


void VCLXAccessibleRadioButton::ProcessWindowEvent( const VclWindowEvent& rVclWindowEvent )
{
    switch ( rVclWindowEvent.GetId() )
    {
        case VclEventId::RadiobuttonToggle:
        {
            Any aOldValue;
            Any aNewValue;

            VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
            if (pRadioButton && pRadioButton->IsChecked())
                aNewValue <<= AccessibleStateType::CHECKED;
            else
                aOldValue <<= AccessibleStateType::CHECKED;

            NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
        }
        break;
        default:
            VCLXAccessibleTextComponent::ProcessWindowEvent( rVclWindowEvent );
   }
}


void VCLXAccessibleRadioButton::FillAccessibleRelationSet( utl::AccessibleRelationSetHelper& rRelationSet )
{
    VCLXAccessibleTextComponent::FillAccessibleRelationSet( rRelationSet );

    VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
    if ( !pRadioButton )
        return;

    std::vector< VclPtr<RadioButton> > aGroup(pRadioButton->GetRadioButtonGroup());
    if (!aGroup.empty())
    {
        std::vector<Reference<css::accessibility::XAccessible>> aVec;
        aVec.reserve(aGroup.size());
        std::transform(aGroup.begin(), aGroup.end(), std::back_inserter(aVec),
            [](const VclPtr<RadioButton>& rxItem) { return rxItem->GetAccessible(); });
        rRelationSet.AddRelation(AccessibleRelation(AccessibleRelationType_MEMBER_OF,
                                                    comphelper::containerToSequence(aVec)));
    }
}


void VCLXAccessibleRadioButton::FillAccessibleStateSet( sal_Int64& rStateSet )
{
    VCLXAccessibleTextComponent::FillAccessibleStateSet( rStateSet );

    VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
    if (pRadioButton)
    {
        rStateSet |= AccessibleStateType::CHECKABLE;
        rStateSet |= AccessibleStateType::FOCUSABLE;
        if (pRadioButton->IsChecked())
            rStateSet |= AccessibleStateType::CHECKED;
    }
}


// XServiceInfo


OUString VCLXAccessibleRadioButton::getImplementationName()
{
    return u"com.sun.star.comp.toolkit.AccessibleRadioButton"_ustr;
}


Sequence< OUString > VCLXAccessibleRadioButton::getSupportedServiceNames()
{
    return { u"com.sun.star.awt.AccessibleRadioButton"_ustr };
}

// XAccessibleAction


sal_Int32 VCLXAccessibleRadioButton::getAccessibleActionCount( )
{
    OExternalLockGuard aGuard( this );

    return 1;
}


sal_Bool VCLXAccessibleRadioButton::doAccessibleAction ( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
    if (pRadioButton && !pRadioButton->IsChecked())
        pRadioButton->Check(true);

    return true;
}

OUString VCLXAccessibleRadioButton::getAccessibleActionDescription ( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    return RID_STR_ACC_ACTION_SELECT;
}

Reference< XAccessibleKeyBinding > VCLXAccessibleRadioButton::getAccessibleActionKeyBinding( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    rtl::Reference<OAccessibleKeyBindingHelper> pKeyBindingHelper = new OAccessibleKeyBindingHelper();

    VclPtr<vcl::Window> pWindow = GetWindow();
    if ( pWindow )
    {
        KeyEvent aKeyEvent = pWindow->GetActivationKey();
        vcl::KeyCode aKeyCode = aKeyEvent.GetKeyCode();
        if ( aKeyCode.GetCode() != 0 )
        {
            awt::KeyStroke aKeyStroke;
            aKeyStroke.Modifiers = 0;
            if ( aKeyCode.IsShift() )
                aKeyStroke.Modifiers |= awt::KeyModifier::SHIFT;
            if ( aKeyCode.IsMod1() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD1;
            if ( aKeyCode.IsMod2() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD2;
            if ( aKeyCode.IsMod3() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD3;
            aKeyStroke.KeyCode = aKeyCode.GetCode();
            aKeyStroke.KeyChar = aKeyEvent.GetCharCode();
            aKeyStroke.KeyFunc = static_cast< sal_Int16 >( aKeyCode.GetFunction() );
            pKeyBindingHelper->AddKeyBinding( aKeyStroke );
        }
    }

    return pKeyBindingHelper;
}


// XAccessibleValue


Any VCLXAccessibleRadioButton::getCurrentValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;

    VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
    if (pRadioButton)
        aValue <<= static_cast<sal_Int32>(pRadioButton->IsChecked() ? 1 : 0);

    return aValue;
}


sal_Bool VCLXAccessibleRadioButton::setCurrentValue( const Any& aNumber )
{
    OExternalLockGuard aGuard( this );

    bool bReturn = false;

    VclPtr<RadioButton> pRadioButton = GetAs<RadioButton>();
    if (pRadioButton)
    {
        sal_Int32 nValue = 0;
        OSL_VERIFY( aNumber >>= nValue );

        if ( nValue < 0 )
            nValue = 0;
        else if ( nValue > 1 )
            nValue = 1;

        pRadioButton->Check(nValue == 1);
        bReturn = true;
    }

    return bReturn;
}


Any VCLXAccessibleRadioButton::getMaximumValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(1);

    return aValue;
}


Any VCLXAccessibleRadioButton::getMinimumValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(0);

    return aValue;
}

Any VCLXAccessibleRadioButton::getMinimumIncrement(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(1);

    return aValue;
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
