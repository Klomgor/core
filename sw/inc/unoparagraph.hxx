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

#ifndef INCLUDED_SW_INC_UNOPARAGRAPH_HXX
#define INCLUDED_SW_INC_UNOPARAGRAPH_HXX

#include <memory>
#include <optional>

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertyState.hpp>
#include <com/sun/star/beans/XMultiPropertySet.hpp>
#include <com/sun/star/beans/XTolerantMultiPropertySet.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XContentEnumerationAccess.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextRange.hpp>

#include <comphelper/interfacecontainer4.hxx>
#include <cppuhelper/implbase.hxx>

#include <sfx2/Metadatable.hxx>
#include <svl/listener.hxx>

#include "unobaseclass.hxx"

class SfxItemPropertySet;
class SfxItemSet;
struct SfxItemPropertyMapEntry;
class SwPaM;
class SwUnoCursor;
class SwTextNode;
class SwTableBox;
class SwXText;
class SwXTextPortionEnumeration;

typedef ::cppu::ImplInheritanceHelper
<   ::sfx2::MetadatableMixin
,   css::lang::XServiceInfo
,   css::beans::XPropertySet
,   css::beans::XPropertyState
,   css::beans::XMultiPropertySet
,   css::beans::XTolerantMultiPropertySet
,   css::container::XEnumerationAccess
,   css::container::XContentEnumerationAccess
,   css::text::XTextContent
,   css::text::XTextRange
> SwXParagraph_Base;

class SwXParagraph final
    : public SwXParagraph_Base
{

private:

    virtual ~SwXParagraph() override;

    SwXParagraph(css::uno::Reference< SwXText > const & xParent,
            SwTextNode & rTextNode,
            const sal_Int32 nSelStart, const sal_Int32 nSelEnd);

    /// descriptor
    SwXParagraph();

    /// @throws beans::UnknownPropertyException
    /// @throws beans::PropertyVetoException
    /// @throws lang::IllegalArgumentException
    /// @throws lang::WrappedTargetException
    /// @throws uno::RuntimeException
    void SetPropertyValues_Impl(
        const css::uno::Sequence< OUString >& rPropertyNames,
        const css::uno::Sequence< css::uno::Any >& rValues );
    /// @throws beans::UnknownPropertyException
    /// @throws lang::WrappedTargetException
    /// @throws uno::RuntimeException
    css::uno::Sequence< css::uno::Any > GetPropertyValues_Impl(
            const css::uno::Sequence< OUString >& rPropertyNames);
    SwTextNode& GetTextNodeOrThrow();
    /// @throws uno::RuntimeException
    static void GetSinglePropertyValue_Impl(
        const SfxItemPropertyMapEntry& rEntry,
        const SfxItemSet& rSet,
        css::uno::Any& rAny );
    /// @throws uno::RuntimeException
    css::uno::Sequence< css::beans::GetDirectPropertyTolerantResult >
        GetPropertyValuesTolerant_Impl(
            const css::uno::Sequence< OUString >& rPropertyNames,
            bool bDirectValuesOnly);

public:

    static rtl::Reference<SwXParagraph>
        CreateXParagraph(SwDoc & rDoc, SwTextNode * pTextNode,
            css::uno::Reference<SwXText> const& xParentText,
            const sal_Int32 nSelStart = -1, const sal_Int32 nSelEnd = - 1);

    const SwTextNode * GetTextNode() const { return m_pTextNode; }
    bool            IsDescriptor() const { return m_bIsDescriptor; }
    /// make rPaM select the paragraph
    bool SelectPaM(SwPaM & rPaM);
    /// for SwXText
    void attachToText(SwXText & rParent, SwTextNode & rTextNode);

    // MetadatableMixin
    virtual ::sfx2::Metadatable* GetCoreObject() override;
    virtual css::uno::Reference< css::frame::XModel >
        GetModel() override;

    // XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(
            const OUString& rServiceName) override;
    virtual css::uno::Sequence< OUString > SAL_CALL
        getSupportedServiceNames() override;

    // XComponent
    virtual void SAL_CALL dispose() override;
    virtual void SAL_CALL addEventListener(
            const css::uno::Reference< css::lang::XEventListener > & xListener) override;
    virtual void SAL_CALL removeEventListener(
            const css::uno::Reference< css::lang::XEventListener > & xListener) override;

    // XPropertySet
    virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL
        getPropertySetInfo() override;
    virtual void SAL_CALL setPropertyValue(
            const OUString& rPropertyName,
            const css::uno::Any& rValue) override;
    virtual css::uno::Any SAL_CALL getPropertyValue(
            const OUString& rPropertyName) override;
    virtual void SAL_CALL addPropertyChangeListener(
            const OUString& rPropertyName,
            const css::uno::Reference< css::beans::XPropertyChangeListener >& xListener) override;
    virtual void SAL_CALL removePropertyChangeListener(
            const OUString& rPropertyName,
            const css::uno::Reference< css::beans::XPropertyChangeListener >& xListener) override;
    virtual void SAL_CALL addVetoableChangeListener(
            const OUString& rPropertyName,
            const css::uno::Reference< css::beans::XVetoableChangeListener >& xListener) override;
    virtual void SAL_CALL removeVetoableChangeListener(
            const OUString& rPropertyName,
            const css::uno::Reference< css::beans::XVetoableChangeListener >& xListener) override;

    // XPropertyState
    virtual css::beans::PropertyState SAL_CALL
        getPropertyState(const OUString& rPropertyName) override;
    virtual css::uno::Sequence< css::beans::PropertyState > SAL_CALL
        getPropertyStates(
            const css::uno::Sequence< OUString >& rPropertyNames) override;
    virtual void SAL_CALL setPropertyToDefault(
            const OUString& rPropertyName) override;
    virtual css::uno::Any SAL_CALL getPropertyDefault(
            const OUString& rPropertyName) override;

    // XMultiPropertySet
    virtual void SAL_CALL setPropertyValues(
            const css::uno::Sequence< OUString >&      rPropertyNames,
            const css::uno::Sequence< css::uno::Any >& rValues) override;
    virtual css::uno::Sequence< css::uno::Any >
        SAL_CALL getPropertyValues(
            const css::uno::Sequence< OUString >&  rPropertyNames) override;
    virtual void SAL_CALL addPropertiesChangeListener(
            const css::uno::Sequence< OUString >&         rPropertyNames,
            const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener) override;
    virtual void SAL_CALL removePropertiesChangeListener(
            const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener) override;
    virtual void SAL_CALL firePropertiesChangeEvent(
            const css::uno::Sequence< OUString >&        rPropertyNames,
            const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener) override;

    // XTolerantMultiPropertySet
    virtual css::uno::Sequence< css::beans::SetPropertyTolerantFailed > SAL_CALL
        setPropertyValuesTolerant(
            const css::uno::Sequence< OUString >&      rPropertyNames,
            const css::uno::Sequence< css::uno::Any >& rValues) override;
    virtual css::uno::Sequence< css::beans::GetPropertyTolerantResult > SAL_CALL
        getPropertyValuesTolerant(
            const css::uno::Sequence< OUString >& rPropertyNames) override;
    virtual css::uno::Sequence<
            css::beans::GetDirectPropertyTolerantResult > SAL_CALL
        getDirectPropertyValuesTolerant(
            const css::uno::Sequence< OUString >& rPropertyNames) override;

    // XElementAccess
    virtual css::uno::Type SAL_CALL getElementType() override;
    virtual sal_Bool SAL_CALL hasElements() override;

    // XEnumerationAccess
    virtual css::uno::Reference< css::container::XEnumeration >  SAL_CALL
        createEnumeration() override;

    // XContentEnumerationAccess
    virtual css::uno::Reference< css::container::XEnumeration > SAL_CALL
        createContentEnumeration(const OUString& rServiceName) override;
    virtual css::uno::Sequence< OUString > SAL_CALL
        getAvailableServiceNames() override;

    // XTextContent
    virtual void SAL_CALL attach(
            const css::uno::Reference< css::text::XTextRange > & xTextRange) override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL getAnchor() override;

    // XTextRange
    virtual css::uno::Reference< css::text::XText >
        SAL_CALL getText() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL getStart() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL getEnd() override;
    virtual OUString SAL_CALL getString() override;
    virtual void SAL_CALL setString(const OUString& rString) override;

    /// tries to return less data, but may return more than just text fields
    rtl::Reference<SwXTextPortionEnumeration> createTextFieldsEnumeration();

private:
    std::mutex m_Mutex; // just for OInterfaceContainerHelper4
    ::comphelper::OInterfaceContainerHelper4<css::lang::XEventListener> m_EventListeners;
    SfxItemPropertySet const& m_rPropSet;
    bool m_bIsDescriptor;
    bool m_bDeleteWithoutCorrection = false;
    sal_Int32 m_nSelectionStartPos;
    sal_Int32 m_nSelectionEndPos;
    OUString m_sText;
    css::uno::Reference<SwXText> m_xParentText;
    SwTextNode* m_pTextNode;
    struct MySvtListener : public SvtListener
    {
        SwXParagraph& m_rThis;
        MySvtListener(SwXParagraph& rThis) : m_rThis(rThis) {}
        virtual void Notify(const SfxHint& rHint) override;
    };
    std::optional<MySvtListener> moSvtListener;
};


struct SwXParagraphEnumeration
    : public SwSimpleEnumeration_Base
{
    static rtl::Reference<SwXParagraphEnumeration> Create(
        css::uno::Reference< css::text::XText > const & xParent,
        const std::shared_ptr<SwUnoCursor>& pCursor,
        const CursorType eType,
        /// only for CursorType::TableText
        SwTableBox const*const pTableBox = nullptr);
};

#endif // INCLUDED_SW_INC_UNOPARAGRAPH_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
