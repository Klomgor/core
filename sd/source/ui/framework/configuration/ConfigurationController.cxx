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

#include <framework/ConfigurationController.hxx>
#include <framework/Configuration.hxx>
#include <framework/FrameworkHelper.hxx>
#include <framework/ResourceFactory.hxx>
#include <DrawController.hxx>
#include "ConfigurationUpdater.hxx"
#include "ConfigurationControllerBroadcaster.hxx"
#include "ConfigurationTracer.hxx"
#include "GenericConfigurationChangeRequest.hxx"
#include "ConfigurationControllerResourceManager.hxx"
#include "ResourceFactoryManager.hxx"
#include "UpdateRequest.hxx"
#include "ChangeRequestQueueProcessor.hxx"
#include "ConfigurationClassifier.hxx"
#include <ResourceId.hxx>
#include <com/sun/star/frame/XController.hpp>

#include <sal/log.hxx>
#include <osl/mutex.hxx>
#include <vcl/svapp.hxx>
#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::drawing::framework;
using ::sd::framework::FrameworkHelper;

namespace sd::framework {

//----- ConfigurationController::Implementation -------------------------------

class ConfigurationController::Implementation
{
public:
    Implementation (
        ConfigurationController& rController,
        const rtl::Reference<::sd::DrawController>& rxController);

    /** The Broadcaster class implements storing and calling of listeners.
    */
    std::shared_ptr<ConfigurationControllerBroadcaster> mpBroadcaster;

    /** The requested configuration which is modified (asynchronously) by
        calls to requestResourceActivation() and
        requestResourceDeactivation().  The mpConfigurationUpdater makes the
        current configuration reflect the content of this one.
    */
    rtl::Reference<sd::framework::Configuration> mxRequestedConfiguration;

    std::shared_ptr<ResourceFactoryManager> mpResourceFactoryContainer;

    std::shared_ptr<ConfigurationControllerResourceManager> mpResourceManager;

    std::shared_ptr<ConfigurationUpdater> mpConfigurationUpdater;

    /** The queue processor owns the queue of configuration change request
        objects and processes the objects.
    */
    std::unique_ptr<ChangeRequestQueueProcessor> mpQueueProcessor;

    std::shared_ptr<ConfigurationUpdaterLock> mpConfigurationUpdaterLock;

    sal_Int32 mnLockCount;
};

//===== ConfigurationController::Lock =========================================

ConfigurationController::Lock::Lock (const rtl::Reference<ConfigurationController>& rxController)
    : mxController(rxController)
{
    OSL_ASSERT(mxController.is());

    if (mxController.is())
        mxController->lock();
}

ConfigurationController::Lock::~Lock()
{
    if (mxController.is())
        suppress_fun_call_w_exception(mxController->unlock());
}

//===== ConfigurationController ===============================================

ConfigurationController::ConfigurationController(const rtl::Reference<::sd::DrawController>& rxController)
    : cppu::WeakComponentImplHelperBase(m_aMutex)
    , mbIsDisposed(false)
{
    const SolarMutexGuard aSolarGuard;

    mpImplementation.reset(new Implementation(
        *this,
        rxController));
}

ConfigurationController::~ConfigurationController() noexcept
{
}

void SAL_CALL ConfigurationController::disposing()
{
    if (mpImplementation == nullptr)
        return;

    SAL_INFO("sd.fwk", __func__ << ": ConfigurationController::disposing");
    SAL_INFO("sd.fwk", __func__ << ":     requesting empty configuration");
    // To destroy all resources an empty configuration is requested and then,
    // synchronously, all resulting requests are processed.
    mpImplementation->mpQueueProcessor->Clear();
    restoreConfiguration(new Configuration(this,false));
    mpImplementation->mpQueueProcessor->ProcessUntilEmpty();
    SAL_INFO("sd.fwk", __func__ << ":     all requests processed");

    // Now that all resources have been deactivated, mark the controller as
    // disposed.
    mbIsDisposed = true;

    // Release the listeners.
    lang::EventObject aEvent;
    aEvent.Source = uno::Reference<uno::XInterface>(static_cast<cppu::OWeakObject*>(this));

    {
        const SolarMutexGuard aSolarGuard;
        mpImplementation->mpBroadcaster->DisposeAndClear();
    }

    mpImplementation->mpQueueProcessor.reset();
    mpImplementation->mxRequestedConfiguration = nullptr;
    mpImplementation.reset();
}

void ConfigurationController::ProcessEvent()
{
    if (mpImplementation != nullptr)
    {
        OSL_ASSERT(mpImplementation->mpQueueProcessor != nullptr);

        mpImplementation->mpQueueProcessor->ProcessOneEvent();
    }
}

void ConfigurationController::RequestSynchronousUpdate()
{
    if (mpImplementation == nullptr)
        return;
    if (mpImplementation->mpQueueProcessor == nullptr)
        return;
    mpImplementation->mpQueueProcessor->ProcessUntilEmpty();
}

void ConfigurationController::addConfigurationChangeListener (
    const rtl::Reference<ConfigurationChangeListener>& rxListener,
    ConfigurationChangeEventType rsEventType)
{
    ::osl::MutexGuard aGuard (m_aMutex);

    ThrowIfDisposed();
    OSL_ASSERT(mpImplementation != nullptr);
    mpImplementation->mpBroadcaster->AddListener(rxListener, rsEventType);
}

void ConfigurationController::removeConfigurationChangeListener (
    const rtl::Reference<ConfigurationChangeListener>& rxListener)
{
    ::osl::MutexGuard aGuard (m_aMutex);

    ThrowIfDisposed();
    mpImplementation->mpBroadcaster->RemoveListener(rxListener);
}

void ConfigurationController::notifyEvent (
    const ConfigurationChangeEvent& rEvent)
{
    ThrowIfDisposed();
    mpImplementation->mpBroadcaster->NotifyListeners(rEvent);
}

void ConfigurationController::lock()
{
    OSL_ASSERT(mpImplementation != nullptr);
    OSL_ASSERT(mpImplementation->mpConfigurationUpdater != nullptr);

    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    ++mpImplementation->mnLockCount;
    if (mpImplementation->mpConfigurationUpdaterLock == nullptr)
        mpImplementation->mpConfigurationUpdaterLock
            = mpImplementation->mpConfigurationUpdater->GetLock();
}

void ConfigurationController::unlock()
{
    ::osl::MutexGuard aGuard (m_aMutex);

    // Allow unlocking while the ConfigurationController is being disposed
    // (but not when that is done and the controller is disposed.)
    if (rBHelper.bDisposed)
        ThrowIfDisposed();

    OSL_ASSERT(mpImplementation->mnLockCount>0);
    --mpImplementation->mnLockCount;
    if (mpImplementation->mnLockCount == 0)
        mpImplementation->mpConfigurationUpdaterLock.reset();
}

void ConfigurationController::requestResourceActivation (
    const rtl::Reference<ResourceId>& rxResourceId,
    ResourceActivationMode eMode)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    // Check whether we are being disposed.  This is handled differently
    // then being completely disposed because the first thing disposing()
    // does is to deactivate all remaining resources.  This is done via
    // regular methods which must not throw DisposedExceptions.  Therefore
    // we just return silently during that stage.
    if (rBHelper.bInDispose)
    {
        SAL_INFO("sd.fwk", __func__ << ": ConfigurationController::requestResourceActivation(): ignoring " <<
                FrameworkHelper::ResourceIdToString(rxResourceId));
        return;
    }

    SAL_INFO("sd.fwk", __func__ << ": ConfigurationController::requestResourceActivation() " <<
            FrameworkHelper::ResourceIdToString(rxResourceId));

    if (!rxResourceId.is())
        return;

    if (eMode == ResourceActivationMode::REPLACE)
    {
        // Get a list of the matching resources and create deactivation
        // requests for them.
        const std::vector<rtl::Reference<ResourceId> > aResourceList (
            mpImplementation->mxRequestedConfiguration->getResources(
                rxResourceId->getAnchor(),
                rxResourceId->getResourceTypePrefix(),
                AnchorBindingMode_DIRECT));

        for (const auto& rResource : aResourceList)
        {
            // Do not request the deactivation of the resource for which
            // this method was called.  Doing it would not change the
            // outcome but would result in unnecessary work.
            if (rxResourceId->compareTo(rResource) == 0)
                continue;

            // Request the deactivation of a resource and all resources
            // linked to it.
            requestResourceDeactivation(rResource);
        }
    }

    rtl::Reference<ConfigurationChangeRequest> xRequest(
        new GenericConfigurationChangeRequest(
            rxResourceId,
            GenericConfigurationChangeRequest::Activation));
    postChangeRequest(xRequest);
}

void ConfigurationController::requestResourceDeactivation (
    const rtl::Reference<ResourceId>& rxResourceId)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    SAL_INFO("sd.fwk", __func__ << ": ConfigurationController::requestResourceDeactivation() " <<
                FrameworkHelper::ResourceIdToString(rxResourceId));

    if (!rxResourceId.is())
        return;

    // Request deactivation of all resources linked to the specified one
    // as well.
    const std::vector<rtl::Reference<ResourceId> > aLinkedResources (
        mpImplementation->mxRequestedConfiguration->getResources(
            rxResourceId,
            u"",
            AnchorBindingMode_DIRECT));
    for (const auto& rLinkedResource : aLinkedResources)
    {
        // We do not add deactivation requests directly but call this
        // method recursively, so that when one time there are resources
        // linked to linked resources, these are handled correctly, too.
        requestResourceDeactivation(rLinkedResource);
    }

    // Add a deactivation request for the specified resource.
    rtl::Reference<ConfigurationChangeRequest> xRequest(
        new GenericConfigurationChangeRequest(
            rxResourceId,
            GenericConfigurationChangeRequest::Deactivation));
    postChangeRequest(xRequest);
}

rtl::Reference<AbstractResource> ConfigurationController::getResource (
    const rtl::Reference<ResourceId>& rxResourceId)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    ConfigurationControllerResourceManager::ResourceDescriptor aDescriptor (
        mpImplementation->mpResourceManager->GetResource(rxResourceId));
    return aDescriptor.mxResource;
}

void ConfigurationController::update()
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    if (mpImplementation->mpQueueProcessor->IsEmpty())
    {
        // The queue is empty.  Add another request that does nothing but
        // asynchronously trigger a request for an update.
        mpImplementation->mpQueueProcessor->AddRequest(new UpdateRequest());
    }
    else
    {
        // The queue is not empty, so we rely on the queue processor to
        // request an update automatically when the queue becomes empty.
    }
}

bool ConfigurationController::hasPendingRequests()
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    return ! mpImplementation->mpQueueProcessor->IsEmpty();
}

void ConfigurationController::postChangeRequest (
    const rtl::Reference<ConfigurationChangeRequest>& rxRequest)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    mpImplementation->mpQueueProcessor->AddRequest(rxRequest);
}

rtl::Reference<Configuration> ConfigurationController::getRequestedConfiguration()
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    if (mpImplementation->mxRequestedConfiguration.is())
        return mpImplementation->mxRequestedConfiguration->createClone();
    else
        return rtl::Reference<Configuration>();
}

rtl::Reference<Configuration> ConfigurationController::getCurrentConfiguration()
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    rtl::Reference<Configuration> xCurrentConfiguration(
        mpImplementation->mpConfigurationUpdater->GetCurrentConfiguration());
    if (xCurrentConfiguration.is())
        return xCurrentConfiguration->createClone();
    else
        return rtl::Reference<Configuration>();
}

/** The given configuration is restored by generating the appropriate set of
    activation and deactivation requests.
*/
void ConfigurationController::restoreConfiguration (
    const rtl::Reference<Configuration>& rxNewConfiguration)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();

    // We will probably be making a couple of activation and deactivation
    // requests so lock the configuration controller and let it later update
    // all changes at once.
    std::shared_ptr<ConfigurationUpdaterLock> pLock (
        mpImplementation->mpConfigurationUpdater->GetLock());

    // Get lists of resources that are to be activated or deactivated.
    rtl::Reference<Configuration> xCurrentConfiguration (mpImplementation->mxRequestedConfiguration);
#if OSL_DEBUG_LEVEL >=1
    SAL_INFO("sd.fwk", __func__ << ": ConfigurationController::restoreConfiguration(");
    ConfigurationTracer::TraceConfiguration(rxNewConfiguration, "requested configuration");
    ConfigurationTracer::TraceConfiguration(xCurrentConfiguration, "current configuration");
#endif
    ConfigurationClassifier aClassifier (rxNewConfiguration, xCurrentConfiguration);
    aClassifier.Partition();
#if DEBUG_SD_CONFIGURATION_TRACE
    aClassifier.TraceResourceIdVector(
        "requested but not current resources:\n", aClassifier.GetC1minusC2());
    aClassifier.TraceResourceIdVector(
        "current but not requested resources:\n", aClassifier.GetC2minusC1());
    aClassifier.TraceResourceIdVector(
        "requested and current resources:\n", aClassifier.GetC1andC2());
#endif

    // Request the deactivation of resources that are not requested in the
    // new configuration.
    const ConfigurationClassifier::ResourceIdVector& rResourcesToDeactivate (
        aClassifier.GetC2minusC1());
    for (const auto& rxResource : rResourcesToDeactivate)
    {
        requestResourceDeactivation(rxResource);
    }

    // Request the activation of resources that are requested in the
    // new configuration but are not part of the current configuration.
    const ConfigurationClassifier::ResourceIdVector& rResourcesToActivate (
        aClassifier.GetC1minusC2());
    for (const auto& rxResource : rResourcesToActivate)
    {
        requestResourceActivation(rxResource, ResourceActivationMode::ADD);
    }

    pLock.reset();
}

void ConfigurationController::addResourceFactory(
    const OUString& sResourceURL,
    const rtl::Reference<ResourceFactory>& rxResourceFactory)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();
    mpImplementation->mpResourceFactoryContainer->AddFactory(sResourceURL, rxResourceFactory);
}

void ConfigurationController::removeResourceFactoryForReference(
    const rtl::Reference<ResourceFactory>& rxResourceFactory)
{
    ::osl::MutexGuard aGuard (m_aMutex);
    ThrowIfDisposed();
    mpImplementation->mpResourceFactoryContainer->RemoveFactoryForReference(rxResourceFactory);
}

void ConfigurationController::ThrowIfDisposed () const
{
    if (mbIsDisposed)
    {
        throw lang::DisposedException (u"ConfigurationController object has already been disposed"_ustr,
            const_cast<uno::XWeak*>(static_cast<const uno::XWeak*>(this)));
    }

    if (mpImplementation == nullptr)
    {
        OSL_ASSERT(mpImplementation != nullptr);
        throw RuntimeException(u"ConfigurationController not initialized"_ustr,
            const_cast<uno::XWeak*>(static_cast<const uno::XWeak*>(this)));
    }
}

//===== ConfigurationController::Implementation ===============================

ConfigurationController::Implementation::Implementation (
    ConfigurationController& rController,
    const rtl::Reference<::sd::DrawController>& rxController)
    : mpBroadcaster(std::make_shared<ConfigurationControllerBroadcaster>(&rController)),
      mxRequestedConfiguration(new Configuration(&rController, true)),
      mpResourceFactoryContainer(std::make_shared<ResourceFactoryManager>(rxController)),
      mpResourceManager(
          std::make_shared<ConfigurationControllerResourceManager>(mpResourceFactoryContainer,mpBroadcaster)),
      mpConfigurationUpdater(
          std::make_shared<ConfigurationUpdater>(mpBroadcaster, mpResourceManager,rxController)),
      mpQueueProcessor(new ChangeRequestQueueProcessor(mpConfigurationUpdater)),
      mnLockCount(0)
{
    mpQueueProcessor->SetConfiguration(mxRequestedConfiguration);
}

} // end of namespace sd::framework


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
