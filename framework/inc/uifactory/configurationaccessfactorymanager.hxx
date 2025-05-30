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

#pragma once

#include <sal/config.h>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/container/XContainerListener.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <cppuhelper/implbase.hxx>
#include <rtl/ref.hxx>
#include <rtl/ustring.hxx>

#include <mutex>
#include <string_view>
#include <unordered_map>


namespace framework {

class WeakContainerListener;

class ConfigurationAccess_FactoryManager final : public ::cppu::WeakImplHelper< css::container::XContainerListener>
{
    public:
                      ConfigurationAccess_FactoryManager( const css::uno::Reference< css::uno::XComponentContext>& rxContext, OUString  _sRoot );
        virtual       ~ConfigurationAccess_FactoryManager() override;

        void          readConfigurationData();

        OUString                           getFactorySpecifierFromTypeNameModule( std::u16string_view rType, std::u16string_view rName, std::u16string_view rModule ) const;
        void                                    addFactorySpecifierToTypeNameModule( std::u16string_view rType, std::u16string_view rName, std::u16string_view rModule, const OUString& aServiceSpecifier );
        void                                    removeFactorySpecifierFromTypeNameModule( std::u16string_view rType, std::u16string_view rName, std::u16string_view rModule );
        css::uno::Sequence< css::uno::Sequence< css::beans::PropertyValue > >   getFactoriesDescription() const;

        // container.XContainerListener
    virtual void SAL_CALL elementInserted( const css::container::ContainerEvent& Event ) override;
    virtual void SAL_CALL elementRemoved( const css::container::ContainerEvent& Event ) override;
    virtual void SAL_CALL elementReplaced( const css::container::ContainerEvent& Event ) override;

    // lang.XEventListener
    virtual void SAL_CALL disposing( const css::lang::EventObject& Source ) override;

    private:

        bool impl_getElementProps( const css::uno::Any& rElement, OUString& rType, OUString& rName, OUString& rModule, OUString& rServiceSpecifier ) const;

        mutable std::mutex           m_aMutex;
        OUString                     m_aPropType;
        OUString                     m_aPropName;
        OUString                     m_aPropModule;
        OUString                     m_aPropFactory;
        OUString                     m_sRoot;
        std::unordered_map<OUString, OUString> m_aFactoryManagerMap;
        css::uno::Reference< css::lang::XMultiServiceFactory >     m_xConfigProvider;
        css::uno::Reference< css::container::XNameAccess >         m_xConfigAccess;
        rtl::Reference< WeakContainerListener >  m_xConfigListener;
        bool                         m_bConfigAccessInitialized;
};

} // namespace framework

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
