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

#include "ConfigurationControllerResourceManager.hxx"
#include "ConfigurationControllerBroadcaster.hxx"
#include "ResourceFactoryManager.hxx"
#include <ResourceId.hxx>
#include <framework/FrameworkHelper.hxx>
#include <com/sun/star/lang/DisposedException.hpp>
#include <framework/Configuration.hxx>
#include <framework/ResourceFactory.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <sal/log.hxx>
#include <algorithm>
#include <utility>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::drawing::framework;

namespace sd::framework {

//===== ConfigurationControllerResourceManager ================================

ConfigurationControllerResourceManager::ConfigurationControllerResourceManager (
    std::shared_ptr<ResourceFactoryManager> pResourceFactoryContainer,
    std::shared_ptr<ConfigurationControllerBroadcaster>  pBroadcaster)
    : maResourceMap(ResourceComparator()),
      mpResourceFactoryContainer(std::move(pResourceFactoryContainer)),
      mpBroadcaster(std::move(pBroadcaster))
{
}

ConfigurationControllerResourceManager::~ConfigurationControllerResourceManager()
{
}

ConfigurationControllerResourceManager::ResourceDescriptor
    ConfigurationControllerResourceManager::GetResource (
        const rtl::Reference<ResourceId>& rxResourceId)
{
    ::osl::MutexGuard aGuard (maMutex);
    ResourceMap::const_iterator iResource (maResourceMap.find(rxResourceId));
    if (iResource != maResourceMap.end())
        return iResource->second;
    else
        return ResourceDescriptor();
}

void ConfigurationControllerResourceManager::ActivateResources (
    const ::std::vector<rtl::Reference<ResourceId> >& rResources,
    const rtl::Reference<Configuration>& rxConfiguration)
{
    ::osl::MutexGuard aGuard (maMutex);
    // Iterate in normal order over the resources that are to be
    // activated so that resources on which others depend are activated
    // before the depending resources are activated.
    for (const rtl::Reference<ResourceId>& xResource : rResources)
        ActivateResource(xResource, rxConfiguration);
}

void ConfigurationControllerResourceManager::DeactivateResources (
    const ::std::vector<rtl::Reference<ResourceId> >& rResources,
    const rtl::Reference<Configuration>& rxConfiguration)
{
    ::osl::MutexGuard aGuard (maMutex);
    // Iterate in reverse order over the resources that are to be
    // deactivated so that resources on which others depend are deactivated
    // only when the depending resources have already been deactivated.
    ::std::for_each(
        rResources.rbegin(),
        rResources.rend(),
        [&] (rtl::Reference<ResourceId> const& xResource) {
            return DeactivateResource(xResource, rxConfiguration);
        } );
}

/* In this method we do following steps.
    1. Get the factory with which the resource will be created.
    2. Create the resource.
    3. Add the resource to the URL->Object map of the configuration
    controller.
    4. Add the resource id to the current configuration.
    5. Notify listeners.
*/
void ConfigurationControllerResourceManager::ActivateResource (
    const rtl::Reference<ResourceId>& rxResourceId,
    const rtl::Reference<Configuration>& rxConfiguration)
{
    if ( ! rxResourceId.is())
    {
       OSL_ASSERT(rxResourceId.is());
       return;
    }

    SAL_INFO("sd.fwk", __func__ << ": activating resource " <<
        FrameworkHelper::ResourceIdToString(rxResourceId));

    // 1. Get the factory.
    const OUString sResourceURL (rxResourceId->getResourceURL());
    rtl::Reference<ResourceFactory> xFactory (mpResourceFactoryContainer->GetFactory(sResourceURL));
    if ( ! xFactory.is())
    {
        SAL_INFO("sd.fwk", __func__ << ":    no factory found for " << sResourceURL);
        return;
    }

    try
    {
        // 2. Create the resource.
        rtl::Reference<AbstractResource> xResource;
        try
        {
            xResource = xFactory->createResource(rxResourceId);
        }
        catch (lang::DisposedException&)
        {
            // The factory is disposed and can be removed from the list
            // of registered factories.
            mpResourceFactoryContainer->RemoveFactoryForReference(xFactory);
        }
        catch (Exception&) {}

        if (xResource.is())
        {
            SAL_INFO("sd.fwk", __func__ << ":    successfully created");
            // 3. Add resource to URL->Object map.
            AddResource(xResource, xFactory);

            // 4. Add resource id to current configuration.
            rxConfiguration->addResource(rxResourceId);

            // 5. Notify the new resource to listeners of the ConfigurationController.
            mpBroadcaster->NotifyListeners(
                ConfigurationChangeEventType::ResourceActivation,
                rxResourceId,
                xResource);
        }
        else
        {
            SAL_INFO("sd.fwk", __func__ << ":    resource creation failed");
        }
    }
    catch (RuntimeException&)
    {
        DBG_UNHANDLED_EXCEPTION("sd");
    }
}

/* In this method we do following steps.
    1. Remove the resource from the URL->Object map of the configuration
    controller.
    2. Notify listeners that deactivation has started.
    3. Remove the resource id from the current configuration.
    4. Release the resource.
    5. Notify listeners about that deactivation is completed.
*/
void ConfigurationControllerResourceManager::DeactivateResource (
    const rtl::Reference<ResourceId>& rxResourceId,
    const rtl::Reference<Configuration>& rxConfiguration)
{
    if ( ! rxResourceId.is())
        return;

#if OSL_DEBUG_LEVEL >= 1
    bool bSuccess (false);
#endif
    try
    {
        // 1. Remove resource from URL->Object map.
        ResourceDescriptor aDescriptor (RemoveResource(rxResourceId));

        if (aDescriptor.mxResource.is() && aDescriptor.mxResourceFactory.is())
        {
            // 2.  Notify listeners that the resource is being deactivated.
            mpBroadcaster->NotifyListeners(
                ConfigurationChangeEventType::ResourceDeactivation,
                rxResourceId,
                aDescriptor.mxResource);

            // 3. Remove resource id from current configuration.
            rxConfiguration->removeResource(rxResourceId);

            // 4. Release the resource.
            try
            {
                aDescriptor.mxResourceFactory->releaseResource(aDescriptor.mxResource);
            }
            catch (const lang::DisposedException& rException)
            {
                if ( ! rException.Context.is()
                    || rException.Context == cppu::getXWeak(aDescriptor.mxResourceFactory.get()))
                {
                    // The factory is disposed and can be removed from the
                    // list of registered factories.
                    mpResourceFactoryContainer->RemoveFactoryForReference(
                        aDescriptor.mxResourceFactory);
                }
            }

#if OSL_DEBUG_LEVEL >= 1
            bSuccess = true;
#endif
        }
    }
    catch (RuntimeException&)
    {
        DBG_UNHANDLED_EXCEPTION("sd");
    }

#if OSL_DEBUG_LEVEL >= 1
    if (bSuccess)
        SAL_INFO("sd.fwk", __func__ << ": successfully deactivated " <<
            FrameworkHelper::ResourceIdToString(rxResourceId));
    else
        SAL_INFO("sd.fwk", __func__ << ": activating resource " <<
            FrameworkHelper::ResourceIdToString(rxResourceId)
            << " failed");
#endif
}

void ConfigurationControllerResourceManager::AddResource (
    const rtl::Reference<AbstractResource>& rxResource,
    const rtl::Reference<ResourceFactory>& rxFactory)
{
    if ( ! rxResource.is())
    {
        OSL_ASSERT(rxResource.is());
        return;
    }

    // Add the resource to the resource container.
    maResourceMap[rxResource->getResourceId()] = ResourceDescriptor{rxResource, rxFactory};

#if OSL_DEBUG_LEVEL >= 2
    SAL_INFO("sd.fwk", __func__ << ": ConfigurationControllerResourceManager::AddResource(): added " <<
            FrameworkHelper::ResourceIdToString(rxResource->getResourceId()) <<
            " -> " << rxResource.get());
#endif
}

ConfigurationControllerResourceManager::ResourceDescriptor
    ConfigurationControllerResourceManager::RemoveResource (
        const rtl::Reference<ResourceId>& rxResourceId)
{
    ResourceDescriptor aDescriptor;

    ResourceMap::iterator iResource (maResourceMap.find(rxResourceId));
    if (iResource != maResourceMap.end())
    {
#if OSL_DEBUG_LEVEL >= 2
        SAL_INFO("sd.fwk", __func__ << ": ConfigurationControllerResourceManager::RemoveResource(): removing " <<
                FrameworkHelper::ResourceIdToString(rxResourceId) <<
                " -> " << &iResource);
#endif

        aDescriptor = iResource->second;
        maResourceMap.erase(rxResourceId);
    }

    return aDescriptor;
}

//===== ConfigurationControllerResourceManager::ResourceComparator ============

bool ConfigurationControllerResourceManager::ResourceComparator::operator() (
    const rtl::Reference<ResourceId>& rxId1,
    const rtl::Reference<ResourceId>& rxId2) const
{
    if (rxId1.is() && rxId2.is())
        return rxId1->compareTo(rxId2)<0;
    else if (rxId1.is())
        return true;
    else
        return false;
}

} // end of namespace sd::framework

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
