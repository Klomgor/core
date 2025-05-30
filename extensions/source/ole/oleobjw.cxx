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

#include "ole2uno.hxx"
#include <sal/log.hxx>
#include <o3tl/char16_t2wchar_t.hxx>

#include <osl/diagnose.h>
#include <osl/doublecheckedlocking.h>
#include <osl/thread.h>

#include <memory>
#include <string_view>
#include <com/sun/star/script/CannotConvertException.hpp>
#include <com/sun/star/script/FailReason.hpp>
#include <com/sun/star/beans/XMaterialHolder.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/script/XInvocation.hpp>
#include <com/sun/star/bridge/ModelDependent.hpp>

#include <com/sun/star/bridge/oleautomation/NamedArgument.hpp>
#include <com/sun/star/bridge/oleautomation/PropertyPutArgument.hpp>
#include <cppuhelper/exc_hlp.hxx>

#include <typelib/typedescription.hxx>
#include <rtl/uuid.h>
#include <rtl/ustring.hxx>

#include "jscriptclasses.hxx"

#include "oleobjw.hxx"
#include "unoobjw.hxx"
#include <stdio.h>
using namespace osl;
using namespace cppu;
using namespace com::sun::star::script;
using namespace com::sun::star::lang;
using namespace com::sun::star::bridge;
using namespace com::sun::star::bridge::oleautomation;
using namespace com::sun::star::bridge::ModelDependent;
using namespace ::com::sun::star;


#define JSCRIPT_ID_PROPERTY L"_environment"
#define JSCRIPT_ID          L"jscript"

// key: XInterface pointer created by Invocation Adapter Factory
// value: XInterface pointer to the wrapper class.
// Entries to the map are made within
// Any createOleObjectWrapper(IUnknown* pUnknown, const Type& aType);
// Entries are being deleted if the wrapper class's destructor has been
// called.
// Before UNO object is wrapped to COM object this map is checked
// to see if the UNO object is already a wrapper.
std::unordered_map<sal_uIntPtr, sal_uIntPtr> AdapterToWrapperMap;
// key: XInterface of the wrapper object.
// value: XInterface of the Interface created by the Invocation Adapter Factory.
// A COM wrapper is responsible for removing the corresponding entry
// in AdapterToWrapperMap if it is being destroyed. Because the wrapper does not
// know about its adapted interface it uses WrapperToAdapterMap to get the
// adapted interface which is then used to locate the entry in AdapterToWrapperMap.
std::unordered_map<sal_uIntPtr,sal_uIntPtr> WrapperToAdapterMap;

std::unordered_map<sal_uIntPtr, WeakReference<XInterface> > ComPtrToWrapperMap;

IUnknownWrapper::IUnknownWrapper( Reference<XMultiServiceFactory> const & xFactory,
                                  sal_uInt8 unoWrapperClass, sal_uInt8 comWrapperClass):
    UnoConversionUtilities<IUnknownWrapper>( xFactory, unoWrapperClass, comWrapperClass),
    m_pxIdlClass( nullptr), m_eJScript( JScriptUndefined),
    m_bComTlbIndexInit(false),  m_bHasDfltMethod(false), m_bHasDfltProperty(false)
{
}


IUnknownWrapper::~IUnknownWrapper()
{
    o2u_attachCurrentThread();
    MutexGuard guard(getBridgeMutex());
    XInterface * xIntRoot = static_cast<OWeakObject *>(this);
#if OSL_DEBUG_LEVEL > 0
    acquire(); // make sure we don't delete us twice because of Reference
    OSL_ASSERT( Reference<XInterface>( static_cast<XWeak*>(this), UNO_QUERY).get() == xIntRoot );
#endif

    // remove entries in global maps
    auto it= WrapperToAdapterMap.find( reinterpret_cast<sal_uIntPtr>(xIntRoot));
    if( it != WrapperToAdapterMap.end())
    {
        sal_uIntPtr adapter= it->second;

        AdapterToWrapperMap.erase( adapter);
        WrapperToAdapterMap.erase( it);
    }

    auto it_c= ComPtrToWrapperMap.find( reinterpret_cast<sal_uIntPtr>(m_spUnknown.p));
    if(it_c != ComPtrToWrapperMap.end())
        ComPtrToWrapperMap.erase(it_c);
}

Any IUnknownWrapper::queryInterface(const Type& t)
{
    if (t == cppu::UnoType<XDefaultMethod>::get() && !m_bHasDfltMethod )
        return Any();
    if (t == cppu::UnoType<XDefaultProperty>::get() && !m_bHasDfltProperty )
        return Any();
    if ( ( t == cppu::UnoType<XInvocation>::get() || t == cppu::UnoType<XAutomationInvocation>::get() ) && !m_spDispatch)
        return Any();
    // XDirectInvocation seems to be an oracle replacement for XAutomationInvocation, however it is flawed especially wrt. assumptions about whether to invoke a
    // Put or Get property, the implementation code has no business guessing that, it's up to the caller to decide that. Worse XDirectInvocation duplicates lots of code.
    // XAutomationInvocation provides separate calls for put& get
    // properties. Note: Currently the basic runtime doesn't call put properties directly, it should... after all the basic runtime should know whether it is calling a put or get property.
    // For the moment for ease of merging we will let the XDirectInvoke and XAuthomationInvocation interfaces stay side by side (and for the moment at least I would prefer the basic
    // runtime to call XAutomationInvocation instead of XDirectInvoke
    return WeakImplHelper<XBridgeSupplier2,
        XInitialization, XAutomationObject, XDefaultProperty, XDefaultMethod, XDirectInvocation, XAutomationInvocation >::queryInterface(t);
}

Reference<XIntrospectionAccess> SAL_CALL IUnknownWrapper::getIntrospection()
{
    Reference<XIntrospectionAccess> ret;

    return ret;
}

Any SAL_CALL IUnknownWrapper::invokeGetProperty( const OUString& aPropertyName, const Sequence< Any >& aParams, Sequence< sal_Int16 >& aOutParamIndex, Sequence< Any >& aOutParam )
{
    Any aResult;
    try
    {
        o2u_attachCurrentThread();
        ITypeInfo * pInfo = getTypeInfo();
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(aPropertyName, & aDescGet, & aDescPut, & aVarDesc);
        if ( !aDescGet )
        {
            OUString msg("[automation bridge]Property \"" + aPropertyName +
                "\" is not supported");
            throw UnknownPropertyException(msg);
        }
        aResult = invokeWithDispIdComTlb( aDescGet, aPropertyName, aParams, aOutParamIndex, aOutParam );
    }
    catch ( const Exception& e )
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::invokeGetProperty ! Message : \n " +
                e.Message,
                nullptr, anyEx );
    }
    return aResult;
}

Any SAL_CALL IUnknownWrapper::invokePutProperty( const OUString& aPropertyName, const Sequence< Any >& aParams, Sequence< sal_Int16 >& aOutParamIndex, Sequence< Any >& aOutParam )
{
    Any aResult;
    try
    {
        o2u_attachCurrentThread();
        ITypeInfo * pInfo = getTypeInfo();
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(aPropertyName, & aDescGet, & aDescPut, & aVarDesc);
        if ( !aDescPut )
        {
            OUString msg("[automation bridge]Property \"" + aPropertyName +
                "\" is not supported");
            throw UnknownPropertyException(msg);
        }
        aResult = invokeWithDispIdComTlb( aDescPut, aPropertyName, aParams, aOutParamIndex, aOutParam );
    }
    catch ( const Exception& e )
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::invokePutProperty ! Message : \n" +
                e.Message,
                nullptr, anyEx );
    }
    return aResult;
}


Any SAL_CALL IUnknownWrapper::invoke( const OUString& aFunctionName,
             const Sequence< Any >& aParams, Sequence< sal_Int16 >& aOutParamIndex,
             Sequence< Any >& aOutParam )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }

    Any ret;

    try
    {
        o2u_attachCurrentThread();

        TypeDescription methodDesc;
        getMethodInfo(aFunctionName, methodDesc);
        if( methodDesc.is())
        {
            ret = invokeWithDispIdUnoTlb(aFunctionName,
                                         aParams,
                                         aOutParamIndex,
                                         aOutParam);
        }
        else
        {
            ret= invokeWithDispIdComTlb( aFunctionName,
                                         aParams,
                                         aOutParamIndex,
                                         aOutParam);
        }
    }
    catch (const IllegalArgumentException &)
    {
        throw;
    }
    catch (const CannotConvertException &)
    {
        throw;
    }
    catch (const BridgeRuntimeError & e)
    {
         throw RuntimeException(e.message);
    }
    catch (const Exception & e)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::invoke ! Message : \n" +
                e.Message,
                nullptr, anyEx );

    }
    catch(...)
    {
        throw RuntimeException("[automation bridge] unexpected exception in "
                  "IUnknownWrapper::Invoke !");
    }
    return ret;
}

void SAL_CALL IUnknownWrapper::setValue( const OUString& aPropertyName,
                 const Any& aValue )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }
    try
    {
        o2u_attachCurrentThread();

        ITypeInfo * pInfo = getTypeInfo();
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(aPropertyName, & aDescGet, & aDescPut, & aVarDesc);
        //check if there is such a property at all or if it is read only
        if ( ! aDescPut && ! aDescGet && ! aVarDesc)
        {
            OUString msg("[automation bridge]Property \"" + aPropertyName +
                         "\" is not supported");
            throw UnknownPropertyException(msg);
        }

        if ( (! aDescPut && aDescGet)
             || (aVarDesc && aVarDesc->wVarFlags == VARFLAG_FREADONLY) )
        {
            //read-only
            SAL_WARN( "extensions.olebridge", "[automation bridge] Property " << aPropertyName << " is read-only");
            // ignore silently
            return;
        }

        HRESULT hr= S_OK;
        DISPPARAMS dispparams;
        CComVariant varArg;
        CComVariant varRefArg;
        CComVariant varResult;
        ExcepInfo excepinfo;
        unsigned int uArgErr;

        // converting UNO value to OLE variant
        DISPID dispidPut= DISPID_PROPERTYPUT;
        dispparams.rgdispidNamedArgs = &dispidPut;
        dispparams.cArgs = 1;
        dispparams.cNamedArgs = 1;
        dispparams.rgvarg = & varArg;

        OSL_ASSERT(aDescPut || aVarDesc);

        VARTYPE vt = 0;
        DISPID dispid = 0;
        INVOKEKIND invkind = INVOKE_PROPERTYPUT;
        //determine the expected type, dispid, invoke kind (DISPATCH_PROPERTYPUT,
        //DISPATCH_PROPERTYPUTREF)
        if (aDescPut)
        {
            vt = getElementTypeDesc(& aDescPut->lprgelemdescParam[0].tdesc);
            dispid = aDescPut->memid;
            invkind = aDescPut->invkind;
        }
        else
        {
            vt = getElementTypeDesc( & aVarDesc->elemdescVar.tdesc);
            dispid = aVarDesc->memid;
            if (vt == VT_UNKNOWN || vt == VT_DISPATCH ||
                (vt & VT_ARRAY) || (vt & VT_BYREF))
            {
                invkind = INVOKE_PROPERTYPUTREF;
            }
        }

        // convert the uno argument
        if (vt & VT_BYREF)
        {
            anyToVariant( & varRefArg, aValue, ::sal::static_int_cast< VARTYPE, int >( vt ^ VT_BYREF ) );
            varArg.vt = vt;
            if( (vt & VT_TYPEMASK) == VT_VARIANT)
                varArg.byref = & varRefArg;
            else if ((vt & VT_TYPEMASK) == VT_DECIMAL)
                varArg.byref = & varRefArg.decVal;
            else
                varArg.byref = & varRefArg.byref;
        }
        else
        {
            anyToVariant(& varArg, aValue, vt);
        }
        // call to IDispatch
        hr = m_spDispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, ::sal::static_int_cast< WORD, INVOKEKIND >( invkind ),
                                 &dispparams, & varResult, & excepinfo, &uArgErr);

        // lookup error code
        switch (hr)
        {
        case S_OK:
            break;
        case DISP_E_BADPARAMCOUNT:
            throw RuntimeException();
            break;
        case DISP_E_BADVARTYPE:
            throw RuntimeException();
            break;
        case DISP_E_EXCEPTION:
            throw InvocationTargetException();
            break;
        case DISP_E_MEMBERNOTFOUND:
            throw UnknownPropertyException();
            break;
        case DISP_E_NONAMEDARGS:
            throw RuntimeException();
            break;
        case DISP_E_OVERFLOW:
            throw CannotConvertException("call to OLE object failed", static_cast<XInterface*>(
                                             static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::OUT_OF_RANGE, uArgErr);
            break;
        case DISP_E_PARAMNOTFOUND:
            throw IllegalArgumentException("call to OLE object failed", static_cast<XInterface*>(
                                            static_cast<XWeak*>(this)), ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr )) ;
            break;
        case DISP_E_TYPEMISMATCH:
            throw CannotConvertException("call to OLE object failed", static_cast<XInterface*>(
                                             static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::UNKNOWN, ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
            break;
        case DISP_E_UNKNOWNINTERFACE:
            throw RuntimeException();
            break;
        case DISP_E_UNKNOWNLCID:
            throw RuntimeException();
            break;
        case DISP_E_PARAMNOTOPTIONAL:
            throw CannotConvertException("call to OLE object failed",static_cast<XInterface*>(
                                             static_cast<XWeak*>(this)) , TypeClass_UNKNOWN, FailReason::NO_DEFAULT_AVAILABLE, uArgErr);
            break;
        default:
            throw  RuntimeException();
            break;
        }
    }
    catch (const CannotConvertException &)
    {
        throw;
    }
    catch (const UnknownPropertyException &)
    {
        throw;
    }
    catch (const BridgeRuntimeError& e)
    {
        throw RuntimeException(e.message);
    }
    catch (const Exception & e)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::setValue ! Message : \n" +
                e.Message,
                nullptr, anyEx );

    }
    catch (...)
    {
        throw RuntimeException(
            "[automation bridge] unexpected exception in "
            "IUnknownWrapper::setValue !");
    }
}

static OUString BStrToOUString(BSTR s) { return OUString(o3tl::toU(s), SysStringLen(s)); }

Any SAL_CALL IUnknownWrapper::getValue( const OUString& aPropertyName )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }
    Any ret;
    try
    {
        o2u_attachCurrentThread();
        ITypeInfo * pInfo = getTypeInfo();
        // I was going to implement an XServiceInfo interface to allow the type
        // of the automation object to be exposed... but it seems
        // from looking at comments in the code that it is possible for
        // this object to actually wrap a UNO object ( I guess if automation is
        // used from MSO to create Openoffice objects ) Therefore, those objects
        // will more than likely already have their own XServiceInfo interface.
        // Instead here I chose a name that should be illegal both in COM and
        // UNO ( from an IDL point of view ) therefore I think this is a safe
        // hack
        if ( aPropertyName == "$GetTypeName" )
        {
            if ( pInfo && m_sTypeName.getLength() == 0 )
            {
                m_sTypeName = "IDispatch";
                CComBSTR sName;

                if ( SUCCEEDED( pInfo->GetDocumentation( -1, &sName, nullptr, nullptr, nullptr  ) ) )
                {
                    OUString sTmp( o3tl::toU(LPCOLESTR(sName)));
                    if ( sTmp.startsWith("_") )
                       sTmp = sTmp.copy(1);
                    // do we own the memory for pTypeLib, msdn doc is vague
                    // I'll assume we do
                    CComPtr< ITypeLib > pTypeLib;
                    unsigned int index;
                    if ( SUCCEEDED(  pInfo->GetContainingTypeLib(  &pTypeLib.p, &index )) )
                    {
                        if ( SUCCEEDED( pTypeLib->GetDocumentation( -1, &sName, nullptr, nullptr, nullptr  ) ) )
                        {
                            OUString sLibName( o3tl::toU(LPCOLESTR(sName)));
                            m_sTypeName = sLibName + "." + sTmp;

                        }
                    }
                }

            }
            ret <<= m_sTypeName;
            return ret;
        }
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(aPropertyName, & aDescGet, & aDescPut, & aVarDesc);
        if ( ! aDescGet && ! aDescPut && ! aVarDesc)
        {
            //property not found
            OUString msg("[automation bridge]Property \"" + aPropertyName +
                         "\" is not supported");
            throw UnknownPropertyException(msg);
        }
        // write-only should not be possible
        OSL_ASSERT(  aDescGet  || ! aDescPut);

        HRESULT hr;
        DISPPARAMS dispparams = {nullptr, nullptr, 0, 0};
        CComVariant varResult;
        ExcepInfo excepinfo;
        unsigned int uArgErr;
        DISPID dispid;
        if (aDescGet)
            dispid = aDescGet->memid;
        else if (aVarDesc)
            dispid = aVarDesc->memid;
        else
            dispid = aDescPut->memid;

        hr = m_spDispatch->Invoke(dispid,
                                 IID_NULL,
                                 LOCALE_USER_DEFAULT,
                                 DISPATCH_PROPERTYGET,
                                 &dispparams,
                                 &varResult,
                                 &excepinfo,
                                 &uArgErr);

        // converting return value and out parameter back to UNO
        if (hr == S_OK)
        {
            // If the com object implements uno interfaces then we have
            // to convert the attribute into the expected type.
            TypeDescription attrInfo;
            getAttributeInfo(aPropertyName, attrInfo);
            if( attrInfo.is() )
                variantToAny( &varResult, ret, Type( attrInfo.get()->pWeakRef));
            else
                variantToAny(&varResult, ret);
        }

        // lookup error code
        switch (hr)
        {
        case S_OK:
            break;
        case DISP_E_BADPARAMCOUNT:
        case DISP_E_BADVARTYPE:
        case DISP_E_EXCEPTION:
            throw RuntimeException(BStrToOUString(excepinfo.bstrDescription));
            break;
        case DISP_E_MEMBERNOTFOUND:
            throw UnknownPropertyException(BStrToOUString(excepinfo.bstrDescription));
            break;
        default:
            throw RuntimeException(BStrToOUString(excepinfo.bstrDescription));
            break;
        }
    }
    catch ( const UnknownPropertyException& )
    {
        throw;
    }
    catch (const BridgeRuntimeError& e)
    {
        throw RuntimeException(e.message);
    }
    catch (const Exception & e)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::getValue ! Message : \n" +
                e.Message,
                nullptr, anyEx );
    }
    catch (...)
    {
        throw RuntimeException(
            "[automation bridge] unexpected exception in "
            "IUnknownWrapper::getValue !");
    }
    return ret;
}

sal_Bool SAL_CALL IUnknownWrapper::hasMethod( const OUString& aName )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }
    bool ret = false;

    try
    {
        o2u_attachCurrentThread();
        ITypeInfo* pInfo = getTypeInfo();
        FuncDesc aDesc(pInfo);
        getFuncDesc(aName, & aDesc);
        // Automation properties can have arguments. Those are treated as methods and
        //are called through XInvocation::invoke.
        if ( ! aDesc)
        {
            FuncDesc aDescGet(pInfo);
            FuncDesc aDescPut(pInfo);
            VarDesc aVarDesc(pInfo);
            getPropDesc( aName, & aDescGet, & aDescPut, & aVarDesc);
            if ((aDescGet && aDescGet->cParams > 0)
                || (aDescPut && aDescPut->cParams > 0))
                ret = true;
        }
        else
            ret = true;
    }
    catch (const BridgeRuntimeError& e)
    {
        throw RuntimeException(e.message);
    }
    catch (const Exception & e)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::hasMethod ! Message : \n" +
                e.Message,
                nullptr, anyEx );
    }
    catch (...)
    {
        throw RuntimeException("[automation bridge] unexpected exception in "
            "IUnknownWrapper::hasMethod !");
    }
    return ret;
}

sal_Bool SAL_CALL IUnknownWrapper::hasProperty( const OUString& aName )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException("[automation bridge] The object does not have an "
            "IDispatch interface");
    }
    bool ret = false;
    try
    {
        o2u_attachCurrentThread();

        ITypeInfo * pInfo = getTypeInfo();
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(aName, & aDescGet, & aDescPut, & aVarDesc);

    // we should probably just check the func kind
        // basic has been modified to handle properties ( 'get' ) props at
    // least with parameters
    // additionally you can call invoke(Get|Set)Property on the bridge
        // you can determine if a property has parameter is hasMethod
    // returns true for the name
        if (aVarDesc
            || aDescPut
            || aDescGet )
        {
            ret = true;
        }
    }
    catch (const BridgeRuntimeError& e)
    {
        throw RuntimeException(e.message);
    }
    catch (const Exception & e)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException("[automation bridge] unexpected exception in "
                "IUnknownWrapper::hasProperty ! Message : \n" +
                e.Message,
                nullptr, anyEx );

    }
    catch (...)
    {
        throw RuntimeException("[automation bridge] unexpected exception in "
            "IUnknownWrapper::hasProperty !");
    }
    return ret;
}

Any SAL_CALL IUnknownWrapper::createBridge( const Any& modelDepObject,
                const Sequence< sal_Int8 >& /*aProcessId*/, sal_Int16 sourceModelType,
                 sal_Int16 destModelType )
{
    Any ret;
    o2u_attachCurrentThread();

    if (
        (sourceModelType == UNO) &&
        (destModelType == OLE) &&
        (modelDepObject.getValueTypeClass() == TypeClass_INTERFACE)
       )
    {
        Reference<XInterface> xInt( *static_cast<XInterface* const *>(modelDepObject.getValue()));
        Reference<XInterface> xSelf( static_cast<OWeakObject*>(this));

        if (xInt == xSelf)
        {
            VARIANT* pVariant = static_cast<VARIANT*>(CoTaskMemAlloc(sizeof(VARIANT)));
            assert(pVariant && "Don't handle OOM conditions");

            VariantInit(pVariant);
            if (m_bOriginalDispatch)
            {
                pVariant->vt = VT_DISPATCH;
                pVariant->pdispVal = m_spDispatch;
                pVariant->pdispVal->AddRef();
            }
            else
            {
                pVariant->vt = VT_UNKNOWN;
                pVariant->punkVal = m_spUnknown;
                pVariant->punkVal->AddRef();
            }

            ret.setValue(static_cast<void*>(&pVariant), cppu::UnoType<sal_uIntPtr>::get());
        }
    }

    return ret;
}
/** @internal
    @exception IllegalArgumentException
    @exception CannotConvertException
    @exception InvocationTargetException
    @RuntimeException
*/
Any  IUnknownWrapper::invokeWithDispIdUnoTlb(const OUString& sFunctionName,
                                             const Sequence< Any >& Params,
                                             Sequence< sal_Int16 >& OutParamIndex,
                                             Sequence< Any >& OutParam)
{
    Any ret;
    HRESULT hr= S_OK;

    sal_Int32 parameterCount= Params.getLength();
    sal_Int32 outParameterCount= 0;
    typelib_InterfaceMethodTypeDescription* pMethod= nullptr;
    TypeDescription methodDesc;
    getMethodInfo(sFunctionName, methodDesc);

    // We need to know whether the IDispatch is from a JScript object.
    // Then out and in/out parameters have to be treated differently than
    // with common COM objects.
    bool bJScriptObject= isJScriptObject();
    std::unique_ptr<CComVariant[]> sarParams;
    std::unique_ptr<CComVariant[]> sarParamsRef;
    CComVariant *pVarParams= nullptr;
    CComVariant *pVarParamsRef= nullptr;
    bool bConvRet= true;

    if( methodDesc.is())
    {
        pMethod = reinterpret_cast<typelib_InterfaceMethodTypeDescription*>(methodDesc.get());
        parameterCount = pMethod->nParams;
        // Create the Array for the array being passed in DISPPARAMS
        // the array also contains the outparameter (but not the values)
        if( pMethod->nParams > 0)
        {
            sarParams.reset(new CComVariant[ parameterCount]);
            pVarParams = sarParams.get();
        }

        // Create the Array for the out an in/out parameter. These values
        // are referenced by the VT_BYREF VARIANTs in DISPPARAMS.
        // We need to find out the number of out and in/out parameter.
        for( sal_Int32 i=0; i < parameterCount; i++)
        {
            if( pMethod->pParams[i].bOut)
                outParameterCount++;
        }

        if( !bJScriptObject)
        {
            sarParamsRef.reset(new CComVariant[outParameterCount]);
            pVarParamsRef = sarParamsRef.get();
            // build up the parameters for IDispatch::Invoke
            sal_Int32 outParamIndex=0;
            int i = 0;
            try
            {
                for( i= 0; i < parameterCount; i++)
                {
                    // In parameter
                    if( pMethod->pParams[i].bIn && ! pMethod->pParams[i].bOut)
                    {
                        anyToVariant( &pVarParams[parameterCount - i -1], Params.getConstArray()[i]);
                    }
                    // Out parameter + in/out parameter
                    else if( pMethod->pParams[i].bOut )
                    {
                        CComVariant var;
                        if(pMethod->pParams[i].bIn)
                        {
                            anyToVariant( & var,Params[i]);
                            pVarParamsRef[outParamIndex] = var;
                        }

                        switch( pMethod->pParams[i].pTypeRef->eTypeClass)
                        {
                        case typelib_TypeClass_INTERFACE:
                        case typelib_TypeClass_STRUCT:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt= VT_DISPATCH;
                                pVarParamsRef[ outParamIndex].pdispVal= nullptr;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_DISPATCH | VT_BYREF;
                            pVarParams[parameterCount - i -1].ppdispVal= &pVarParamsRef[outParamIndex].pdispVal;
                            break;
                        case typelib_TypeClass_ENUM:
                        case typelib_TypeClass_LONG:
                        case typelib_TypeClass_UNSIGNED_LONG:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_I4;
                                pVarParamsRef[ outParamIndex].lVal = 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_I4 | VT_BYREF;
                            pVarParams[parameterCount - i -1].plVal= &pVarParamsRef[outParamIndex].lVal;
                            break;
                        case typelib_TypeClass_SEQUENCE:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_ARRAY| VT_VARIANT;
                                pVarParamsRef[ outParamIndex].parray= nullptr;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_ARRAY| VT_BYREF | VT_VARIANT;
                            pVarParams[parameterCount - i -1].pparray= &pVarParamsRef[outParamIndex].parray;
                            break;
                        case typelib_TypeClass_ANY:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_EMPTY;
                                pVarParamsRef[ outParamIndex].lVal = 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_VARIANT | VT_BYREF;
                            pVarParams[parameterCount - i -1].pvarVal = &pVarParamsRef[outParamIndex];
                            break;
                        case typelib_TypeClass_BOOLEAN:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_BOOL;
                                pVarParamsRef[ outParamIndex].boolVal = 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_BOOL| VT_BYREF;
                            pVarParams[parameterCount - i -1].pboolVal =
                                & pVarParamsRef[outParamIndex].boolVal;
                            break;

                        case typelib_TypeClass_STRING:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_BSTR;
                                pVarParamsRef[ outParamIndex].bstrVal= nullptr;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_BSTR| VT_BYREF;
                            pVarParams[parameterCount - i -1].pbstrVal=
                                & pVarParamsRef[outParamIndex].bstrVal;
                            break;

                        case typelib_TypeClass_FLOAT:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_R4;
                                pVarParamsRef[ outParamIndex].fltVal= 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_R4| VT_BYREF;
                            pVarParams[parameterCount - i -1].pfltVal =
                                & pVarParamsRef[outParamIndex].fltVal;
                            break;
                        case typelib_TypeClass_DOUBLE:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_R8;
                                pVarParamsRef[ outParamIndex].dblVal= 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_R8| VT_BYREF;
                            pVarParams[parameterCount - i -1].pdblVal=
                                & pVarParamsRef[outParamIndex].dblVal;
                            break;
                        case typelib_TypeClass_BYTE:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_UI1;
                                pVarParamsRef[ outParamIndex].bVal= 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_UI1| VT_BYREF;
                            pVarParams[parameterCount - i -1].pbVal=
                                & pVarParamsRef[outParamIndex].bVal;
                            break;
                        case typelib_TypeClass_CHAR:
                        case typelib_TypeClass_SHORT:
                        case typelib_TypeClass_UNSIGNED_SHORT:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_I2;
                                pVarParamsRef[ outParamIndex].iVal = 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_I2| VT_BYREF;
                            pVarParams[parameterCount - i -1].piVal=
                                & pVarParamsRef[outParamIndex].iVal;
                            break;

                        default:
                            if( ! pMethod->pParams[i].bIn)
                            {
                                pVarParamsRef[ outParamIndex].vt = VT_EMPTY;
                                pVarParamsRef[ outParamIndex].lVal = 0;
                            }
                            pVarParams[parameterCount - i -1].vt = VT_VARIANT | VT_BYREF;
                            pVarParams[parameterCount - i -1].pvarVal =
                                & pVarParamsRef[outParamIndex];
                        }
                        outParamIndex++;
                    } // end else if
                } // end for
            }
            catch (IllegalArgumentException & e)
            {
                e.ArgumentPosition = ::sal::static_int_cast< sal_Int16, int >( i );
                throw;
            }
            catch (CannotConvertException & e)
            {
                e.ArgumentIndex = i;
                throw;
            }
        }
        else // it is a JScriptObject
        {
            int i = 0;
            try
            {
                for( ; i< parameterCount; i++)
                {
                    // In parameter
                    if( pMethod->pParams[i].bIn && ! pMethod->pParams[i].bOut)
                    {
                        anyToVariant( &pVarParams[parameterCount - i -1], Params.getConstArray()[i]);
                    }
                    // Out parameter + in/out parameter
                    else if( pMethod->pParams[i].bOut )
                    {
                        CComObject<JScriptOutParam>* pParamObject;
                        if( !SUCCEEDED( CComObject<JScriptOutParam>::CreateInstance( &pParamObject)))
                        {
                            throw BridgeRuntimeError(
                                      "[automation bridge]IUnknownWrapper::"
                                      "invokeWithDispIdUnoTlb\n"
                                      "Could not create out parameter at index: " +
                                OUString::number(static_cast<sal_Int32>(i)));
                        }

                        CComPtr<IUnknown> pUnk(pParamObject->GetUnknown());
                        CComQIPtr<IDispatch> pDisp( pUnk);

                        pVarParams[ parameterCount - i -1].vt= VT_DISPATCH;
                        pVarParams[ parameterCount - i -1].pdispVal= pDisp;
                        pVarParams[ parameterCount - i -1].pdispVal->AddRef();
                        // if the param is in/out then put the parameter on index 0
                        if( pMethod->pParams[i].bIn ) // in / out
                        {
                            CComVariant varParam;
                            anyToVariant( &varParam, Params.getConstArray()[i]);
                            CComDispatchDriver dispDriver( pDisp);
                            if(FAILED( dispDriver.PutPropertyByName( L"0", &varParam)))
                                throw BridgeRuntimeError(
                                    "[automation bridge]IUnknownWrapper::"
                                    "invokeWithDispIdUnoTlb\n"
                                    "Could not set property \"0\" for the in/out "
                                    "param!");

                        }
                    }
                }
            }
            catch (IllegalArgumentException & e)
            {
                e.ArgumentPosition = ::sal::static_int_cast< sal_Int16, int >( i );
                throw;
            }
            catch (CannotConvertException & e)
            {
                e.ArgumentIndex = i;
                throw;
            }
        }
    }
    // No type description Available, that is we have to deal with a COM component,
    // that does not implements UNO interfaces ( IDispatch based)
    else
    {
        //We should not run into this block, because invokeWithDispIdComTlb should
        //have been called instead.
        OSL_ASSERT(false);
    }


    CComVariant     varResult;
    ExcepInfo       excepinfo;
    unsigned int    uArgErr;
    DISPPARAMS dispparams= { pVarParams, nullptr, static_cast<UINT>(parameterCount), 0};

    // Get the DISPID
    FuncDesc aDesc(getTypeInfo());
    getFuncDesc(sFunctionName, & aDesc);
    // invoking OLE method
    hr = m_spDispatch->Invoke(aDesc->memid,
                             IID_NULL,
                             LOCALE_USER_DEFAULT,
                             DISPATCH_METHOD,
                             &dispparams,
                             &varResult,
                             &excepinfo,
                             &uArgErr);

    // converting return value and out parameter back to UNO
    if (hr == S_OK)
    {
        if( outParameterCount && pMethod)
        {
            OutParamIndex.realloc( outParameterCount);
            auto pOutParamIndex = OutParamIndex.getArray();
            OutParam.realloc( outParameterCount);
            auto pOutParam = OutParam.getArray();
            sal_Int32 outIndex=0;
            int i = 0;
            try
            {
                for( ; i < parameterCount; i++)
                {
                    if( pMethod->pParams[i].bOut )
                    {
                        pOutParamIndex[outIndex]= static_cast<sal_Int16>(i);
                        Any outAny;
                        if( !bJScriptObject)
                        {
                            variantToAny( &pVarParamsRef[outIndex], outAny,
                                        Type(pMethod->pParams[i].pTypeRef), false);
                            pOutParam[outIndex++]= outAny;
                        }
                        else //JScriptObject
                        {
                            if( pVarParams[i].vt == VT_DISPATCH)
                            {
                                CComDispatchDriver pDisp( pVarParams[i].pdispVal);
                                if( pDisp)
                                {
                                    CComVariant varOut;
                                    if( SUCCEEDED( pDisp.GetPropertyByName( L"0", &varOut)))
                                    {
                                        variantToAny( &varOut, outAny,
                                                    Type(pMethod->pParams[parameterCount - 1 - i].pTypeRef), false);
                                        pOutParam[outParameterCount - 1 - outIndex++]= outAny;
                                    }
                                    else
                                        bConvRet= false;
                                }
                                else
                                    bConvRet= false;
                            }
                            else
                                bConvRet= false;
                        }
                    }
                    if( !bConvRet) break;
                }
            }
            catch(IllegalArgumentException & e)
            {
                e.ArgumentPosition = ::sal::static_int_cast< sal_Int16, int >( i );
                throw;
            }
            catch(CannotConvertException & e)
            {
                e.ArgumentIndex = i;
                throw;
            }
        }
        // return value, no type information available
        if ( bConvRet)
        {
            try
            {
                if( pMethod )
                    variantToAny(&varResult, ret, Type( pMethod->pReturnTypeRef), false);
                else
                    variantToAny(&varResult, ret, false);
            }
            catch (IllegalArgumentException & e)
            {
                e.Message =
                    "[automation bridge]IUnknownWrapper::invokeWithDispIdUnoTlb\n"
                    "Could not convert return value! \n Message: \n" + e.Message;
                throw;
            }
            catch (CannotConvertException & e)
            {
                e.Message =
                    "[automation bridge]IUnknownWrapper::invokeWithDispIdUnoTlb\n"
                    "Could not convert return value! \n Message: \n" + e.Message;
                throw;
            }
        }
    }

    if( !bConvRet) // conversion of return or out parameter failed
        throw CannotConvertException("Call to COM object failed. Conversion of return or out value failed",
                                      Reference<XInterface>( static_cast<XWeak*>(this), UNO_QUERY   ), TypeClass_UNKNOWN,
                                      FailReason::UNKNOWN, 0);// lookup error code
    // conversion of return or out parameter failed
    switch (hr)
    {
    case S_OK:
        break;
    case DISP_E_BADPARAMCOUNT:
        throw IllegalArgumentException();
        break;
    case DISP_E_BADVARTYPE:
        throw RuntimeException();
        break;
    case DISP_E_EXCEPTION:
        throw InvocationTargetException();
        break;
    case DISP_E_MEMBERNOTFOUND:
        throw IllegalArgumentException();
        break;
    case DISP_E_NONAMEDARGS:
        throw IllegalArgumentException();
        break;
    case DISP_E_OVERFLOW:
        throw CannotConvertException("call to OLE object failed", static_cast<XInterface*>(
                                         static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::OUT_OF_RANGE, uArgErr);
        break;
    case DISP_E_PARAMNOTFOUND:
        throw IllegalArgumentException("call to OLE object failed", static_cast<XInterface*>(
                                           static_cast<XWeak*>(this)), ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
        break;
    case DISP_E_TYPEMISMATCH:
        throw CannotConvertException("call to OLE object failed",static_cast<XInterface*>(
                                         static_cast<XWeak*>(this)) , TypeClass_UNKNOWN, FailReason::UNKNOWN, uArgErr);
        break;
    case DISP_E_UNKNOWNINTERFACE:
        throw RuntimeException() ;
        break;
    case DISP_E_UNKNOWNLCID:
        throw RuntimeException() ;
        break;
    case DISP_E_PARAMNOTOPTIONAL:
        throw CannotConvertException("call to OLE object failed", static_cast<XInterface*>(
                                         static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::NO_DEFAULT_AVAILABLE, uArgErr);
                break;
    default:
        throw RuntimeException();
        break;
    }

    return ret;
}


// XInitialization
void SAL_CALL IUnknownWrapper::initialize( const Sequence< Any >& aArguments )
{
    // 1.parameter is IUnknown
    // 2.parameter is a boolean which indicates if the COM pointer was an IUnknown or IDispatch
    // 3.parameter is a Sequence<Type>
    o2u_attachCurrentThread();
    OSL_ASSERT(aArguments.getLength() == 3);

    m_spUnknown= *static_cast<IUnknown* const *>(aArguments[0].getValue());
    m_spUnknown.QueryInterface( & m_spDispatch.p);

    aArguments[1] >>= m_bOriginalDispatch;
    aArguments[2] >>= m_seqTypes;

    ITypeInfo* pType = nullptr;
    try
    {
        // a COM object implementation that has no TypeInfo is still a legal COM object;
        // such objects can at least be transported through UNO using the bridge
        // so we should allow to create wrappers for them as well
        pType = getTypeInfo();
    }
    catch( const BridgeRuntimeError& )
    {}
    catch( const Exception& )
    {}

    if ( pType )
    {
        try
        {
            // Get Default member
            CComBSTR defaultMemberName;
            if ( SUCCEEDED( pType->GetDocumentation(0, &defaultMemberName, nullptr, nullptr, nullptr ) ) )
            {
                OUString usName(o3tl::toU(LPCOLESTR(defaultMemberName)));
                FuncDesc aDescGet(pType);
                FuncDesc aDescPut(pType);
                VarDesc aVarDesc(pType);
                // see if this is a property first ( more likely to be a property then a method )
                getPropDesc( usName, & aDescGet, & aDescPut, & aVarDesc);

                if ( !aDescGet && !aDescPut )
                {
                    getFuncDesc( usName, &aDescGet );
                    if ( !aDescGet )
                        throw BridgeRuntimeError( "[automation bridge]IUnknownWrapper::initialize() Failed to get Function or Property desc. for " + usName );
                }
                // now for some funny heuristics to make basic understand what to do
                // a single aDescGet ( that doesn't take any params ) would be
                // a read only ( defaultmember ) property e.g. this object
                // should implement XDefaultProperty
                // a single aDescGet ( that *does* ) take params is basically a
                // default method e.g. implement XDefaultMethod

                // a DescPut ( I guess we only really support a default param with '1' param ) as a setValue ( but I guess we can leave it through, the object will fail if we don't get it right anyway )
                if ( aDescPut || ( aDescGet && aDescGet->cParams == 0 ) )
                    m_bHasDfltProperty = true;
                if ( aDescGet->cParams > 0 )
                    m_bHasDfltMethod = true;
                if ( m_bHasDfltProperty || m_bHasDfltMethod )
                    m_sDefaultMember = usName;
            }
        }
        catch ( const BridgeRuntimeError & e )
        {
            throw RuntimeException( e.message );
        }
        catch( const Exception& e )
        {
            css::uno::Any anyEx = cppu::getCaughtException();
            throw css::lang::WrappedTargetRuntimeException(
                    "[automation bridge] unexpected exception in IUnknownWrapper::initialize() error message: \n" + e.Message,
                    nullptr, anyEx );
        }
    }
}


// XDirectInvocation
uno::Any SAL_CALL IUnknownWrapper::directInvoke( const OUString& aName, const uno::Sequence< uno::Any >& aParams )
{
    Any aResult;

    if ( !m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }

    o2u_attachCurrentThread();
    DISPID dispid;
    if ( !getDispid( aName, &dispid ) )
        throw IllegalArgumentException(
            "[automation bridge] The object does not have a function or property "
            + aName, Reference<XInterface>(), 0);

    CComVariant     varResult;
    ExcepInfo       excepinfo;
    unsigned int    uArgErr = 0;
    INVOKEKIND pInvkinds[2];
    pInvkinds[0] = INVOKE_FUNC;
    pInvkinds[1] = aParams.getLength() ? INVOKE_PROPERTYPUT : INVOKE_PROPERTYGET;
    HRESULT hInvRes = E_FAIL;

    // try Invoke first, if it does not work, try put/get property
    for ( sal_Int32 nStep = 0; FAILED( hInvRes ) && nStep < 2; nStep++ )
    {
        DISPPARAMS      dispparams = {nullptr, nullptr, 0, 0};

        std::unique_ptr<DISPID[]> arDispidNamedArgs;
        std::unique_ptr<CComVariant[]> ptrArgs;
        std::unique_ptr<CComVariant[]> ptrRefArgs; // referenced arguments
        CComVariant * arArgs = nullptr;
        CComVariant * arRefArgs = nullptr;

        dispparams.cArgs = aParams.getLength();

        // Determine the number of named arguments
        for ( uno::Any const & any : aParams )
            if ( any.getValueType() == cppu::UnoType<NamedArgument>::get() )
                dispparams.cNamedArgs ++;

        // fill the named arguments
        if ( dispparams.cNamedArgs > 0
          && ( dispparams.cNamedArgs != 1 || pInvkinds[nStep] != INVOKE_PROPERTYPUT ) )
        {
            int nSizeAr = dispparams.cNamedArgs + 1;
            if ( pInvkinds[nStep] == INVOKE_PROPERTYPUT )
                nSizeAr = dispparams.cNamedArgs;

            std::unique_ptr<OLECHAR*[]> saNames(new OLECHAR*[nSizeAr]);
            OLECHAR ** pNames = saNames.get();
            pNames[0] = const_cast<OLECHAR*>(o3tl::toW(aName.getStr()));

            int cNamedArg = 0;
            for ( size_t nInd = 0; nInd < dispparams.cArgs; nInd++ )
            {
                if (auto v = o3tl::tryAccess<NamedArgument>(aParams[nInd]))
                {
                    const NamedArgument& arg = *v;

                    //We put the parameter names in reverse order into the array,
                    //so we can use the DISPID array for DISPPARAMS::rgdispidNamedArgs
                    //The first name in the array is the method name
                    pNames[nSizeAr - 1 - cNamedArg++] = const_cast<OLECHAR*>(o3tl::toW(arg.Name.getStr()));
                }
            }

            arDispidNamedArgs.reset( new DISPID[nSizeAr] );
            HRESULT hr = getTypeInfo()->GetIDsOfNames( pNames, nSizeAr, arDispidNamedArgs.get() );
            if ( hr == E_NOTIMPL )
                hr = m_spDispatch->GetIDsOfNames(IID_NULL, pNames, nSizeAr, LOCALE_USER_DEFAULT, arDispidNamedArgs.get() );

            if ( SUCCEEDED( hr ) )
            {
                if ( pInvkinds[nStep] == DISPATCH_PROPERTYPUT )
                {
                    DISPID*  arIDs = arDispidNamedArgs.get();
                    arIDs[0] = DISPID_PROPERTYPUT;
                    dispparams.rgdispidNamedArgs = arIDs;
                }
                else
                {
                    DISPID*  arIDs = arDispidNamedArgs.get();
                    dispparams.rgdispidNamedArgs = & arIDs[1];
                }
            }
            else if (hr == DISP_E_UNKNOWNNAME)
            {
                 throw IllegalArgumentException(
                     "[automation bridge]One of the named arguments is wrong!",
                     Reference<XInterface>(), 0);
            }
            else
            {
                throw InvocationTargetException(
                    "[automation bridge] ITypeInfo::GetIDsOfNames returned error "
                    + OUString::number(static_cast<sal_Int32>(hr), 16), Reference<XInterface>(), Any());
            }
        }

        //Convert arguments
        ptrArgs.reset(new CComVariant[dispparams.cArgs]);
        ptrRefArgs.reset(new CComVariant[dispparams.cArgs]);
        arArgs = ptrArgs.get();
        arRefArgs = ptrRefArgs.get();

        sal_Int32 nInd = 0;
        try
        {
            sal_Int32 revIndex = 0;
            for ( nInd = 0; nInd < sal_Int32(dispparams.cArgs); nInd++)
            {
                revIndex = dispparams.cArgs - nInd - 1;
                arRefArgs[revIndex].byref = nullptr;
                Any  anyArg;
                if ( nInd < aParams.getLength() )
                    anyArg = aParams.getConstArray()[nInd];

                // Property Put arguments
                if ( anyArg.getValueType() == cppu::UnoType<PropertyPutArgument>::get() )
                {
                    PropertyPutArgument arg;
                    anyArg >>= arg;
                    anyArg = arg.Value;
                }
                // named argument
                if (anyArg.getValueType() == cppu::UnoType<NamedArgument>::get())
                {
                    NamedArgument aNamedArgument;
                    anyArg >>= aNamedArgument;
                    anyArg = aNamedArgument.Value;
                }

                if ( nInd < aParams.getLength() && anyArg.getValueTypeClass() != TypeClass_VOID )
                {
                    anyToVariant( &arArgs[revIndex], anyArg, VT_VARIANT );
                }
                else
                {
                    arArgs[revIndex].vt = VT_ERROR;
                    arArgs[revIndex].scode = DISP_E_PARAMNOTFOUND;
                }
            }
        }
        catch (IllegalArgumentException & e)
        {
            e.ArgumentPosition = ::sal::static_int_cast< sal_Int16, sal_Int32 >( nInd );
            throw;
        }
        catch (CannotConvertException & e)
        {
            e.ArgumentIndex = nInd;
            throw;
        }

        dispparams.rgvarg = arArgs;
        // invoking OLE method
        hInvRes = m_spDispatch->Invoke( dispid,
                                        IID_NULL,
                                        LOCALE_USER_DEFAULT,
                                        ::sal::static_int_cast< WORD, INVOKEKIND >( pInvkinds[nStep] ),
                                        &dispparams,
                                        &varResult,
                                        &excepinfo,
                                        &uArgErr);
    }

    // converting return value and out parameter back to UNO
    if ( SUCCEEDED( hInvRes ) )
        variantToAny( &varResult, aResult, false );
    else
    {
        // map error codes to exceptions
        OUString message;
        switch ( hInvRes )
        {
            case S_OK:
                break;
            case DISP_E_BADPARAMCOUNT:
                throw IllegalArgumentException("[automation bridge] Wrong "
                      "number of arguments. Object returned DISP_E_BADPARAMCOUNT.",
                      nullptr, 0);
                break;
            case DISP_E_BADVARTYPE:
                throw RuntimeException("[automation bridge] One or more "
                      "arguments have the wrong type. Object returned "
                      "DISP_E_BADVARTYPE.", nullptr);
                break;
            case DISP_E_EXCEPTION:
                    message = OUString::Concat("[automation bridge]: ")
                        + std::u16string_view(o3tl::toU(excepinfo.bstrDescription),
                            ::SysStringLen(excepinfo.bstrDescription));
                    throw InvocationTargetException(message, Reference<XInterface>(), Any());
                    break;
            case DISP_E_MEMBERNOTFOUND:
                message = "[automation bridge]: A function with the name \""
                    + aName + "\" is not supported. Object returned "
                    "DISP_E_MEMBERNOTFOUND.";
                throw IllegalArgumentException(message, nullptr, 0);
                break;
            case DISP_E_NONAMEDARGS:
                throw IllegalArgumentException("[automation bridge] Object "
                      "returned DISP_E_NONAMEDARGS",nullptr, ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
                break;
            case DISP_E_OVERFLOW:
                throw CannotConvertException("[automation bridge] Call failed.",
                                             static_cast<XInterface*>(
                    static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::OUT_OF_RANGE, uArgErr);
                break;
            case DISP_E_PARAMNOTFOUND:
                throw IllegalArgumentException("[automation bridge]Call failed."
                                               "Object returned DISP_E_PARAMNOTFOUND.",
                                               nullptr, ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
                break;
            case DISP_E_TYPEMISMATCH:
                throw CannotConvertException("[automation bridge] Call  failed. "
                                             "Object returned DISP_E_TYPEMISMATCH",
                    static_cast<XInterface*>(
                    static_cast<XWeak*>(this)) , TypeClass_UNKNOWN, FailReason::UNKNOWN, uArgErr);
                break;
            case DISP_E_UNKNOWNINTERFACE:
                throw RuntimeException("[automation bridge] Call failed. "
                                           "Object returned DISP_E_UNKNOWNINTERFACE.",nullptr);
                break;
            case DISP_E_UNKNOWNLCID:
                throw RuntimeException("[automation bridge] Call failed. "
                                           "Object returned DISP_E_UNKNOWNLCID.",nullptr);
                break;
            case DISP_E_PARAMNOTOPTIONAL:
                throw CannotConvertException("[automation bridge] Call failed."
                      "Object returned DISP_E_PARAMNOTOPTIONAL",
                            static_cast<XInterface*>(static_cast<XWeak*>(this)),
                                  TypeClass_UNKNOWN, FailReason::NO_DEFAULT_AVAILABLE, uArgErr);
                break;
            default:
                throw RuntimeException();
                break;
        }
    }

    return aResult;
}

sal_Bool SAL_CALL IUnknownWrapper::hasMember( const OUString& aName )
{
    if ( ! m_spDispatch )
    {
        throw RuntimeException(
            "[automation bridge] The object does not have an IDispatch interface");
    }

    o2u_attachCurrentThread();
    DISPID dispid;
    return getDispid( aName, &dispid );
}


// UnoConversionUtilities --------------------------------------------------------------------------------
Reference< XInterface > IUnknownWrapper::createUnoWrapperInstance()
{
    if( m_nUnoWrapperClass == INTERFACE_OLE_WRAPPER_IMPL)
    {
        Reference<XWeak> xWeak= static_cast<XWeak*>( new InterfaceOleWrapper(
                                m_smgr, m_nUnoWrapperClass, m_nComWrapperClass));
        return Reference<XInterface>( xWeak, UNO_QUERY);
    }
    else if( m_nUnoWrapperClass == UNO_OBJECT_WRAPPER_REMOTE_OPT)
    {
        Reference<XWeak> xWeak= static_cast<XWeak*>( new UnoObjectWrapperRemoteOpt(
                                m_smgr, m_nUnoWrapperClass, m_nComWrapperClass));
        return Reference<XInterface>( xWeak, UNO_QUERY);
    }
    else
        return Reference<XInterface>();
}
Reference<XInterface> IUnknownWrapper::createComWrapperInstance()
{
    Reference<XWeak> xWeak= static_cast<XWeak*>( new IUnknownWrapper(
                            m_smgr, m_nUnoWrapperClass, m_nComWrapperClass));
    return Reference<XInterface>( xWeak, UNO_QUERY);
}


void IUnknownWrapper::getMethodInfo(std::u16string_view sName, TypeDescription& methodInfo)
{
    TypeDescription desc= getInterfaceMemberDescOfCurrentCall(sName);
    if( desc.is())
    {
        typelib_TypeDescription* pMember= desc.get();
        if( pMember->eTypeClass == typelib_TypeClass_INTERFACE_METHOD )
            methodInfo= pMember;
    }
}

void IUnknownWrapper::getAttributeInfo(std::u16string_view sName, TypeDescription& attributeInfo)
{
    TypeDescription desc= getInterfaceMemberDescOfCurrentCall(sName);
    if( desc.is())
    {
        typelib_TypeDescription* pMember= desc.get();
        if( pMember->eTypeClass == typelib_TypeClass_INTERFACE_ATTRIBUTE )
        {
            attributeInfo= reinterpret_cast<typelib_InterfaceAttributeTypeDescription*>(pMember)->pAttributeTypeRef;
        }
    }
}
TypeDescription IUnknownWrapper::getInterfaceMemberDescOfCurrentCall(std::u16string_view sName)
{
    TypeDescription ret;

    for (auto const& rType : m_seqTypes)
    {
        TypeDescription _curDesc( rType );
        _curDesc.makeComplete();
        typelib_InterfaceTypeDescription * pInterface= reinterpret_cast<typelib_InterfaceTypeDescription*>(_curDesc.get());
        if( pInterface)
        {
            typelib_InterfaceMemberTypeDescription* pMember= nullptr;
            //find the member description of the current call
            for( int j=0; j < pInterface->nAllMembers; j++)
            {
                typelib_TypeDescriptionReference* pTypeRefMember = pInterface->ppAllMembers[j];
                typelib_TypeDescription* pDescMember= nullptr;
                TYPELIB_DANGER_GET( &pDescMember, pTypeRefMember);

                typelib_InterfaceMemberTypeDescription* pInterfaceMember=
                    reinterpret_cast<typelib_InterfaceMemberTypeDescription*>(pDescMember);
                if( OUString( pInterfaceMember->pMemberName) == sName)
                {
                    pMember= pInterfaceMember;
                    break;
                }
                TYPELIB_DANGER_RELEASE( pDescMember);
            }

            if( pMember)
            {
                ret= &pMember->aBase;
                TYPELIB_DANGER_RELEASE( &pMember->aBase);
            }
        }
        if( ret.is())
            break;
    }
    return ret;
}

bool IUnknownWrapper::isJScriptObject()
{
    if(  m_eJScript == JScriptUndefined)
    {
        CComDispatchDriver disp( m_spDispatch);
        if( disp)
        {
            CComVariant result;
            if( SUCCEEDED(  disp.GetPropertyByName( JSCRIPT_ID_PROPERTY, &result)))
            {
                if(result.vt == VT_BSTR)
                {
                    CComBSTR name( result.bstrVal);
                    name.ToLower();
                    if( name == CComBSTR(JSCRIPT_ID))
                        m_eJScript= IsJScript;
                }
            }
        }
        if( m_eJScript == JScriptUndefined)
            m_eJScript= NoJScript;
    }

    return m_eJScript != NoJScript;
}


/** @internal
    The function ultimately calls IDispatch::Invoke on the wrapped COM object.
    The COM object does not implement UNO Interfaces ( via IDispatch). This
    is the case when the OleObjectFactory service has been used to create a
    component.
    @exception IllegalArgumentException
    @exception CannotConvertException
    @InvocationTargetException
    @RuntimeException
    @BridgeRuntimeError
*/
Any  IUnknownWrapper::invokeWithDispIdComTlb(const OUString& sFuncName,
                                             const Sequence< Any >& Params,
                                             Sequence< sal_Int16 >& OutParamIndex,
                                             Sequence< Any >& OutParam)
{
    // Get type info for the call. It can be a method call or property put or
    // property get operation.
    FuncDesc aFuncDesc(getTypeInfo());
    getFuncDescForInvoke(sFuncName, Params, & aFuncDesc);
    return invokeWithDispIdComTlb( aFuncDesc, sFuncName, Params, OutParamIndex, OutParam );
}

Any  IUnknownWrapper::invokeWithDispIdComTlb(FuncDesc& aFuncDesc,
                                             const OUString& sFuncName,
                                             const Sequence< Any >& Params,
                                             Sequence< sal_Int16 >& OutParamIndex,
                                             Sequence< Any >& OutParam)
{
    Any ret;
    HRESULT result;

    DISPPARAMS      dispparams = {nullptr, nullptr, 0, 0};
    CComVariant     varResult;
    ExcepInfo       excepinfo;
    unsigned int    uArgErr;
    sal_Int32       i = 0;
    sal_Int32 nUnoArgs = Params.getLength();
    DISPID idPropertyPut = DISPID_PROPERTYPUT;
    std::unique_ptr<DISPID[]> arDispidNamedArgs;
    std::unique_ptr<CComVariant[]> ptrArgs;
    std::unique_ptr<CComVariant[]> ptrRefArgs; // referenced arguments
    CComVariant * arArgs = nullptr;
    CComVariant * arRefArgs = nullptr;
    sal_Int32 revIndex = 0;

    //Set the array of DISPIDs for named args if it is a property put operation.
    //If there are other named arguments another array is set later on.
    if (aFuncDesc->invkind == INVOKE_PROPERTYPUT
        || aFuncDesc->invkind == INVOKE_PROPERTYPUTREF)
        dispparams.rgdispidNamedArgs = & idPropertyPut;

    //Determine the number of named arguments
    for (int iParam = 0; iParam < nUnoArgs; iParam ++)
    {
        const Any & curArg = Params[iParam];
        if (curArg.getValueType() == cppu::UnoType<NamedArgument>::get())
            dispparams.cNamedArgs ++;
    }
    //In a property put operation a property value is a named argument (DISPID_PROPERTYPUT).
    //Therefore the number of named arguments is increased by one.
    //Although named, the argument is not named in an actual language, such as Basic,
    //therefore it is never a com.sun.star.bridge.oleautomation.NamedArgument
    if (aFuncDesc->invkind == DISPATCH_PROPERTYPUT
        || aFuncDesc->invkind == DISPATCH_PROPERTYPUTREF)
        dispparams.cNamedArgs ++;

    //Determine the number of all arguments and named arguments
    if (aFuncDesc->cParamsOpt == -1)
    {
        //Attribute vararg is set on this method. "Unlimited" number of args
        //supported. There can be no optional or defaultvalue on any of the arguments.
        dispparams.cArgs = nUnoArgs;
    }
    else
    {
        //If there are named arguments, then the dispparams.cArgs
        //is the number of supplied args, otherwise it is the expected number.
        if (dispparams.cNamedArgs)
            dispparams.cArgs = nUnoArgs;
        else
            dispparams.cArgs = aFuncDesc->cParams;
    }

    //check if there are not too many arguments supplied
    if (::sal::static_int_cast< sal_uInt32, int >( nUnoArgs ) > dispparams.cArgs)
    {
        throw IllegalArgumentException(
            "[automation bridge] There are too many arguments for this method",
            Reference<XInterface>(), static_cast<sal_Int16>(dispparams.cArgs));
    }

    //Set up the array of DISPIDs (DISPPARAMS::rgdispidNamedArgs)
    //for the named arguments.
    //If there is only one named arg and if it is because of a property put
    //operation, then we need not set up the DISPID array.
    if (dispparams.cNamedArgs > 0 &&
        ! (dispparams.cNamedArgs == 1 &&
           (aFuncDesc->invkind == INVOKE_PROPERTYPUT ||
            aFuncDesc->invkind == INVOKE_PROPERTYPUTREF)))
    {
        //set up an array containing the member and parameter names
        //which is then used in ITypeInfo::GetIDsOfNames
        //First determine the size of the array of names which is passed to
        //ITypeInfo::GetIDsOfNames. It must hold the method names + the named
        //args.
        int nSizeAr = dispparams.cNamedArgs + 1;
        if (aFuncDesc->invkind == INVOKE_PROPERTYPUT
            || aFuncDesc->invkind == INVOKE_PROPERTYPUTREF)
        {
            nSizeAr = dispparams.cNamedArgs; //counts the DISID_PROPERTYPUT
        }

        std::unique_ptr<OLECHAR*[]> saNames(new OLECHAR*[nSizeAr]);
        OLECHAR ** arNames = saNames.get();
        arNames[0] = const_cast<OLECHAR*>(o3tl::toW(sFuncName.getStr()));

        int cNamedArg = 0;
        for (size_t iParams = 0; iParams < dispparams.cArgs; iParams ++)
        {
            const Any &  curArg = Params[iParams];
            if (auto v = o3tl::tryAccess<NamedArgument>(curArg))
            {
                const NamedArgument& arg = *v;
                //We put the parameter names in reverse order into the array,
                //so we can use the DISPID array for DISPPARAMS::rgdispidNamedArgs
                //The first name in the array is the method name
                arNames[nSizeAr - 1 - cNamedArg++] = const_cast<OLECHAR*>(o3tl::toW(arg.Name.getStr()));
            }
        }

        //Prepare the array of DISPIDs for ITypeInfo::GetIDsOfNames
        //it must be big enough to contain the DISPIDs of the member + parameters
        arDispidNamedArgs.reset(new DISPID[nSizeAr]);
        HRESULT hr = getTypeInfo()->GetIDsOfNames(arNames, nSizeAr,
                                                  arDispidNamedArgs.get());
        if ( hr == E_NOTIMPL )
            hr = m_spDispatch->GetIDsOfNames(IID_NULL, arNames, nSizeAr, LOCALE_USER_DEFAULT, arDispidNamedArgs.get() );

        if (hr == S_OK)
        {
            // In a "property put" operation, the property value is a named param with the
            //special DISPID DISPID_PROPERTYPUT
            if (aFuncDesc->invkind == DISPATCH_PROPERTYPUT
                || aFuncDesc->invkind == DISPATCH_PROPERTYPUTREF)
            {
                //Element at index 0 in the DISPID array must be DISPID_PROPERTYPUT
                //The first item in the array arDispidNamedArgs is the DISPID for
                //the method. We replace it with DISPID_PROPERTYPUT.
                DISPID*  arIDs = arDispidNamedArgs.get();
                arIDs[0] = DISPID_PROPERTYPUT;
                dispparams.rgdispidNamedArgs = arIDs;
            }
            else
            {
                //The first item in the array arDispidNamedArgs is the DISPID for
                //the method. It must be removed
                DISPID*  arIDs = arDispidNamedArgs.get();
                dispparams.rgdispidNamedArgs = & arIDs[1];
            }
        }
        else if (hr == DISP_E_UNKNOWNNAME)
        {
             throw IllegalArgumentException(
                 "[automation bridge]One of the named arguments is wrong!",
                 Reference<XInterface>(), 0);
        }
        else
        {
            throw InvocationTargetException(
                "[automation bridge] ITypeInfo::GetIDsOfNames returned error "
                + OUString::number(static_cast<sal_Int32>(hr), 16), Reference<XInterface>(), Any());
        }
    }

    //Convert arguments
    ptrArgs.reset(new CComVariant[dispparams.cArgs]);
    ptrRefArgs.reset(new CComVariant[dispparams.cArgs]);
    arArgs = ptrArgs.get();
    arRefArgs = ptrRefArgs.get();
    try
    {
        for (i = 0; i < static_cast<sal_Int32>(dispparams.cArgs); i++)
        {
            revIndex= dispparams.cArgs - i -1;
            arRefArgs[revIndex].byref=nullptr;
            Any  anyArg;
            if ( i < nUnoArgs)
                anyArg= Params.getConstArray()[i];

            unsigned short paramFlags = PARAMFLAG_FOPT | PARAMFLAG_FIN;
            VARTYPE varType = VT_VARIANT;
            if (aFuncDesc->cParamsOpt != -1 || aFuncDesc->cParams != (i + 1))
            {
                paramFlags = aFuncDesc->lprgelemdescParam[i].paramdesc.wParamFlags;
                varType = getElementTypeDesc(&aFuncDesc->lprgelemdescParam[i].tdesc);
            }

            // Make sure that there is a UNO parameter for every
            // expected parameter. If there is no UNO parameter where the
            // called function expects one, then it must be optional. Otherwise
            // it's a UNO programming error.
            if (i  >= nUnoArgs && !(paramFlags & PARAMFLAG_FOPT))
            {
                throw IllegalArgumentException(
                    ("ole automation bridge: The called function expects an argument at position: "
                     + OUString::number(i) + " (index starting at 0)."),
                    Reference<XInterface>(), static_cast<sal_Int16>(i));
            }

            // Property Put arguments
            if (anyArg.getValueType() == cppu::UnoType<PropertyPutArgument>::get())
            {
                PropertyPutArgument arg;
                anyArg >>= arg;
                anyArg = arg.Value;
            }
            // named argument
            if (anyArg.getValueType() == cppu::UnoType<NamedArgument>::get())
            {
                NamedArgument aNamedArgument;
                anyArg >>= aNamedArgument;
                anyArg = aNamedArgument.Value;
            }
            // out param
            if (paramFlags & PARAMFLAG_FOUT &&
                ! (paramFlags & PARAMFLAG_FIN)  )
            {
                VARTYPE type = ::sal::static_int_cast< VARTYPE, int >( varType ^ VT_BYREF );
                if (i < nUnoArgs)
                {
                    arRefArgs[revIndex].vt= type;
                }
                else
                {
                    //optional arg
                    arRefArgs[revIndex].vt = VT_ERROR;
                    arRefArgs[revIndex].scode = DISP_E_PARAMNOTFOUND;
                }
                if( type == VT_VARIANT )
                {
                    arArgs[revIndex].vt= VT_VARIANT | VT_BYREF;
                    arArgs[revIndex].byref= &arRefArgs[revIndex];
                }
                else
                {
                    arArgs[revIndex].vt= varType;
                    if (type == VT_DECIMAL)
                        arArgs[revIndex].byref= & arRefArgs[revIndex].decVal;
                    else
                        arArgs[revIndex].byref= & arRefArgs[revIndex].byref;
                }
            }
            // in/out  + in byref params
            else if (varType & VT_BYREF)
            {
                VARTYPE type = ::sal::static_int_cast< VARTYPE, int >( varType ^ VT_BYREF );
                CComVariant var;

                if (i < nUnoArgs && anyArg.getValueTypeClass() != TypeClass_VOID)
                {
                    anyToVariant( & arRefArgs[revIndex], anyArg, type);
                }
                else if (paramFlags & PARAMFLAG_FHASDEFAULT)
                {
                    //optional arg with default
                    VariantCopy( & arRefArgs[revIndex],
                                & aFuncDesc->lprgelemdescParam[i].paramdesc.
                                pparamdescex->varDefaultValue);
                }
                else
                {
                    //optional arg
                    //e.g: call func(x) in basic : func() ' no arg supplied
                    OSL_ASSERT(paramFlags & PARAMFLAG_FOPT);
                    arRefArgs[revIndex].vt = VT_ERROR;
                    arRefArgs[revIndex].scode = DISP_E_PARAMNOTFOUND;
                }

                // Set the converted arguments in the array which will be
                // DISPPARAMS::rgvarg
                // byref arg VT_XXX |VT_BYREF
                arArgs[revIndex].vt = varType;
                if (revIndex == 0 && aFuncDesc->invkind == INVOKE_PROPERTYPUT)
                {
                    arArgs[revIndex] = arRefArgs[revIndex];
                }
                else if (type == VT_DECIMAL)
                {
                    arArgs[revIndex].byref= & arRefArgs[revIndex].decVal;
                }
                else if (type == VT_VARIANT)
                {
                    if ( ! (paramFlags & PARAMFLAG_FOUT))
                        arArgs[revIndex] = arRefArgs[revIndex];
                    else
                        arArgs[revIndex].byref = & arRefArgs[revIndex];
                }
                else
                {
                    arArgs[revIndex].byref = & arRefArgs[revIndex].byref;
                    arArgs[revIndex].vt = ::sal::static_int_cast< VARTYPE, int >( arRefArgs[revIndex].vt | VT_BYREF );
                }

            }
            // in parameter no VT_BYREF except for array, interfaces
            else
            {   // void any stands for optional param
                if (i < nUnoArgs && anyArg.getValueTypeClass() != TypeClass_VOID)
                {
                    anyToVariant( & arArgs[revIndex], anyArg, varType);
                }
                //optional arg but no void any supplied
                //Basic:  obj.func() ' first parameter left out because it is optional
                else if (paramFlags & PARAMFLAG_FHASDEFAULT)
                {
                    //optional arg with default either as direct arg : VT_XXX or
                    VariantCopy( & arArgs[revIndex],
                        & aFuncDesc->lprgelemdescParam[i].paramdesc.
                            pparamdescex->varDefaultValue);
                }
                else if (paramFlags & PARAMFLAG_FOPT)
                {
                    arArgs[revIndex].vt = VT_ERROR;
                    arArgs[revIndex].scode = DISP_E_PARAMNOTFOUND;
                }
                else
                {
                    arArgs[revIndex].vt = VT_EMPTY;
                    arArgs[revIndex].lVal = 0;
                }
            }
        }
    }
    catch (IllegalArgumentException & e)
    {
        e.ArgumentPosition = ::sal::static_int_cast< sal_Int16, sal_Int32 >( i );
        throw;
    }
    catch (CannotConvertException & e)
    {
        e.ArgumentIndex = i;
        throw;
    }
    dispparams.rgvarg= arArgs;
    // invoking OLE method
    result = m_spDispatch->Invoke(aFuncDesc->memid,
                                 IID_NULL,
                                 LOCALE_USER_DEFAULT,
                                 ::sal::static_int_cast< WORD, INVOKEKIND >( aFuncDesc->invkind ),
                                 &dispparams,
                                 &varResult,
                                 &excepinfo,
                                 &uArgErr);

    // converting return value and out parameter back to UNO
    if (result == S_OK)
    {

        // allocate space for the out param Sequence and indices Sequence
        int outParamsCount= 0; // includes in/out parameter
        for (int j = 0; j < aFuncDesc->cParams; j++)
        {
            if (aFuncDesc->lprgelemdescParam[j].paramdesc.wParamFlags &
                PARAMFLAG_FOUT)
                outParamsCount++;
        }

        OutParamIndex.realloc(outParamsCount);
        OutParam.realloc(outParamsCount);
        // Convert out params
        if (outParamsCount)
        {
            auto pOutParamIndex = OutParamIndex.getArray();
            auto pOutParam = OutParam.getArray();
            int outParamIndex=0;
            for (int paramIndex = 0; paramIndex < nUnoArgs; paramIndex ++)
            {
                //Determine the index within the method signature
                int realParamIndex = paramIndex;
                int revParamIndex = dispparams.cArgs - paramIndex - 1;
                if (Params[paramIndex].getValueType()
                    == cppu::UnoType<NamedArgument>::get())
                {
                    //dispparams.rgdispidNamedArgs contains the mapping from index
                    //of named args list to index of parameter list
                    realParamIndex = dispparams.rgdispidNamedArgs[revParamIndex];
                }

                // no named arg, always come before named args
                if (! (aFuncDesc->lprgelemdescParam[realParamIndex].paramdesc.wParamFlags
                       & PARAMFLAG_FOUT))
                    continue;
                Any outAny;
                // variantToAny is called with the "reduce range" parameter set to sal_False.
                // That causes VT_I4 values not to be converted down to a "lower" type. That
                // feature exist for JScript only because it only uses VT_I4 for integer types.
                try
                {
                    variantToAny( & arRefArgs[revParamIndex], outAny, false );
                }
                catch (IllegalArgumentException & e)
                {
                    e.ArgumentPosition = static_cast<sal_Int16>(paramIndex);
                    throw;
                }
                catch (CannotConvertException & e)
                {
                    e.ArgumentIndex = paramIndex;
                    throw;
                }
                pOutParam[outParamIndex] = outAny;
                pOutParamIndex[outParamIndex] = ::sal::static_int_cast< sal_Int16, int >( paramIndex );
                outParamIndex++;
            }
            OutParam.realloc(outParamIndex);
            OutParamIndex.realloc(outParamIndex);
        }
        // Return value
        variantToAny(&varResult, ret, false);
    }

    // map error codes to exceptions
    OUString message;
    switch (result)
    {
        case S_OK:
            break;
        case DISP_E_BADPARAMCOUNT:
            throw IllegalArgumentException("[automation bridge] Wrong "
                  "number of arguments. Object returned DISP_E_BADPARAMCOUNT.",
                  nullptr, 0);
            break;
        case DISP_E_BADVARTYPE:
            throw RuntimeException("[automation bridge] One or more "
                  "arguments have the wrong type. Object returned "
                  "DISP_E_BADVARTYPE.", nullptr);
            break;
        case DISP_E_EXCEPTION:
                message = OUString::Concat("[automation bridge]: ")
                    + std::u16string_view(o3tl::toU(excepinfo.bstrDescription),
                                    ::SysStringLen(excepinfo.bstrDescription));

                throw InvocationTargetException(message, Reference<XInterface>(), Any());
            break;
        case DISP_E_MEMBERNOTFOUND:
            message = "[automation bridge]: A function with the name \""
                + sFuncName + "\" is not supported. Object returned "
                "DISP_E_MEMBERNOTFOUND.";
            throw IllegalArgumentException(message, nullptr, 0);
            break;
        case DISP_E_NONAMEDARGS:
            throw IllegalArgumentException("[automation bridge] Object "
                  "returned DISP_E_NONAMEDARGS",nullptr, ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
            break;
        case DISP_E_OVERFLOW:
            throw CannotConvertException("[automation bridge] Call failed.",
                                         static_cast<XInterface*>(
                static_cast<XWeak*>(this)), TypeClass_UNKNOWN, FailReason::OUT_OF_RANGE, uArgErr);
            break;
        case DISP_E_PARAMNOTFOUND:
            throw IllegalArgumentException("[automation bridge]Call failed."
                                           "Object returned DISP_E_PARAMNOTFOUND.",
                                           nullptr, ::sal::static_int_cast< sal_Int16, unsigned int >( uArgErr ));
            break;
        case DISP_E_TYPEMISMATCH:
            throw CannotConvertException("[automation bridge] Call  failed. "
                                         "Object returned DISP_E_TYPEMISMATCH",
                static_cast<XInterface*>(
                static_cast<XWeak*>(this)) , TypeClass_UNKNOWN, FailReason::UNKNOWN, uArgErr);
            break;
        case DISP_E_UNKNOWNINTERFACE:
            throw RuntimeException("[automation bridge] Call failed. "
                                       "Object returned DISP_E_UNKNOWNINTERFACE.",nullptr);
            break;
        case DISP_E_UNKNOWNLCID:
            throw RuntimeException("[automation bridge] Call failed. "
                                       "Object returned DISP_E_UNKNOWNLCID.",nullptr);
            break;
        case DISP_E_PARAMNOTOPTIONAL:
            throw CannotConvertException("[automation bridge] Call failed."
                  "Object returned DISP_E_PARAMNOTOPTIONAL",
                        static_cast<XInterface*>(static_cast<XWeak*>(this)),
                              TypeClass_UNKNOWN, FailReason::NO_DEFAULT_AVAILABLE, uArgErr);
            break;
        default:
            throw RuntimeException();
            break;
    }

    return ret;
}

void IUnknownWrapper::getFuncDescForInvoke(const OUString & sFuncName,
                                           const Sequence<Any> & seqArgs,
                                           FUNCDESC** pFuncDesc)
{
    int nUnoArgs = seqArgs.getLength();
    const Any * arArgs = seqArgs.getConstArray();
    ITypeInfo* pInfo = getTypeInfo();

    //If the last of the positional arguments is a PropertyPutArgument
    //then obtain the type info for the property put operation.

    //The property value is always the last argument, in a positional argument list
    //or in a list of named arguments. A PropertyPutArgument is actually a named argument
    //hence it must not be put in an extra NamedArgument structure
    if (nUnoArgs > 0 &&
        arArgs[nUnoArgs - 1].getValueType() == cppu::UnoType<PropertyPutArgument>::get())
    {
        // DISPATCH_PROPERTYPUT
        FuncDesc aDescGet(pInfo);
        FuncDesc aDescPut(pInfo);
        VarDesc aVarDesc(pInfo);
        getPropDesc(sFuncName, & aDescGet, & aDescPut, & aVarDesc);
        if ( ! aDescPut)
        {
            throw IllegalArgumentException(
                "[automation bridge] The object does not have a writeable property: "
                + sFuncName, Reference<XInterface>(), 0);
        }
        *pFuncDesc = aDescPut.Detach();
    }
    else
    {   // DISPATCH_METHOD
        FuncDesc aFuncDesc(pInfo);
        getFuncDesc(sFuncName, & aFuncDesc);
        if ( ! aFuncDesc)
        {
            // Fallback: DISPATCH_PROPERTYGET can mostly be called as
            // DISPATCH_METHOD
            ITypeInfo * pTypeInfo = getTypeInfo();
            FuncDesc aDescPut(pTypeInfo);
            VarDesc aVarDesc(pTypeInfo);
            getPropDesc(sFuncName, & aFuncDesc, & aDescPut, & aVarDesc);
            if ( ! aFuncDesc )
            {
                throw IllegalArgumentException(
                    "[automation bridge] The object does not have a function"
                    " or readable property \""
                    + sFuncName + "\"", Reference<XInterface>(), 0);
            }
        }
        *pFuncDesc = aFuncDesc.Detach();
    }
}
bool IUnknownWrapper::getDispid(const OUString& sFuncName, DISPID * id)
{
    OSL_ASSERT(m_spDispatch);
    LPOLESTR lpsz = const_cast<LPOLESTR> (o3tl::toW(sFuncName.getStr()));
    HRESULT hr = m_spDispatch->GetIDsOfNames(IID_NULL, &lpsz, 1, LOCALE_USER_DEFAULT, id);
    return hr == S_OK;
}
void IUnknownWrapper::getFuncDesc(const OUString & sFuncName, FUNCDESC ** pFuncDesc)

{
    OSL_ASSERT( * pFuncDesc == nullptr);
    buildComTlbIndex();
    typedef TLBFuncIndexMap::const_iterator cit;
    //We assume there is only one entry with the function name. A property
    //would have two entries.
    cit itIndex= m_mapComFunc.find(sFuncName);
    if (itIndex == m_mapComFunc.end())
    {
        //try case insensitive with IDispatch::GetIDsOfNames
        DISPID id;
        if (getDispid(sFuncName, &id))
        {
            CComBSTR memberName;
            unsigned int pcNames=0;
            // get the case sensitive name
            if( SUCCEEDED(getTypeInfo()->GetNames( id, & memberName, 1, &pcNames)))
            {
                //get the associated index and add an entry to the map
                //with the name sFuncName which differs in the casing of the letters to
                //the actual name as obtained from ITypeInfo
                OUString sRealName(o3tl::toU(LPCOLESTR(memberName)));
                cit itOrg  = m_mapComFunc.find(sRealName);
                OSL_ASSERT(itOrg != m_mapComFunc.end());
                // maybe this is a property, if so we need
                // to store either both id's ( put/get ) or
                // just the get. Storing both is more consistent
                std::pair<cit, cit> pItems = m_mapComFunc.equal_range( sRealName );
                for ( ;pItems.first != pItems.second; ++pItems.first )
                    m_mapComFunc.insert( TLBFuncIndexMap::value_type ( std::make_pair(sFuncName, pItems.first->second ) ));
                itIndex =
                    m_mapComFunc.find( sFuncName );
            }
        }
    }

#if OSL_DEBUG_LEVEL >= 1
    // There must only be one entry if sFuncName represents a function or two
    // if it is a property
    std::pair<cit, cit> p = m_mapComFunc.equal_range(sFuncName.toAsciiLowerCase());
    int numEntries = 0;
    for ( ;p.first != p.second; p.first ++, numEntries ++);
    OSL_ASSERT( ! (numEntries > 3) );
#endif
    if( itIndex != m_mapComFunc.end())
    {
        ITypeInfo* pType= getTypeInfo();
        FUNCDESC * pDesc = nullptr;
        if (!SUCCEEDED(pType->GetFuncDesc(itIndex->second, & pDesc)))
        {
            throw BridgeRuntimeError("[automation bridge] Could not get "
                                     "FUNCDESC for " + sFuncName);
        }
        if (pDesc->invkind == INVOKE_FUNC)
        {
            (*pFuncDesc) = pDesc;
        }
        else
        {
            pType->ReleaseFuncDesc(pDesc);
        }
    }
   //else no entry found for sFuncName, pFuncDesc will not be filled in
}

void IUnknownWrapper::getPropDesc(const OUString & sFuncName, FUNCDESC ** pFuncDescGet,
                                  FUNCDESC** pFuncDescPut, VARDESC** pVarDesc)
{
    OSL_ASSERT( * pFuncDescGet == nullptr && * pFuncDescPut == nullptr);
    buildComTlbIndex();
    typedef TLBFuncIndexMap::const_iterator cit;
    std::pair<cit, cit> p = m_mapComFunc.equal_range(sFuncName);
    if (p.first == m_mapComFunc.end())
    {
        //try case insensitive with IDispatch::GetIDsOfNames
        DISPID id;
        if (getDispid(sFuncName, &id))
        {
            CComBSTR memberName;
            unsigned int pcNames=0;
            // get the case sensitive name
            if( SUCCEEDED(getTypeInfo()->GetNames( id, & memberName, 1, &pcNames)))
            {
                //As opposed to getFuncDesc, we do not add the value because we would
                // need to find the get and set description for the property. This would
                //mean to iterate over all FUNCDESCs again.
                p = m_mapComFunc.equal_range(OUString(o3tl::toU(LPCOLESTR(memberName))));
            }
        }
    }

    for ( int i = 0 ;p.first != p.second; p.first ++, i ++)
    {
        // There are a maximum of two entries, property put and property get
        OSL_ASSERT( ! (i > 2) );
        ITypeInfo* pType= getTypeInfo();
        FUNCDESC * pFuncDesc = nullptr;
        if (SUCCEEDED( pType->GetFuncDesc(p.first->second, & pFuncDesc)))
        {
            if (pFuncDesc->invkind == INVOKE_PROPERTYGET)
            {
                (*pFuncDescGet) = pFuncDesc;
            }
            else if (pFuncDesc->invkind == INVOKE_PROPERTYPUT ||
                     pFuncDesc->invkind == INVOKE_PROPERTYPUTREF)
            {
                //a property can have 3 entries, put, put ref, get
                // If INVOKE_PROPERTYPUTREF or INVOKE_PROPERTYPUT is used
                //depends on what is found first.
                if ( * pFuncDescPut)
                {
                    //we already have found one
                    pType->ReleaseFuncDesc(pFuncDesc);
                }
                else
                {
                    (*pFuncDescPut) = pFuncDesc;
                }
            }
            else
            {
                pType->ReleaseFuncDesc(pFuncDesc);
            }
        }
        //ITypeInfo::GetFuncDesc may even provide a funcdesc for a VARDESC
        // with invkind = INVOKE_FUNC. Since this function should only return
        //a value for a real property (XInvokation::hasMethod, ..::hasProperty
        //we need to make sure that sFuncName represents a real property.
        VARDESC * pVD = nullptr;
        if (SUCCEEDED(pType->GetVarDesc(p.first->second, & pVD)))
            (*pVarDesc) = pVD;
    }
   //else no entry for sFuncName, pFuncDesc will not be filled in
}

VARTYPE IUnknownWrapper::getUserDefinedElementType( ITypeInfo* pTypeInfo, const DWORD nHrefType )
{
    VARTYPE _type( VT_NULL );
    if ( pTypeInfo )
    {
        CComPtr<ITypeInfo> spRefInfo;
        pTypeInfo->GetRefTypeInfo( nHrefType, &spRefInfo.p );
        if ( spRefInfo )
        {
            TypeAttr attr( spRefInfo );
            spRefInfo->GetTypeAttr( &attr );
            if ( attr->typekind == TKIND_ENUM )
            {
                // We use the type of the first enum value.
                if ( attr->cVars == 0 )
                {
                    throw BridgeRuntimeError("[automation bridge] Could not obtain type description");
                }
                VarDesc var( spRefInfo );
                spRefInfo->GetVarDesc( 0, &var );
                _type = var->lpvarValue->vt;
            }
            else if ( attr->typekind == TKIND_INTERFACE )
            {
                _type = VT_UNKNOWN;
            }
            else if ( attr->typekind == TKIND_DISPATCH )
            {
                _type = VT_DISPATCH;
            }
            else if ( attr->typekind == TKIND_ALIAS )
            {
                // TKIND_ALIAS is a type that is an alias for another type. So get that alias type.
                _type = getUserDefinedElementType( pTypeInfo, attr->tdescAlias.hreftype );
            }
            else
            {
                throw BridgeRuntimeError( "[automation bridge] Unhandled user defined type." );
            }
        }
    }
    return _type;
}

VARTYPE IUnknownWrapper::getElementTypeDesc(const TYPEDESC *desc)
{
    VARTYPE _type( VT_NULL );

    if (desc->vt == VT_PTR)
    {
        _type = getElementTypeDesc(desc->lptdesc);
        _type |= VT_BYREF;
    }
    else if (desc->vt == VT_SAFEARRAY)
    {
        _type = getElementTypeDesc(desc->lptdesc);
        _type |= VT_ARRAY;
    }
    else if (desc->vt == VT_USERDEFINED)
    {
        ITypeInfo* thisInfo = getTypeInfo(); //kept by this instance
        _type = getUserDefinedElementType( thisInfo, desc->hreftype );
    }
    else
    {
        _type = desc->vt;
    }
    return _type;
}

void IUnknownWrapper::buildComTlbIndex()
{
    if ( ! m_bComTlbIndexInit)
    {
        MutexGuard guard(getBridgeMutex());
        {
            if ( ! m_bComTlbIndexInit)
            {
                OUString sError;
                ITypeInfo* pType= getTypeInfo();
                TypeAttr typeAttr(pType);
                if( SUCCEEDED( pType->GetTypeAttr( &typeAttr)))
                {
                    for( WORD i= 0; i < typeAttr->cFuncs; i++)
                    {
                        FuncDesc funcDesc(pType);
                        if( SUCCEEDED( pType->GetFuncDesc( i, &funcDesc)))
                        {
                            CComBSTR memberName;
                            unsigned int pcNames=0;
                            if( SUCCEEDED(pType->GetNames( funcDesc->memid, & memberName, 1, &pcNames)))
                            {
                                OUString usName(o3tl::toU(LPCOLESTR(memberName)));
                                m_mapComFunc.emplace(usName, i);
                            }
                            else
                            {
                                sError = "[automation bridge] IUnknownWrapper::buildComTlbIndex, "
                                         "ITypeInfo::GetNames failed.";
                            }
                        }
                        else
                            sError = "[automation bridge] IUnknownWrapper::buildComTlbIndex, "
                                     "ITypeInfo::GetFuncDesc failed.";
                    }

                    //If we create an Object in JScript and a property then it
                    //has VARDESC instead of FUNCDESC
                    for (WORD i = 0; i < typeAttr->cVars; i++)
                    {
                        VarDesc varDesc(pType);
                        if (SUCCEEDED(pType->GetVarDesc(i, & varDesc)))
                        {
                            CComBSTR memberName;
                            unsigned int pcNames = 0;
                            if (SUCCEEDED(pType->GetNames(varDesc->memid, & memberName, 1, &pcNames)))
                            {
                                if (varDesc->varkind == VAR_DISPATCH)
                                {
                                    OUString usName(o3tl::toU(LPCOLESTR(memberName)));
                                    m_mapComFunc.emplace(usName, i);
                                }
                            }
                            else
                            {
                                sError = "[automation bridge] IUnknownWrapper::buildComTlbIndex, "
                                         "ITypeInfo::GetNames failed.";
                            }
                        }
                        else
                            sError = "[automation bridge] IUnknownWrapper::buildComTlbIndex, "
                                     "ITypeInfo::GetVarDesc failed.";

                    }
                }
                else
                    sError = "[automation bridge] IUnknownWrapper::buildComTlbIndex, "
                             "ITypeInfo::GetTypeAttr failed.";

                if (sError.getLength())
                {
                    throw BridgeRuntimeError(sError);
                }

                m_bComTlbIndexInit = true;
            }
        }
    }
}

ITypeInfo* IUnknownWrapper::getTypeInfo()
{
    if( !m_spDispatch)
    {
        throw BridgeRuntimeError("The object has no IDispatch interface!");
    }

    if( !m_spTypeInfo )
    {
        MutexGuard guard(getBridgeMutex());
        if( ! m_spTypeInfo)
        {
            CComPtr< ITypeInfo > spType;
            if( !SUCCEEDED( m_spDispatch->GetTypeInfo( 0, LOCALE_USER_DEFAULT, &spType.p)))
            {
                throw BridgeRuntimeError("[automation bridge]The dispatch object does not "
                                         "support ITypeInfo!");
            }

            OSL_DOUBLE_CHECKED_LOCKING_MEMORY_BARRIER();

            //If this is a dual interface then TYPEATTR::typekind is usually TKIND_INTERFACE
            //We need to get the type description for TKIND_DISPATCH
            TypeAttr typeAttr(spType.p);
            if( SUCCEEDED(spType->GetTypeAttr( &typeAttr)))
            {
                if (typeAttr->typekind == TKIND_INTERFACE &&
                    typeAttr->wTypeFlags & TYPEFLAG_FDUAL)
                {
                    HREFTYPE refDispatch;
                    if (!SUCCEEDED(spType->GetRefTypeOfImplType(::sal::static_int_cast< UINT, int >( -1 ), &refDispatch)))
                    {
                        throw BridgeRuntimeError(
                            "[automation bridge] Could not obtain type information "
                            "for dispatch interface." );
                    }
                    CComPtr<ITypeInfo> spTypeDisp;
                    if (SUCCEEDED(spType->GetRefTypeInfo(refDispatch, & spTypeDisp)))
                        m_spTypeInfo= spTypeDisp;
                }
                else if (typeAttr->typekind == TKIND_DISPATCH)
                {
                    m_spTypeInfo= spType;
                }
                else
                {
                    throw BridgeRuntimeError(
                        "[automation bridge] Automation object does not "
                        "provide type information.");
                }
            }
        }
    }
    return m_spTypeInfo;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
