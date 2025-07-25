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

#include <string_view>
#include <utility>
#include <unordered_map>

#include <properties.h>
#include <helper/mischelper.hxx>

#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/XProperty.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/util/XChangesNotifier.hpp>
#include <com/sun/star/util/PathSubstitution.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/util/XStringSubstitution.hpp>
#include <com/sun/star/util/XChangesListener.hpp>
#include <com/sun/star/util/XPathSettings.hpp>

#include <tools/urlobj.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ref.hxx>
#include <sal/log.hxx>

#include <comphelper/propshlp.hxx>
#include <comphelper/compbase.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/configurationhelper.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <unotools/configpaths.hxx>
#include <o3tl/string_view.hxx>

using namespace framework;

constexpr OUString CFGPROP_USERPATHS = u"UserPaths"_ustr;
constexpr OUString CFGPROP_WRITEPATH = u"WritePath"_ustr;

/*
    0 : old style              "Template"              string using ";" as separator
    1 : internal paths         "Template_internal"     string list
    2 : user paths             "Template_user"         string list
    3 : write path             "Template_write"        string
 */

constexpr OUString POSTFIX_INTERNAL_PATHS = u"_internal"_ustr;
constexpr OUString POSTFIX_USER_PATHS = u"_user"_ustr;
constexpr OUString POSTFIX_WRITE_PATH = u"_writable"_ustr;

namespace {

const sal_Int32 IDGROUP_OLDSTYLE        = 0;
const sal_Int32 IDGROUP_INTERNAL_PATHS = 1;
const sal_Int32 IDGROUP_USER_PATHS     = 2;
const sal_Int32 IDGROUP_WRITE_PATH      = 3;

const sal_Int32 IDGROUP_COUNT           = 4;

sal_Int32 impl_getPropGroup(sal_Int32 nID)
{
    return (nID % IDGROUP_COUNT);
}

/* enable it if you wish to migrate old user settings (using the old cfg schema) on demand...
   disable it in case only the new schema must be used.
 */

typedef ::comphelper::WeakComponentImplHelper<
            css::lang::XServiceInfo,
            css::lang::XInitialization,
            css::util::XChangesListener,    // => XEventListener
            css::util::XPathSettings>       // => XPropertySet
                PathSettings_BASE;

class PathSettings : public PathSettings_BASE
                   , public comphelper::OPropertySetHelper
{
    struct PathInfo
    {
        public:

            PathInfo()
                : bIsSinglePath (false)
                , bIsReadonly   (false)
            {}

            /// an internal name describing this path
            OUString sPathName;

            /// contains all paths, which are used internally - but are not visible for the user.
            std::vector<OUString> lInternalPaths;

            /// contains all paths configured by the user
            std::vector<OUString> lUserPaths;

            /// this special path is used to generate feature depending content there
            OUString sWritePath;

            /// indicates real single paths, which uses WritePath property only
            bool bIsSinglePath;

            /// simple handling of finalized/mandatory states ... => we know one state READONLY only .-)
            bool bIsReadonly;
    };

    typedef std::unordered_map<OUString, PathSettings::PathInfo> PathHash;

    enum EChangeOp
    {
        E_UNDEFINED,
        E_ADDED,
        E_CHANGED,
        E_REMOVED
    };

private:

    /** reference to factory, which has create this instance. */
    css::uno::Reference< css::uno::XComponentContext > m_xContext;

    /** list of all path variables and her corresponding values. */
    PathSettings::PathHash m_lPaths;

    /** describes all properties available on our interface.
        Will be generated on demand based on our path list m_lPaths. */
    css::uno::Sequence< css::beans::Property > m_lPropDesc;

    /** helper needed to (re-)substitute all internal save path values. */
    css::uno::Reference< css::util::XStringSubstitution > m_xSubstitution;

    /** provides access to the old configuration schema (which will be migrated on demand). */
    css::uno::Reference< css::container::XNameAccess > m_xCfgOld;

    /** provides access to the new configuration schema. */
    css::uno::Reference< css::container::XNameAccess > m_xCfgNew;

    /** helper to listen for configuration changes without ownership cycle problems */
    rtl::Reference< WeakChangesListener > m_xCfgNewListener;

    std::unique_ptr<::cppu::OPropertyArrayHelper> m_pPropHelp;

public:

    /** initialize a new instance of this class.
        Attention: It's necessary for right function of this class, that the order of base
        classes is the right one. Because we transfer information from one base to another
        during this ctor runs! */
    explicit PathSettings(css::uno::Reference< css::uno::XComponentContext >  xContext);

    /** free all used resources ... if it was not already done. */
    virtual ~PathSettings() override;

    virtual OUString SAL_CALL getImplementationName() override
    {
        return u"com.sun.star.comp.framework.PathSettings"_ustr;
    }

    virtual sal_Bool SAL_CALL supportsService(OUString const & ServiceName) override
    {
        return cppu::supportsService(this, ServiceName);
    }

    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override
    {
        return {u"com.sun.star.util.PathSettings"_ustr};
    }

    // XInterface
    virtual css::uno::Any SAL_CALL queryInterface( const css::uno::Type& type) override;
    virtual void SAL_CALL acquire() noexcept override
        { OWeakObject::acquire(); }
    virtual void SAL_CALL release() noexcept override
        { OWeakObject::release(); }

    // XTypeProvider
    virtual css::uno::Sequence< css::uno::Type > SAL_CALL getTypes(  ) override;

    // css::util::XChangesListener
    virtual void SAL_CALL changesOccurred(const css::util::ChangesEvent& aEvent) override;

    // css::lang::XEventListener
    virtual void SAL_CALL disposing(const css::lang::EventObject& aSource) override;

    /**
     * XPathSettings attribute methods
     */
    virtual OUString SAL_CALL getAddin() override
        { return getStringProperty(u"Addin"_ustr); }
    virtual void SAL_CALL setAddin(const OUString& p1) override
        { setStringProperty(u"Addin"_ustr, p1); }
    virtual OUString SAL_CALL getAutoCorrect() override
        { return getStringProperty(u"AutoCorrect"_ustr); }
    virtual void SAL_CALL setAutoCorrect(const OUString& p1) override
        { setStringProperty(u"AutoCorrect"_ustr, p1); }
    virtual OUString SAL_CALL getAutoText() override
        { return getStringProperty(u"AutoText"_ustr); }
    virtual void SAL_CALL setAutoText(const OUString& p1) override
        { setStringProperty(u"AutoText"_ustr, p1); }
    virtual OUString SAL_CALL getBackup() override
        { return getStringProperty(u"Backup"_ustr); }
    virtual void SAL_CALL setBackup(const OUString& p1) override
        { setStringProperty(u"Backup"_ustr, p1); }
    virtual OUString SAL_CALL getBasic() override
        { return getStringProperty(u"Basic"_ustr); }
    virtual void SAL_CALL setBasic(const OUString& p1) override
        { setStringProperty(u"Basic"_ustr, p1); }
    virtual OUString SAL_CALL getBitmap() override
        { return getStringProperty(u"Bitmap"_ustr); }
    virtual void SAL_CALL setBitmap(const OUString& p1) override
        { setStringProperty(u"Bitmap"_ustr, p1); }
    virtual OUString SAL_CALL getConfig() override
        { return getStringProperty(u"Config"_ustr); }
    virtual void SAL_CALL setConfig(const OUString& p1) override
        { setStringProperty(u"Config"_ustr, p1); }
    virtual OUString SAL_CALL getDictionary() override
        { return getStringProperty(u"Dictionary"_ustr); }
    virtual void SAL_CALL setDictionary(const OUString& p1) override
        { setStringProperty(u"Dictionary"_ustr, p1); }
    virtual OUString SAL_CALL getFavorite() override
        { return getStringProperty(u"Favorite"_ustr); }
    virtual void SAL_CALL setFavorite(const OUString& p1) override
        { setStringProperty(u"Favorite"_ustr, p1); }
    virtual OUString SAL_CALL getFilter() override
        { return getStringProperty(u"Filter"_ustr); }
    virtual void SAL_CALL setFilter(const OUString& p1) override
        { setStringProperty(u"Filter"_ustr, p1); }
    virtual OUString SAL_CALL getGallery() override
        { return getStringProperty(u"Gallery"_ustr); }
    virtual void SAL_CALL setGallery(const OUString& p1) override
        { setStringProperty(u"Gallery"_ustr, p1); }
    virtual OUString SAL_CALL getGraphic() override
        { return getStringProperty(u"Graphic"_ustr); }
    virtual void SAL_CALL setGraphic(const OUString& p1) override
        { setStringProperty(u"Graphic"_ustr, p1); }
    virtual OUString SAL_CALL getHelp() override
        { return getStringProperty(u"Help"_ustr); }
    virtual void SAL_CALL setHelp(const OUString& p1) override
        { setStringProperty(u"Help"_ustr, p1); }
    virtual OUString SAL_CALL getLinguistic() override
        { return getStringProperty(u"Linguistic"_ustr); }
    virtual void SAL_CALL setLinguistic(const OUString& p1) override
        { setStringProperty(u"Linguistic"_ustr, p1); }
    virtual OUString SAL_CALL getModule() override
        { return getStringProperty(u"Module"_ustr); }
    virtual void SAL_CALL setModule(const OUString& p1) override
        { setStringProperty(u"Module"_ustr, p1); }
    virtual OUString SAL_CALL getPalette() override
        { return getStringProperty(u"Palette"_ustr); }
    virtual void SAL_CALL setPalette(const OUString& p1) override
        { setStringProperty(u"Palette"_ustr, p1); }
    virtual OUString SAL_CALL getPlugin() override
        { return getStringProperty(u"Plugin"_ustr); }
    virtual void SAL_CALL setPlugin(const OUString& p1) override
        { setStringProperty(u"Plugin"_ustr, p1); }
    virtual OUString SAL_CALL getStorage() override
        { return getStringProperty(u"Storage"_ustr); }
    virtual void SAL_CALL setStorage(const OUString& p1) override
        { setStringProperty(u"Storage"_ustr, p1); }
    virtual OUString SAL_CALL getTemp() override
        { return getStringProperty(u"Temp"_ustr); }
    virtual void SAL_CALL setTemp(const OUString& p1) override
        { setStringProperty(u"Temp"_ustr, p1); }
    virtual OUString SAL_CALL getTemplate() override
        { return getStringProperty(u"Template"_ustr); }
    virtual void SAL_CALL setTemplate(const OUString& p1) override
        { setStringProperty(u"Template"_ustr, p1); }
    virtual OUString SAL_CALL getUIConfig() override
        { return getStringProperty(u"UIConfig"_ustr); }
    virtual void SAL_CALL setUIConfig(const OUString& p1) override
        { setStringProperty(u"UIConfig"_ustr, p1); }
    virtual OUString SAL_CALL getUserConfig() override
        { return getStringProperty(u"UserConfig"_ustr); }
    virtual void SAL_CALL setUserConfig(const OUString& p1) override
        { setStringProperty(u"UserConfig"_ustr, p1); }
    virtual OUString SAL_CALL getUserDictionary() override
        { return getStringProperty(u"UserDictionary"_ustr); }
    virtual void SAL_CALL setUserDictionary(const OUString& p1) override
        { setStringProperty(u"UserDictionary"_ustr, p1); }
    virtual OUString SAL_CALL getWork() override
        { return getStringProperty(u"Work"_ustr); }
    virtual void SAL_CALL setWork(const OUString& p1) override
        { setStringProperty(u"Work"_ustr, p1); }
    virtual OUString SAL_CALL getBasePathShareLayer() override
        { return getStringProperty(u"UIConfig"_ustr); }
    virtual void SAL_CALL setBasePathShareLayer(const OUString& p1) override
        { setStringProperty(u"UIConfig"_ustr, p1); }
    virtual OUString SAL_CALL getBasePathUserLayer() override
        { return getStringProperty(u"UserConfig"_ustr); }
    virtual void SAL_CALL setBasePathUserLayer(const OUString& p1) override
        { setStringProperty(u"UserConfig"_ustr, p1); }

    /**
     * overrides to resolve inheritance ambiguity
     */
    virtual void SAL_CALL setPropertyValue(const OUString& p1, const css::uno::Any& p2) override
        { ::comphelper::OPropertySetHelper::setPropertyValue(p1, p2); }
    virtual css::uno::Any SAL_CALL getPropertyValue(const OUString& p1) override
        { return ::comphelper::OPropertySetHelper::getPropertyValue(p1); }
    virtual void SAL_CALL addPropertyChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XPropertyChangeListener>& p2) override
        { ::comphelper::OPropertySetHelper::addPropertyChangeListener(p1, p2); }
    virtual void SAL_CALL removePropertyChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XPropertyChangeListener>& p2) override
        { ::comphelper::OPropertySetHelper::removePropertyChangeListener(p1, p2); }
    virtual void SAL_CALL addVetoableChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XVetoableChangeListener>& p2) override
        { ::comphelper::OPropertySetHelper::addVetoableChangeListener(p1, p2); }
    virtual void SAL_CALL removeVetoableChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XVetoableChangeListener>& p2) override
        { ::comphelper::OPropertySetHelper::removeVetoableChangeListener(p1, p2); }

    // XInitialization
    virtual void SAL_CALL initialize(const css::uno::Sequence<css::uno::Any>& rArguments) override;

    /** read all configured paths and create all needed internal structures. */
    void readAll();

private:
    virtual void disposing(std::unique_lock<std::mutex>& g) final override;

    /** read all configured paths and create all needed internal structures. */
    void impl_readAll(std::unique_lock<std::mutex>&);

    /// @throws css::uno::RuntimeException
    OUString getStringProperty(const OUString& p1);

    /// @throws css::uno::RuntimeException
    void setStringProperty(const OUString& p1, const OUString& p2);

    /** read a path info using the old cfg schema.
        This is needed for "migration on demand" reasons only.
        Can be removed for next major release .-) */
    std::vector<OUString> impl_readOldFormat(std::unique_lock<std::mutex>& g, const OUString& sPath);

    /** read a path info using the new cfg schema. */
    PathSettings::PathInfo impl_readNewFormat(std::unique_lock<std::mutex>& g, const OUString& sPath);

    /** filter "real user defined paths" from the old configuration schema
        and set it as UserPaths on the new schema.
        Can be removed with new major release ... */
    static void impl_mergeOldUserPaths(      PathSettings::PathInfo& rPath,
                                 const std::vector<OUString>& lOld );

    /** reload one path directly from the new configuration schema (because
        it was updated by any external code) */
    PathSettings::EChangeOp impl_updatePath(std::unique_lock<std::mutex>& g,
                                            const OUString& sPath          ,
                                            bool         bNotifyListener);

    /** replace all might existing placeholder variables inside the given path ...
        or check if the given path value uses paths, which can be replaced with predefined
        placeholder variables ...
     */
    static void impl_subst(std::vector<OUString>& lVals   ,
                    const css::uno::Reference< css::util::XStringSubstitution >& xSubst  ,
                          bool                                               bReSubst);

    void impl_subst(std::unique_lock<std::mutex>& g,
                    PathSettings::PathInfo& aPath   ,
                    bool                bReSubst);

    /** converts our new string list schema to the old ";" separated schema ... */
    static OUString impl_convertPath2OldStyle(const PathSettings::PathInfo& rPath );
    static std::vector<OUString> impl_convertOldStyle2Path(std::u16string_view sOldStylePath);

    /** remove still known paths from the given lList argument.
        So real user defined paths can be extracted from the list of
        fix internal paths !
     */
    static void impl_purgeKnownPaths(PathSettings::PathInfo& rPath,
                              std::vector<OUString>& lList);

    /** rebuild the member m_lPropDesc using the path list m_lPaths. */
    void impl_rebuildPropertyDescriptor(std::unique_lock<std::mutex>& g);

    /** provides direct access to the list of path values
        using its internal property id.
     */
    css::uno::Any impl_getPathValue(std::unique_lock<std::mutex>& g, sal_Int32 nID ) const;
    void          impl_setPathValue(std::unique_lock<std::mutex>& g, sal_Int32 nID,
                                    const css::uno::Any& aVal);

    /** check the given handle and return the corresponding PathInfo reference.
        These reference can be used then directly to manipulate these path. */
          PathSettings::PathInfo* impl_getPathAccess     (std::unique_lock<std::mutex>& g, sal_Int32 nHandle);
    const PathSettings::PathInfo* impl_getPathAccessConst(std::unique_lock<std::mutex>& g, sal_Int32 nHandle) const;

    /** it checks, if the given path value seems to be a valid URL or system path. */
    static bool impl_isValidPath(std::u16string_view sPath);
    static bool impl_isValidPath(const std::vector<OUString>& lPath);

    void impl_storePath(std::unique_lock<std::mutex>& g, const PathSettings::PathInfo& aPath);

    css::uno::Sequence< sal_Int32 > impl_mapPathName2IDList(std::u16string_view sPath);

    void impl_notifyPropListener( std::unique_lock<std::mutex>& g,
                                  std::u16string_view    sPath   ,
                                  const PathSettings::PathInfo* pPathOld,
                                  const PathSettings::PathInfo* pPathNew);

    //  OPropertySetHelper
    virtual bool convertFastPropertyValue( std::unique_lock<std::mutex>& g, css::uno::Any&  aConvertedValue,
            css::uno::Any& aOldValue,
            sal_Int32 nHandle,
            const css::uno::Any& aValue ) override;
    virtual void setFastPropertyValue_NoBroadcast( std::unique_lock<std::mutex>& g, sal_Int32 nHandle,
            const css::uno::Any&  aValue ) override;
    virtual void getFastPropertyValue( std::unique_lock<std::mutex>& g, css::uno::Any&  aValue,
            sal_Int32 nHandle ) const override;
    // Avoid:
    // warning: 'virtual css::uno::Any cppu::OPropertySetHelper::getFastPropertyValue(sal_Int32)' was hidden [-Woverloaded-virtual]
    // warning:   by ‘virtual void {anonymous}::PathSettings::getFastPropertyValue(css::uno::Any&, sal_Int32) const’ [-Woverloaded-virtual]
    using comphelper::OPropertySetHelper::getFastPropertyValue;
    virtual ::cppu::IPropertyArrayHelper& getInfoHelper() override;
    virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL getPropertySetInfo() override;

    /** factory methods to guarantee right (but on demand) initialized members ... */
    css::uno::Reference< css::util::XStringSubstitution > fa_getSubstitution(std::unique_lock<std::mutex>& g);
    css::uno::Reference< css::container::XNameAccess >    fa_getCfgOld(std::unique_lock<std::mutex>& g);
    css::uno::Reference< css::container::XNameAccess >    fa_getCfgNew(std::unique_lock<std::mutex>& g);
};

PathSettings::PathSettings( css::uno::Reference< css::uno::XComponentContext >  xContext )
    : PathSettings_BASE()
    , comphelper::OPropertySetHelper()
    ,   m_xContext (std::move(xContext))
{
}

PathSettings::~PathSettings()
{
    std::unique_lock g(m_aMutex);
    disposing(g);
}

void PathSettings::disposing(std::unique_lock<std::mutex>& /*g*/)
{
    css::uno::Reference< css::util::XChangesNotifier >
        xBroadcaster(m_xCfgNew, css::uno::UNO_QUERY);
    if (xBroadcaster.is())
        xBroadcaster->removeChangesListener(m_xCfgNewListener);

    m_xSubstitution.clear();
    m_xCfgOld.clear();
    m_xCfgNew.clear();
    m_xCfgNewListener.clear();

    m_pPropHelp.reset();
}

css::uno::Any SAL_CALL PathSettings::queryInterface( const css::uno::Type& _rType )
{
    css::uno::Any aRet = PathSettings_BASE::queryInterface( _rType );
    if ( !aRet.hasValue() )
        aRet = ::comphelper::OPropertySetHelper::queryInterface( _rType );
    return aRet;
}

css::uno::Sequence< css::uno::Type > SAL_CALL PathSettings::getTypes(  )
{
    return comphelper::concatSequences(
        PathSettings_BASE::getTypes(),
        ::comphelper::OPropertySetHelper::getTypes()
    );
}

void SAL_CALL PathSettings::changesOccurred(const css::util::ChangesEvent& aEvent)
{
    std::unique_lock g(m_aMutex);

    sal_Int32 c                 = aEvent.Changes.getLength();
    sal_Int32 i                 = 0;
    bool  bUpdateDescriptor = false;

    for (i=0; i<c; ++i)
    {
        const css::util::ElementChange& aChange = aEvent.Changes[i];

        OUString sChanged;
        aChange.Accessor >>= sChanged;

        OUString sPath = ::utl::extractFirstFromConfigurationPath(sChanged);
        if (!sPath.isEmpty())
        {
            PathSettings::EChangeOp eOp = impl_updatePath(g, sPath, true);
            if (
                (eOp == PathSettings::E_ADDED  ) ||
                (eOp == PathSettings::E_REMOVED)
               )
                bUpdateDescriptor = true;
        }
    }

    if (bUpdateDescriptor)
        impl_rebuildPropertyDescriptor(g);
}

void SAL_CALL PathSettings::disposing(const css::lang::EventObject& aSource)
{
    std::unique_lock g(m_aMutex);

    if (aSource.Source == m_xCfgNew)
        m_xCfgNew.clear();
}

OUString PathSettings::getStringProperty(const OUString& p1)
{
    css::uno::Any a = ::comphelper::OPropertySetHelper::getPropertyValue(p1);
    OUString s;
    a >>= s;
    return s;
}

void PathSettings::setStringProperty(const OUString& p1, const OUString& p2)
{
    ::comphelper::OPropertySetHelper::setPropertyValue(p1, css::uno::Any(p2));
}

void PathSettings::readAll()
{
    std::unique_lock g(m_aMutex);
    impl_readAll(g);
}

void PathSettings::impl_readAll(std::unique_lock<std::mutex>& g)
{
    try
    {
        // TODO think about me
        css::uno::Reference< css::container::XNameAccess > xCfg    = fa_getCfgNew(g);
        css::uno::Sequence< OUString >              lPaths = xCfg->getElementNames();

        sal_Int32 c = lPaths.getLength();
        for (sal_Int32 i = 0; i < c; ++i)
        {
            const OUString& sPath = lPaths[i];
            impl_updatePath(g, sPath, false);
        }
    }
    catch(const css::uno::RuntimeException& )
    {
    }

    impl_rebuildPropertyDescriptor(g);
}

// NO substitution here ! It's done outside ...
std::vector<OUString> PathSettings::impl_readOldFormat(std::unique_lock<std::mutex>& g, const OUString& sPath)
{
    css::uno::Reference< css::container::XNameAccess > xCfg( fa_getCfgOld(g) );
    std::vector<OUString> aPathVal;

    if( xCfg->hasByName(sPath) )
    {
        css::uno::Any aVal( xCfg->getByName(sPath) );

        OUString                       sStringVal;
        css::uno::Sequence< OUString > lStringListVal;

        if (aVal >>= sStringVal)
        {
            aPathVal.push_back(sStringVal);
        }
        else if (aVal >>= lStringListVal)
        {
            aPathVal = comphelper::sequenceToContainer<std::vector<OUString>>(lStringListVal);
        }
    }

    return aPathVal;
}

// NO substitution here ! It's done outside ...
PathSettings::PathInfo PathSettings::impl_readNewFormat(std::unique_lock<std::mutex>& g, const OUString& sPath)
{
    css::uno::Reference< css::container::XNameAccess > xCfg = fa_getCfgNew(g);

    // get access to the "queried" path
    css::uno::Reference< css::container::XNameAccess > xPath;
    xCfg->getByName(sPath) >>= xPath;

    PathSettings::PathInfo aPathVal;

    // read internal path list
    css::uno::Reference< css::container::XNameAccess > xIPath;
    xPath->getByName(u"InternalPaths"_ustr) >>= xIPath;
    aPathVal.lInternalPaths = comphelper::sequenceToContainer<std::vector<OUString>>(xIPath->getElementNames());

    // read user defined path list
    css::uno::Sequence<OUString> vTmpUserPathsSeq;
    xPath->getByName(CFGPROP_USERPATHS) >>= vTmpUserPathsSeq;
    aPathVal.lUserPaths = comphelper::sequenceToContainer<std::vector<OUString>>(vTmpUserPathsSeq);

    // read the writeable path
    xPath->getByName(CFGPROP_WRITEPATH) >>= aPathVal.sWritePath;

    // avoid duplicates, by removing the writeable path from
    // the user defined path list if it happens to be there too
    std::vector<OUString>::iterator aI = std::find(aPathVal.lUserPaths.begin(), aPathVal.lUserPaths.end(), aPathVal.sWritePath);
    if (aI != aPathVal.lUserPaths.end())
        aPathVal.lUserPaths.erase(aI);

    // read state props
    xPath->getByName(u"IsSinglePath"_ustr) >>= aPathVal.bIsSinglePath;

    // analyze finalized/mandatory states
    aPathVal.bIsReadonly = false;
    css::uno::Reference< css::beans::XProperty > xInfo(xPath, css::uno::UNO_QUERY);
    if (xInfo.is())
    {
        css::beans::Property aInfo = xInfo->getAsProperty();
        bool bFinalized = ((aInfo.Attributes & css::beans::PropertyAttribute::READONLY  ) == css::beans::PropertyAttribute::READONLY  );

        // Note: 'till we support finalized/mandatory on our API more in detail we handle
        // all states simple as READONLY! But because all really needed paths are "mandatory" by default
        // we have to handle "finalized" as the real "readonly" indicator.
        aPathVal.bIsReadonly = bFinalized;
    }

    return aPathVal;
}

void PathSettings::impl_storePath(std::unique_lock<std::mutex>& g, const PathSettings::PathInfo& aPath)
{
    css::uno::Reference< css::container::XNameAccess > xCfgNew = fa_getCfgNew(g);
    css::uno::Reference< css::container::XNameAccess > xCfgOld = fa_getCfgOld(g);

    // try to replace path-parts with well known and supported variables.
    // So an office can be moved easily to another location without losing
    // its related paths.
    PathInfo aResubstPath(aPath);
    impl_subst(g, aResubstPath, true);

    // unlock because writeRelativeKey and friends might trigger a listener which calls back into us
    g.unlock();

    // update new configuration
    if (! aResubstPath.bIsSinglePath)
    {
        ::comphelper::ConfigurationHelper::writeRelativeKey(xCfgNew,
                                                            aResubstPath.sPathName,
                                                            CFGPROP_USERPATHS,
                            css::uno::Any(comphelper::containerToSequence(aResubstPath.lUserPaths)));
    }

    ::comphelper::ConfigurationHelper::writeRelativeKey(xCfgNew,
                                                        aResubstPath.sPathName,
                                                        CFGPROP_WRITEPATH,
                                                        css::uno::Any(aResubstPath.sWritePath));

    ::comphelper::ConfigurationHelper::flush(xCfgNew);

    // remove the whole path from the old configuration!
    // Otherwise we can't make sure that the diff between new and old configuration
    // on loading time really represents a user setting!!!

    // Check if the given path exists inside the old configuration.
    // Because our new configuration knows more than the list of old paths ... !
    if (xCfgOld->hasByName(aResubstPath.sPathName))
    {
        css::uno::Reference< css::beans::XPropertySet > xProps(xCfgOld, css::uno::UNO_QUERY_THROW);
        xProps->setPropertyValue(aResubstPath.sPathName, css::uno::Any());
        ::comphelper::ConfigurationHelper::flush(xCfgOld);
    }

    g.lock();
}

// static
void PathSettings::impl_mergeOldUserPaths(      PathSettings::PathInfo& rPath,
                                          const std::vector<OUString>& lOld )
{
    for (auto const& old : lOld)
    {
        if (rPath.bIsSinglePath)
        {
            SAL_WARN_IF(lOld.size()>1, "fwk", "PathSettings::impl_mergeOldUserPaths(): Single path has more than one path value inside old configuration (Common.xcu)!");
            if ( rPath.sWritePath != old )
               rPath.sWritePath = old;
        }
        else
        {
            if (
                (  std::find(rPath.lInternalPaths.begin(), rPath.lInternalPaths.end(), old) == rPath.lInternalPaths.end()) &&
                (  std::find(rPath.lUserPaths.begin(), rPath.lUserPaths.end(), old)     == rPath.lUserPaths.end()    ) &&
                (  rPath.sWritePath != old                                     )
               )
               rPath.lUserPaths.push_back(old);
        }
    }
}

PathSettings::EChangeOp PathSettings::impl_updatePath(std::unique_lock<std::mutex>& g,
                                                      const OUString& sPath          ,
                                                      bool         bNotifyListener)
{
    // SAFE ->

    PathSettings::PathInfo* pPathOld = nullptr;
    PathSettings::PathInfo* pPathNew = nullptr;
    PathSettings::EChangeOp eOp      = PathSettings::E_UNDEFINED;
    PathSettings::PathInfo  aPath;

    try
    {
        aPath = impl_readNewFormat(g, sPath);
        aPath.sPathName = sPath;
        // replace all might existing variables with real values
        // Do it before these old paths will be compared against the
        // new path configuration. Otherwise some strings uses different variables ... but substitution
        // will produce strings with same content (because some variables are redundant!)
        impl_subst(g, aPath, false);
    }
    catch(const css::uno::RuntimeException&)
        { throw; }
    catch(const css::container::NoSuchElementException&)
        { eOp = PathSettings::E_REMOVED; }
    catch(const css::uno::Exception&)
        { throw; }

    try
    {
        // migration of old user defined values on demand
        // can be disabled for a new major
        std::vector<OUString> lOldVals = impl_readOldFormat(g, sPath);
        // replace all might existing variables with real values
        // Do it before these old paths will be compared against the
        // new path configuration. Otherwise some strings uses different variables ... but substitution
        // will produce strings with same content (because some variables are redundant!)
        impl_subst(lOldVals, fa_getSubstitution(g), false);
        impl_mergeOldUserPaths(aPath, lOldVals);
    }
    catch(const css::uno::RuntimeException&)
        { throw; }
    // Normal(!) exceptions can be ignored!
    // E.g. in case an addon installs a new path, which was not well known for an OOo 1.x installation
    // we can't find a value for it inside the "old" configuration. So a NoSuchElementException
    // will be normal .-)
    catch(const css::uno::Exception&)
        {}

    PathSettings::PathHash::iterator pPath = m_lPaths.find(sPath);
    if (eOp == PathSettings::E_UNDEFINED)
    {
        if (pPath != m_lPaths.end())
            eOp = PathSettings::E_CHANGED;
        else
            eOp = PathSettings::E_ADDED;
    }

    switch(eOp)
    {
        case PathSettings::E_ADDED :
             {
                if (bNotifyListener)
                {
                    pPathOld = nullptr;
                    pPathNew = &aPath;
                    impl_notifyPropListener(g, sPath, pPathOld, pPathNew);
                }
                m_lPaths[sPath] = aPath;
             }
             break;

        case PathSettings::E_CHANGED :
             {
                if (bNotifyListener)
                {
                    pPathOld = &(pPath->second);
                    pPathNew = &aPath;
                    impl_notifyPropListener(g, sPath, pPathOld, pPathNew);
                }
                m_lPaths[sPath] = std::move(aPath);
             }
             break;

        case PathSettings::E_REMOVED :
             {
                if (pPath != m_lPaths.end())
                {
                    if (bNotifyListener)
                    {
                        pPathOld = &(pPath->second);
                        pPathNew = nullptr;
                        impl_notifyPropListener(g, sPath, pPathOld, pPathNew);
                    }
                    m_lPaths.erase(pPath);
                }
             }
             break;

        default: // to let compiler be happy
             break;
    }

    return eOp;
}

css::uno::Sequence< sal_Int32 > PathSettings::impl_mapPathName2IDList(std::u16string_view sPath)
{
    OUString sInternalProp = OUString::Concat(sPath)+POSTFIX_INTERNAL_PATHS;
    OUString sUserProp     = OUString::Concat(sPath)+POSTFIX_USER_PATHS;
    OUString sWriteProp    = OUString::Concat(sPath)+POSTFIX_WRITE_PATH;

    // Attention: The default set of IDs is fix and must follow these schema.
    // Otherwise the outside code ant work for new added properties.
    // Why?
    // The outside code must fire N events for every changed property.
    // And the knowing about packaging of variables of the structure PathInfo
    // follow these group IDs! But if such ID is not in the range of [0..IDGROUP_COUNT]
    // the outside can't determine the right group ... and can not fire the right events .-)

    css::uno::Sequence<sal_Int32> lIDs{ IDGROUP_OLDSTYLE, IDGROUP_INTERNAL_PATHS,
                                        IDGROUP_USER_PATHS, IDGROUP_WRITE_PATH };
    assert(lIDs.getLength() == IDGROUP_COUNT);
    auto plIDs = lIDs.getArray();

    sal_Int32 c = m_lPropDesc.getLength();
    sal_Int32 i = 0;
    for (i=0; i<c; ++i)
    {
        const css::beans::Property& rProp = m_lPropDesc[i];

        if (rProp.Name == sPath)
            plIDs[IDGROUP_OLDSTYLE] = rProp.Handle;
        else
        if (rProp.Name == sInternalProp)
            plIDs[IDGROUP_INTERNAL_PATHS] = rProp.Handle;
        else
        if (rProp.Name == sUserProp)
            plIDs[IDGROUP_USER_PATHS] = rProp.Handle;
        else
        if (rProp.Name == sWriteProp)
            plIDs[IDGROUP_WRITE_PATH] = rProp.Handle;
    }

    return lIDs;
}

void PathSettings::impl_notifyPropListener( std::unique_lock<std::mutex>& g,
                                            std::u16string_view           sPath,
                                            const PathSettings::PathInfo* pPathOld,
                                            const PathSettings::PathInfo* pPathNew)
{
    css::uno::Sequence< sal_Int32 >     lHandles(1);
    auto plHandles = lHandles.getArray();
    css::uno::Sequence< css::uno::Any > lOldVals(1);
    auto plOldVals = lOldVals.getArray();
    css::uno::Sequence< css::uno::Any > lNewVals(1);
    auto plNewVals = lNewVals.getArray();

    css::uno::Sequence< sal_Int32 > lIDs   = impl_mapPathName2IDList(sPath);
    sal_Int32                       c      = lIDs.getLength();
    sal_Int32                       i      = 0;
    sal_Int32                       nMaxID = m_lPropDesc.getLength()-1;
    for (i=0; i<c; ++i)
    {
        sal_Int32 nID = lIDs[i];

        if (
            (nID < 0     ) ||
            (nID > nMaxID)
           )
           continue;

        plHandles[0] = nID;
        switch(impl_getPropGroup(nID))
        {
            case IDGROUP_OLDSTYLE :
                 {
                    if (pPathOld)
                    {
                        OUString sVal = impl_convertPath2OldStyle(*pPathOld);
                        plOldVals[0] <<= sVal;
                    }
                    if (pPathNew)
                    {
                        OUString sVal = impl_convertPath2OldStyle(*pPathNew);
                        plNewVals[0] <<= sVal;
                    }
                 }
                 break;

            case IDGROUP_INTERNAL_PATHS :
                 {
                    if (pPathOld)
                        plOldVals[0] <<= comphelper::containerToSequence(pPathOld->lInternalPaths);
                    if (pPathNew)
                        plNewVals[0] <<= comphelper::containerToSequence(pPathNew->lInternalPaths);
                 }
                 break;

            case IDGROUP_USER_PATHS :
                 {
                    if (pPathOld)
                        plOldVals[0] <<= comphelper::containerToSequence(pPathOld->lUserPaths);
                    if (pPathNew)
                        plNewVals[0] <<= comphelper::containerToSequence(pPathNew->lUserPaths);
                 }
                 break;

            case IDGROUP_WRITE_PATH :
                 {
                    if (pPathOld)
                        plOldVals[0] <<= pPathOld->sWritePath;
                    if (pPathNew)
                        plNewVals[0] <<= pPathNew->sWritePath;
                 }
                 break;
        }

        fire(g,
             plHandles,
             plNewVals,
             plOldVals,
             1,
             false);
    }
}

// static
void PathSettings::impl_subst(std::vector<OUString>& lVals   ,
                              const css::uno::Reference< css::util::XStringSubstitution >& xSubst  ,
                                    bool                                               bReSubst)
{
    for (auto & old : lVals)
    {
        OUString  sNew;
        if (bReSubst)
            sNew = xSubst->reSubstituteVariables(old);
        else
            sNew = xSubst->substituteVariables(old, false);

        old = sNew;
    }
}

void PathSettings::impl_subst(std::unique_lock<std::mutex>& g,
                              PathSettings::PathInfo& aPath   ,
                              bool                bReSubst)
{
    css::uno::Reference< css::util::XStringSubstitution > xSubst = fa_getSubstitution(g);

    impl_subst(aPath.lInternalPaths, xSubst, bReSubst);
    impl_subst(aPath.lUserPaths    , xSubst, bReSubst);
    if (bReSubst)
        aPath.sWritePath = xSubst->reSubstituteVariables(aPath.sWritePath);
    else
        aPath.sWritePath = xSubst->substituteVariables(aPath.sWritePath, false);
}

// static
OUString PathSettings::impl_convertPath2OldStyle(const PathSettings::PathInfo& rPath)
{
    OUStringBuffer sPathVal(256);

    for (auto const& internalPath : rPath.lInternalPaths)
    {
        if (sPathVal.getLength())
            sPathVal.append(";");
        sPathVal.append(internalPath);
    }
    for (auto const& userPath : rPath.lUserPaths)
    {
        if (sPathVal.getLength())
            sPathVal.append(";");
        sPathVal.append(userPath);
    }
    if (!rPath.sWritePath.isEmpty())
    {
        if (sPathVal.getLength())
            sPathVal.append(";");
        sPathVal.append(rPath.sWritePath);
    }

    return sPathVal.makeStringAndClear();
}

// static
std::vector<OUString> PathSettings::impl_convertOldStyle2Path(std::u16string_view sOldStylePath)
{
    std::vector<OUString> lList;
    sal_Int32    nToken = 0;
    do
    {
        OUString sToken( o3tl::getToken(sOldStylePath, 0, ';', nToken) );
        if (!sToken.isEmpty())
            lList.push_back(sToken);
    }
    while(nToken >= 0);

    return lList;
}

// static
void PathSettings::impl_purgeKnownPaths(PathSettings::PathInfo& rPath,
                                        std::vector<OUString>& lList)
{
    // Erase items in the internal path list from lList.
    // Also erase items in the internal path list from the user path list.
    for (auto const& internalPath : rPath.lInternalPaths)
    {
        std::vector<OUString>::iterator pItem = std::find(lList.begin(), lList.end(), internalPath);
        if (pItem != lList.end())
            lList.erase(pItem);
        pItem = std::find(rPath.lUserPaths.begin(), rPath.lUserPaths.end(), internalPath);
        if (pItem != rPath.lUserPaths.end())
            rPath.lUserPaths.erase(pItem);
    }

    // Erase items not in lList from the user path list.
    std::erase_if(rPath.lUserPaths,
        [&lList](const OUString& rItem) {
            return std::find(lList.begin(), lList.end(), rItem) == lList.end();
        });

    // Erase items in the user path list from lList.
    for (auto const& userPath : rPath.lUserPaths)
    {
        std::vector<OUString>::iterator pItem = std::find(lList.begin(), lList.end(), userPath);
        if (pItem != lList.end())
            lList.erase(pItem);
    }

    // Erase the write path from lList
    std::vector<OUString>::iterator pItem = std::find(lList.begin(), lList.end(), rPath.sWritePath);
    if (pItem != lList.end())
        lList.erase(pItem);
}

void PathSettings::impl_rebuildPropertyDescriptor(std::unique_lock<std::mutex>& /*g*/)
{

    sal_Int32 c = static_cast<sal_Int32>(m_lPaths.size());
    sal_Int32 i = 0;
    m_lPropDesc.realloc(c*IDGROUP_COUNT);
    auto plPropDesc = m_lPropDesc.getArray();

    for (auto const& path : m_lPaths)
    {
        const PathSettings::PathInfo& rPath = path.second;
        css::beans::Property*   pProp = nullptr;

        pProp             = &(plPropDesc[i]);
        pProp->Name       = rPath.sPathName;
        pProp->Handle     = i;
        pProp->Type       = cppu::UnoType<OUString>::get();
        pProp->Attributes = css::beans::PropertyAttribute::BOUND;
        if (rPath.bIsReadonly)
            pProp->Attributes |= css::beans::PropertyAttribute::READONLY;
        ++i;

        pProp             = &(plPropDesc[i]);
        pProp->Name       = rPath.sPathName+POSTFIX_INTERNAL_PATHS;
        pProp->Handle     = i;
        pProp->Type       = cppu::UnoType<css::uno::Sequence< OUString >>::get();
        pProp->Attributes = css::beans::PropertyAttribute::BOUND   |
                            css::beans::PropertyAttribute::READONLY;
        ++i;

        pProp             = &(plPropDesc[i]);
        pProp->Name       = rPath.sPathName+POSTFIX_USER_PATHS;
        pProp->Handle     = i;
        pProp->Type       = cppu::UnoType<css::uno::Sequence< OUString >>::get();
        pProp->Attributes = css::beans::PropertyAttribute::BOUND;
        if (rPath.bIsReadonly)
            pProp->Attributes |= css::beans::PropertyAttribute::READONLY;
        ++i;

        pProp             = &(plPropDesc[i]);
        pProp->Name       = rPath.sPathName+POSTFIX_WRITE_PATH;
        pProp->Handle     = i;
        pProp->Type       = cppu::UnoType<OUString>::get();
        pProp->Attributes = css::beans::PropertyAttribute::BOUND;
        if (rPath.bIsReadonly)
            pProp->Attributes |= css::beans::PropertyAttribute::READONLY;
        ++i;
    }

    m_pPropHelp.reset(new ::cppu::OPropertyArrayHelper(m_lPropDesc, false)); // false => not sorted ... must be done inside helper
}

css::uno::Any PathSettings::impl_getPathValue(std::unique_lock<std::mutex>& g, sal_Int32 nID) const
{
    const PathSettings::PathInfo* pPath = impl_getPathAccessConst(g, nID);
    if (! pPath)
        throw css::lang::IllegalArgumentException();

    css::uno::Any aVal;
    switch(impl_getPropGroup(nID))
    {
        case IDGROUP_OLDSTYLE :
             {
                OUString sVal = impl_convertPath2OldStyle(*pPath);
                aVal <<= sVal;
             }
             break;

        case IDGROUP_INTERNAL_PATHS :
             {
                aVal <<= comphelper::containerToSequence(pPath->lInternalPaths);
             }
             break;

        case IDGROUP_USER_PATHS :
             {
                aVal <<= comphelper::containerToSequence(pPath->lUserPaths);
             }
             break;

        case IDGROUP_WRITE_PATH :
             {
                aVal <<= pPath->sWritePath;
             }
             break;
    }

    return aVal;
}

void PathSettings::impl_setPathValue(std::unique_lock<std::mutex>& g,
                                     sal_Int32      nID ,
                                     const css::uno::Any& aVal)
{
    PathSettings::PathInfo* pOrgPath = impl_getPathAccess(g, nID);
    if (! pOrgPath)
        throw css::container::NoSuchElementException();

    // We work on a copied path ... so we can be sure that errors during this operation
    // does not make our internal cache invalid  .-)
    PathSettings::PathInfo aChangePath(*pOrgPath);

    switch(impl_getPropGroup(nID))
    {
        case IDGROUP_OLDSTYLE :
             {
                OUString sVal;
                aVal >>= sVal;
                std::vector<OUString> lList = impl_convertOldStyle2Path(sVal);
                impl_subst(lList, fa_getSubstitution(g), false);
                impl_purgeKnownPaths(aChangePath, lList);
                if (! impl_isValidPath(lList))
                    throw css::lang::IllegalArgumentException();

                if (aChangePath.bIsSinglePath)
                {
                    SAL_WARN_IF(lList.size()>1, "fwk", "PathSettings::impl_setPathValue(): You try to set more than path value for a defined SINGLE_PATH!");
                    if ( !lList.empty() )
                        aChangePath.sWritePath = *(lList.begin());
                    else
                        aChangePath.sWritePath.clear();
                }
                else
                {
                    for (auto const& elem : lList)
                    {
                        aChangePath.lUserPaths.push_back(elem);
                    }
                }
             }
             break;

        case IDGROUP_INTERNAL_PATHS :
             {
                if (aChangePath.bIsSinglePath)
                {
                    throw css::uno::Exception(
                        "The path '" + aChangePath.sPathName
                        + "' is defined as SINGLE_PATH. It's sub set of internal paths can't be set.",
                        static_cast< ::cppu::OWeakObject* >(this));
                }

                css::uno::Sequence<OUString> lTmpList;
                aVal >>= lTmpList;
                std::vector<OUString> lList = comphelper::sequenceToContainer<std::vector<OUString>>(lTmpList);
                if (! impl_isValidPath(lList))
                    throw css::lang::IllegalArgumentException();
                aChangePath.lInternalPaths = std::move(lList);
             }
             break;

        case IDGROUP_USER_PATHS :
             {
                if (aChangePath.bIsSinglePath)
                {
                    throw css::uno::Exception(
                        "The path '" + aChangePath.sPathName
                        + "' is defined as SINGLE_PATH. It's sub set of internal paths can't be set.",
                        static_cast< ::cppu::OWeakObject* >(this));
                }

                css::uno::Sequence<OUString> lTmpList;
                aVal >>= lTmpList;
                std::vector<OUString> lList = comphelper::sequenceToContainer<std::vector<OUString>>(lTmpList);
                if (! impl_isValidPath(lList))
                    throw css::lang::IllegalArgumentException();
                aChangePath.lUserPaths = std::move(lList);
             }
             break;

        case IDGROUP_WRITE_PATH :
             {
                OUString sVal;
                aVal >>= sVal;
                if (! impl_isValidPath(sVal))
                    throw css::lang::IllegalArgumentException();
                aChangePath.sWritePath = sVal;
             }
             break;
    }

    // TODO check if path has at least one path value set
    // At least it depends from the feature using this path, if an empty path list is allowed.

    // first we should try to store the changed (copied!) path ...
    // In case an error occurs on saving time an exception is thrown ...
    // If no exception occurs we can update our internal cache (means
    // we can overwrite pOrgPath !
    impl_storePath(g, aChangePath);
    *pOrgPath = std::move(aChangePath);
}

// static
bool PathSettings::impl_isValidPath(const std::vector<OUString>& lPath)
{
    for (auto const& path : lPath)
    {
        if (! impl_isValidPath(path))
            return false;
    }

    return true;
}

// static
bool PathSettings::impl_isValidPath(std::u16string_view sPath)
{
    // allow empty path to reset a path.
// idea by LLA to support empty paths
//    if (sPath.getLength() == 0)
//    {
//        return sal_True;
//    }

    return (! INetURLObject(sPath).HasError());
}

OUString impl_extractBaseFromPropName(const OUString& sPropName)
{
    sal_Int32 i = sPropName.indexOf(POSTFIX_INTERNAL_PATHS);
    if (i > -1)
        return sPropName.copy(0, i);
    i = sPropName.indexOf(POSTFIX_USER_PATHS);
    if (i > -1)
        return sPropName.copy(0, i);
    i = sPropName.indexOf(POSTFIX_WRITE_PATH);
    if (i > -1)
        return sPropName.copy(0, i);

    return sPropName;
}

PathSettings::PathInfo* PathSettings::impl_getPathAccess(std::unique_lock<std::mutex>& /*g*/, sal_Int32 nHandle)
{
    if (nHandle > (m_lPropDesc.getLength()-1))
        return nullptr;

    const css::beans::Property&            rProp = m_lPropDesc[nHandle];
    OUString                  sProp = impl_extractBaseFromPropName(rProp.Name);
    PathSettings::PathHash::iterator rPath = m_lPaths.find(sProp);

    if (rPath != m_lPaths.end())
       return &(rPath->second);

    return nullptr;
}

const PathSettings::PathInfo* PathSettings::impl_getPathAccessConst(std::unique_lock<std::mutex>& /*g*/, sal_Int32 nHandle) const
{
    if (nHandle > (m_lPropDesc.getLength()-1))
        return nullptr;

    const css::beans::Property&     rProp = m_lPropDesc[nHandle];
    OUString                        sProp = impl_extractBaseFromPropName(rProp.Name);
    PathSettings::PathHash::const_iterator rPath = m_lPaths.find(sProp);

    if (rPath != m_lPaths.end())
       return &(rPath->second);

    return nullptr;
}

bool PathSettings::convertFastPropertyValue(std::unique_lock<std::mutex>& g,
                                            css::uno::Any& aConvertedValue,
                                            css::uno::Any& aOldValue      ,
                                            sal_Int32      nHandle        ,
                                            const css::uno::Any& aValue         )
{
    // throws NoSuchElementException !
    css::uno::Any aCurrentVal = impl_getPathValue(g, nHandle);

    return PropHelper::willPropertyBeChanged(
                aCurrentVal,
                aValue,
                aOldValue,
                aConvertedValue);
}

void PathSettings::setFastPropertyValue_NoBroadcast(std::unique_lock<std::mutex>& g,
                                                    sal_Int32      nHandle,
                                                    const css::uno::Any& aValue )
{
    // throws NoSuchElement- and IllegalArgumentException !
    impl_setPathValue(g, nHandle, aValue);
}

void PathSettings::getFastPropertyValue(std::unique_lock<std::mutex>& g,
                                                 css::uno::Any& aValue ,
                                                 sal_Int32      nHandle) const
{
    aValue = impl_getPathValue(g, nHandle);
}

::cppu::IPropertyArrayHelper& PathSettings::getInfoHelper()
{
    return *m_pPropHelp;
}

css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL PathSettings::getPropertySetInfo()
{
    return ::cppu::OPropertySetHelper::createPropertySetInfo(getInfoHelper());
}

css::uno::Reference< css::util::XStringSubstitution > PathSettings::fa_getSubstitution(std::unique_lock<std::mutex>& g)
{
    css::uno::Reference< css::util::XStringSubstitution > xSubst = m_xSubstitution;


    if (! xSubst.is())
    {
        g.unlock();

        // create the needed substitution service.
        // We must replace all used variables inside read path values.
        // In case we can't do so... the whole office can't work really.
        // That's why it seems to be OK to throw a RuntimeException then.
        xSubst = css::util::PathSubstitution::create(m_xContext);

        g.lock();
        m_xSubstitution = xSubst;
    }

    return xSubst;
}

css::uno::Reference< css::container::XNameAccess > PathSettings::fa_getCfgOld(std::unique_lock<std::mutex>& g)
{
    css::uno::Reference< css::container::XNameAccess > xCfg = m_xCfgOld;

    if (! xCfg.is())
    {
        g.unlock();

        xCfg.set(  ::comphelper::ConfigurationHelper::openConfig(
                        m_xContext,
                        u"org.openoffice.Office.Common/Path/Current"_ustr,
                        ::comphelper::EConfigurationModes::Standard), // not readonly! Sometimes we need write access there !!!
                   css::uno::UNO_QUERY_THROW);

        g.lock();

        m_xCfgOld = xCfg;
    }

    return xCfg;
}

css::uno::Reference< css::container::XNameAccess > PathSettings::fa_getCfgNew(std::unique_lock<std::mutex>& g)
{
    css::uno::Reference< css::container::XNameAccess > xCfg = m_xCfgNew;

    if (! xCfg.is())
    {
        g.unlock();

        xCfg.set(  ::comphelper::ConfigurationHelper::openConfig(
                        m_xContext,
                        u"org.openoffice.Office.Paths/Paths"_ustr,
                        ::comphelper::EConfigurationModes::Standard),
                   css::uno::UNO_QUERY_THROW);

        g.lock();

        m_xCfgNew = xCfg;
        m_xCfgNewListener = new WeakChangesListener(this);

        css::uno::Reference< css::util::XChangesNotifier > xBroadcaster(xCfg, css::uno::UNO_QUERY_THROW);
        xBroadcaster->addChangesListener(m_xCfgNewListener);
    }

    return xCfg;
}

// XInitialization
void SAL_CALL PathSettings::initialize(const css::uno::Sequence<css::uno::Any>& /*rArguments*/)
{
    // so we can reinitialize/reset all path variables to default
    std::unique_lock g(m_aMutex);
    impl_readAll(g);
}

}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_framework_PathSettings_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &)
{
    rtl::Reference<PathSettings> xPathSettings = new PathSettings(context);
    // fill cache
    xPathSettings->readAll();

    return cppu::acquire(xPathSettings.get());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
