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

#include <stdlib.h>

#include <algorithm>
#include <string_view>
#include <unordered_map>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/script/XDefaultMethod.hpp>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/util/SearchAlgorithms2.hpp>

#include <comphelper/processfactory.hxx>
#include <comphelper/string.hxx>
#include <o3tl/safeint.hxx>
#include <sal/log.hxx>

#include <tools/wldcrd.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>

#include <rtl/math.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/character.hxx>

#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>

#include <unicode/regex.h>

#include <basic/sbuno.hxx>

#include <codegen.hxx>
#include "comenumwrapper.hxx"
#include "ddectrl.hxx"
#include "dllmgr.hxx"
#include <errobject.hxx>
#include <image.hxx>
#include <iosys.hxx>
#include <opcodes.hxx>
#include <runtime.hxx>
#include <sb.hxx>
#include <sbintern.hxx>
#include <sbprop.hxx>
#include <sbunoobj.hxx>
#include <basic/codecompletecache.hxx>
#include <memory>

using com::sun::star::uno::Reference;

using namespace com::sun::star::uno;
using namespace com::sun::star::container;
using namespace com::sun::star::lang;
using namespace com::sun::star::script;

using namespace ::com::sun::star;

#if HAVE_FEATURE_SCRIPTING

static void lcl_clearImpl( SbxVariableRef const & refVar, SbxDataType const & eType );
static void lcl_eraseImpl( SbxVariableRef const & refVar, bool bVBAEnabled );

namespace
{
class ScopedWritableGuard
{
public:
    ScopedWritableGuard(const SbxVariableRef& rVar, bool bMakeWritable)
        : m_rVar(bMakeWritable && !rVar->CanWrite() ? rVar : SbxVariableRef())
    {
        if (m_rVar)
        {
            m_rVar->SetFlag(SbxFlagBits::Write);
        }
    }
    ~ScopedWritableGuard()
    {
        if (m_rVar)
        {
            m_rVar->ResetFlag(SbxFlagBits::Write);
        }
    }

private:
    SbxVariableRef m_rVar;
};
}

bool SbiRuntime::isVBAEnabled()
{
    SbiInstance* pInst = GetSbData()->pInst;
    return pInst && pInst->pRun && pInst->pRun->bVBAEnabled;
}

void StarBASIC::SetVBAEnabled( bool bEnabled )
{
    if ( bDocBasic )
    {
        bVBAEnabled = bEnabled;
    }
}

bool StarBASIC::isVBAEnabled() const
{
    return bDocBasic && (bVBAEnabled || SbiRuntime::isVBAEnabled());
}

struct SbiArgv {                   // Argv stack:
    SbxArrayRef    refArgv;             // Argv
    short nArgc;                        // Argc

    SbiArgv(SbxArrayRef refArgv_, short nArgc_) :
        refArgv(std::move(refArgv_)),
        nArgc(nArgc_) {}
};

struct SbiGosub {              // GOSUB-Stack:
    const sal_uInt8* pCode;         // Return-Pointer
    sal_uInt16 nStartForLvl;        // #118235: For Level in moment of gosub

    SbiGosub(const sal_uInt8* pCode_, sal_uInt16 nStartForLvl_) :
        pCode(pCode_),
        nStartForLvl(nStartForLvl_) {}
};

const SbiRuntime::pStep0 SbiRuntime::aStep0[] = { // all opcodes without operands
    &SbiRuntime::StepNOP,
    &SbiRuntime::StepEXP,
    &SbiRuntime::StepMUL,
    &SbiRuntime::StepDIV,
    &SbiRuntime::StepMOD,
    &SbiRuntime::StepPLUS,
    &SbiRuntime::StepMINUS,
    &SbiRuntime::StepNEG,
    &SbiRuntime::StepEQ,
    &SbiRuntime::StepNE,
    &SbiRuntime::StepLT,
    &SbiRuntime::StepGT,
    &SbiRuntime::StepLE,
    &SbiRuntime::StepGE,
    &SbiRuntime::StepIDIV,
    &SbiRuntime::StepAND,
    &SbiRuntime::StepOR,
    &SbiRuntime::StepXOR,
    &SbiRuntime::StepEQV,
    &SbiRuntime::StepIMP,
    &SbiRuntime::StepNOT,
    &SbiRuntime::StepCAT,

    &SbiRuntime::StepLIKE,
    &SbiRuntime::StepIS,
    // load/save
    &SbiRuntime::StepARGC,      // establish new Argv
    &SbiRuntime::StepARGV,      // TOS ==> current Argv
    &SbiRuntime::StepINPUT,     // Input ==> TOS
    &SbiRuntime::StepLINPUT,        // Line Input ==> TOS
    &SbiRuntime::StepGET,        // touch TOS
    &SbiRuntime::StepSET,        // save object TOS ==> TOS-1
    &SbiRuntime::StepPUT,       // TOS ==> TOS-1
    &SbiRuntime::StepPUTC,      // TOS ==> TOS-1, then ReadOnly
    &SbiRuntime::StepDIM,       // DIM
    &SbiRuntime::StepREDIM,         // REDIM
    &SbiRuntime::StepREDIMP,        // REDIM PRESERVE
    &SbiRuntime::StepERASE,         // delete TOS
    // branch
    &SbiRuntime::StepSTOP,          // program end
    &SbiRuntime::StepINITFOR,   // initialize FOR-Variable
    &SbiRuntime::StepNEXT,      // increment FOR-Variable
    &SbiRuntime::StepCASE,      // beginning CASE
    &SbiRuntime::StepENDCASE,   // end CASE
    &SbiRuntime::StepSTDERROR,      // standard error handling
    &SbiRuntime::StepNOERROR,   // no error handling
    &SbiRuntime::StepLEAVE,     // leave UP
    // E/A
    &SbiRuntime::StepCHANNEL,   // TOS = channel number
    &SbiRuntime::StepPRINT,     // print TOS
    &SbiRuntime::StepPRINTF,        // print TOS in field
    &SbiRuntime::StepWRITE,     // write TOS
    &SbiRuntime::StepRENAME,        // Rename Tos+1 to Tos
    &SbiRuntime::StepPROMPT,        // define Input Prompt from TOS
    &SbiRuntime::StepRESTART,   // Set restart point
    &SbiRuntime::StepCHANNEL0,  // set E/A-channel 0
    &SbiRuntime::StepEMPTY,     // empty expression on stack
    &SbiRuntime::StepERROR,     // TOS = error code
    &SbiRuntime::StepLSET,      // save object TOS ==> TOS-1
    &SbiRuntime::StepRSET,      // save object TOS ==> TOS-1
    &SbiRuntime::StepREDIMP_ERASE,// Copy array object for REDIMP
    &SbiRuntime::StepINITFOREACH,// Init for each loop
    &SbiRuntime::StepVBASET,// vba-like set statement
    &SbiRuntime::StepERASE_CLEAR,// vba-like set statement
    &SbiRuntime::StepARRAYACCESS,// access TOS as array
    &SbiRuntime::StepBYVAL,     // access TOS as array
};

const SbiRuntime::pStep1 SbiRuntime::aStep1[] = { // all opcodes with one operand
    &SbiRuntime::StepLOADNC,        // loading a numeric constant (+ID)
    &SbiRuntime::StepLOADSC,        // loading a string constant (+ID)
    &SbiRuntime::StepLOADI,     // Immediate Load (+value)
    &SbiRuntime::StepARGN,      // save a named Args in Argv (+StringID)
    &SbiRuntime::StepPAD,       // bring string to a definite length (+length)
    // branches
    &SbiRuntime::StepJUMP,      // jump (+Target)
    &SbiRuntime::StepJUMPT,     // evaluate TOS, conditional jump (+Target)
    &SbiRuntime::StepJUMPF,     // evaluate TOS, conditional jump (+Target)
    &SbiRuntime::StepONJUMP,        // evaluate TOS, jump into JUMP-table (+MaxVal)
    &SbiRuntime::StepGOSUB,     // UP-call (+Target)
    &SbiRuntime::StepRETURN,        // UP-return (+0 or Target)
    &SbiRuntime::StepTESTFOR,   // check FOR-variable, increment (+Endlabel)
    &SbiRuntime::StepCASETO,        // Tos+1 <= Case <= Tos), 2xremove (+Target)
    &SbiRuntime::StepERRHDL,        // error handler (+Offset)
    &SbiRuntime::StepRESUME,        // resume after errors (+0 or 1 or Label)
    // E/A
    &SbiRuntime::StepCLOSE,     // (+channel/0)
    &SbiRuntime::StepPRCHAR,        // (+char)
    // management
    &SbiRuntime::StepSETCLASS,  // check set + class names (+StringId)
    &SbiRuntime::StepTESTCLASS, // Check TOS class (+StringId)
    &SbiRuntime::StepLIB,       // lib for declare-call (+StringId)
    &SbiRuntime::StepBASED,     // TOS is incremented by BASE, BASE is pushed before
    &SbiRuntime::StepARGTYP,        // convert last parameter in Argv (+Type)
    &SbiRuntime::StepVBASETCLASS,// vba-like set statement
};

const SbiRuntime::pStep2 SbiRuntime::aStep2[] = {// all opcodes with two operands
    &SbiRuntime::StepRTL,       // load from RTL (+StringID+Typ)
    &SbiRuntime::StepFIND,      // load (+StringID+Typ)
    &SbiRuntime::StepELEM,          // load element (+StringID+Typ)
    &SbiRuntime::StepPARAM,     // Parameter (+Offset+Typ)
    // branches
    &SbiRuntime::StepCALL,      // Declare-Call (+StringID+Typ)
    &SbiRuntime::StepCALLC,     // CDecl-Declare-Call (+StringID+Typ)
    &SbiRuntime::StepCASEIS,        // Case-Test (+Test-Opcode+False-Target)
    // management
    &SbiRuntime::StepSTMNT,         // beginning of a statement (+Line+Col)
    // E/A
    &SbiRuntime::StepOPEN,          // (+StreamMode+Flags)
    // Objects
    &SbiRuntime::StepLOCAL,     // define local variable (+StringId+Typ)
    &SbiRuntime::StepPUBLIC,        // module global variable (+StringID+Typ)
    &SbiRuntime::StepGLOBAL,        // define global variable (+StringID+Typ)
    &SbiRuntime::StepCREATE,        // create object (+StringId+StringId)
    &SbiRuntime::StepSTATIC,     // static variable (+StringId+StringId)
    &SbiRuntime::StepTCREATE,    // user-defined objects (+StringId+StringId)
    &SbiRuntime::StepDCREATE,    // create object-array (+StringID+StringID)
    &SbiRuntime::StepGLOBAL_P,   // define global variable which is not overwritten
                                 // by the Basic on a restart (+StringID+Typ)
    &SbiRuntime::StepFIND_G,        // finds global variable with special treatment because of _GLOBAL_P
    &SbiRuntime::StepDCREATE_REDIMP, // redimension object array (+StringID+StringID)
    &SbiRuntime::StepFIND_CM,    // Search inside a class module (CM) to enable global search in time
    &SbiRuntime::StepPUBLIC_P,    // Search inside a class module (CM) to enable global search in time
    &SbiRuntime::StepFIND_STATIC,    // Search inside a class module (CM) to enable global search in time
};


//                              SbiRTLData

SbiRTLData::SbiRTLData()
    : nDirFlags(SbAttributes::NORMAL)
    , nCurDirPos(0)
{
}

SbiRTLData::~SbiRTLData()
{
}

//                              SbiInstance

// 16.10.96: #31460 new concept for StepInto/Over/Out
// The decision whether StepPoint shall be called is done with the help of
// the CallLevel. It's stopped when the current CallLevel is <= nBreakCallLvl.
// The current CallLevel can never be smaller than 1, as it's also incremented
// during the call of a method (also main). Therefore a BreakCallLvl from 0
// means that the program isn't stopped at all.
// (also have a look at: step2.cxx, SbiRuntime::StepSTMNT() )


void SbiInstance::CalcBreakCallLevel( BasicDebugFlags nFlags )
{

    nFlags &= ~BasicDebugFlags::Break;

    sal_uInt16 nRet;
    if (nFlags  == BasicDebugFlags::StepInto) {
        nRet = nCallLvl + 1;    // CallLevel+1 is also stopped
    } else if (nFlags == (BasicDebugFlags::StepOver | BasicDebugFlags::StepInto)) {
        nRet = nCallLvl;        // current CallLevel is stopped
    } else if (nFlags == BasicDebugFlags::StepOut) {
        nRet = nCallLvl - 1;    // smaller CallLevel is stopped
    } else {
        // Basic-IDE returns 0 instead of BasicDebugFlags::Continue, so also default=continue
        nRet = 0;               // CallLevel is always > 0 -> no StepPoint
    }
    nBreakCallLvl = nRet;           // take result
}

SbiInstance::SbiInstance( StarBASIC* p )
    : pIosys(new SbiIoSystem)
    , pDdeCtrl(new SbiDdeControl)
    , pBasic(p)
    , meFormatterLangType(LANGUAGE_DONTKNOW)
    , meFormatterDateOrder(DateOrder::YMD)
    , nStdDateIdx(0)
    , nStdTimeIdx(0)
    , nStdDateTimeIdx(0)
    , nErr(0)
    , nErl(0)
    , bReschedule(true)
    , bCompatibility(false)
    , pRun(nullptr)
    , nCallLvl(0)
    , nBreakCallLvl(0)
{
}

SbiInstance::~SbiInstance()
{
    while( pRun )
    {
        SbiRuntime* p = pRun->pNext;
        delete pRun;
        pRun = p;
    }

    try
    {
        int nSize = ComponentVector.size();
        if( nSize )
        {
            for( int i = nSize - 1 ; i >= 0 ; --i )
            {
                Reference< XComponent > xDlgComponent = ComponentVector[i];
                if( xDlgComponent.is() )
                    xDlgComponent->dispose();
            }
        }
    }
    catch( const Exception& )
    {
        TOOLS_WARN_EXCEPTION("basic", "SbiInstance::~SbiInstance: caught an exception while disposing the components" );
    }
}

SbiDllMgr* SbiInstance::GetDllMgr()
{
    if( !pDllMgr )
    {
        pDllMgr.reset(new SbiDllMgr);
    }
    return pDllMgr.get();
}

#endif

// #39629 create NumberFormatter with the help of a static method now
std::shared_ptr<SvNumberFormatter> const & SbiInstance::GetNumberFormatter()
{
    LanguageType eLangType = Application::GetSettings().GetLanguageTag().getLanguageType();
    SvtSysLocale aSysLocale;
    DateOrder eDate = aSysLocale.GetLocaleData().getDateOrder();
    if( pNumberFormatter )
    {
        if( eLangType != meFormatterLangType ||
            eDate != meFormatterDateOrder )
        {
            pNumberFormatter.reset();
        }
    }
    meFormatterLangType = eLangType;
    meFormatterDateOrder = eDate;
    if( !pNumberFormatter )
    {
        pNumberFormatter = PrepareNumberFormatter( nStdDateIdx, nStdTimeIdx, nStdDateTimeIdx,
                &meFormatterLangType, &meFormatterDateOrder);
    }
    return pNumberFormatter;
}

// #39629 offer NumberFormatter static too
std::shared_ptr<SvNumberFormatter> SbiInstance::PrepareNumberFormatter( sal_uInt32 &rnStdDateIdx,
    sal_uInt32 &rnStdTimeIdx, sal_uInt32 &rnStdDateTimeIdx,
    LanguageType const * peFormatterLangType, DateOrder const * peFormatterDateOrder )
{
    LanguageType eLangType;
    if( peFormatterLangType )
    {
        eLangType = *peFormatterLangType;
    }
    else
    {
        eLangType = Application::GetSettings().GetLanguageTag().getLanguageType();
    }
    DateOrder eDate;
    if( peFormatterDateOrder )
    {
        eDate = *peFormatterDateOrder;
    }
    else
    {
        SvtSysLocale aSysLocale;
        eDate = aSysLocale.GetLocaleData().getDateOrder();
    }

    std::shared_ptr<SvNumberFormatter> pNumberFormatter =
            std::make_shared<SvNumberFormatter>( comphelper::getProcessComponentContext(), eLangType );

    // Several parser methods pass SvNumberFormatter::IsNumberFormat() a number
    // format index to parse against. Tell the formatter the proper date
    // evaluation order, which also determines the date acceptance patterns to
    // use if a format was passed. NfEvalDateFormat::Format restricts to the
    // format's locale's date patterns/order (no init/system locale match
    // tried) and falls back to NfEvalDateFormat::International if no specific (i.e. 0)
    // (or an unknown) format index was passed.
    pNumberFormatter->SetEvalDateFormat( NfEvalDateFormat::Format);

    sal_Int32 nCheckPos = 0;
    SvNumFormatType nType;
    rnStdTimeIdx = pNumberFormatter->GetStandardFormat( SvNumFormatType::TIME, eLangType );

    // the formatter's standard templates have only got a two-digit date
    // -> registering an own format

    // HACK, because the numberformatter doesn't swap the place holders
    // for month, day and year according to the system setting.
    // Problem: Print Year(Date) under engl. BS
    // also have a look at: basic/source/sbx/sbxdate.cxx

    OUString aDateStr;
    switch( eDate )
    {
        default:
        case DateOrder::MDY: aDateStr = "MM/DD/YYYY"; break;
        case DateOrder::DMY: aDateStr = "DD/MM/YYYY"; break;
        case DateOrder::YMD: aDateStr = "YYYY/MM/DD"; break;
    }
    OUString aStr( aDateStr );      // PutandConvertEntry() modifies string!
    pNumberFormatter->PutandConvertEntry( aStr, nCheckPos, nType,
        rnStdDateIdx, LANGUAGE_ENGLISH_US, eLangType, true);
    nCheckPos = 0;
    aDateStr += " HH:MM:SS";
    aStr = aDateStr;
    pNumberFormatter->PutandConvertEntry( aStr, nCheckPos, nType,
        rnStdDateTimeIdx, LANGUAGE_ENGLISH_US, eLangType, true);
    return pNumberFormatter;
}

#if HAVE_FEATURE_SCRIPTING

// Let engine run. If Flags == BasicDebugFlags::Continue, take Flags over

void SbiInstance::Stop()
{
    for( SbiRuntime* p = pRun; p; p = p->pNext )
    {
        p->Stop();
    }
}

// Allows Basic IDE to set watch mode to suppress errors
static bool bWatchMode = false;

void setBasicWatchMode( bool bOn )
{
    bWatchMode = bOn;
}

void SbiInstance::Error( ErrCode n )
{
    Error( n, OUString() );
}

void SbiInstance::Error( ErrCode n, const OUString& rMsg )
{
    if( !bWatchMode )
    {
        aErrorMsg = rMsg;
        pRun->Error( n );
    }
}

void SbiInstance::ErrorVB( sal_Int32 nVBNumber, const OUString& rMsg )
{
    if( !bWatchMode )
    {
        ErrCode n = StarBASIC::GetSfxFromVBError( static_cast< sal_uInt16 >( nVBNumber ) );
        if ( !n )
        {
            n = ErrCode(nVBNumber); // force orig number, probably should have a specific table of vb ( localized ) errors
        }
        aErrorMsg = rMsg;
        SbiRuntime::translateErrorToVba( n, aErrorMsg );

        pRun->Error( ERRCODE_BASIC_COMPAT, true/*bVBATranslationAlreadyDone*/ );
    }
}

void SbiInstance::setErrorVB( sal_Int32 nVBNumber )
{
    ErrCode n = StarBASIC::GetSfxFromVBError( static_cast< sal_uInt16 >( nVBNumber ) );
    if( !n )
    {
        n = ErrCode(nVBNumber); // force orig number, probably should have a specific table of vb ( localized ) errors
    }
    aErrorMsg = OUString();
    SbiRuntime::translateErrorToVba( n, aErrorMsg );

    nErr = n;
}


void SbiInstance::FatalError( ErrCode n )
{
    pRun->FatalError( n );
}

void SbiInstance::FatalError( ErrCode _errCode, const OUString& _details )
{
    pRun->FatalError( _errCode, _details );
}

void SbiInstance::Abort()
{
    StarBASIC* pErrBasic = GetCurrentBasic( pBasic );
    pErrBasic->RTError( nErr, aErrorMsg, pRun->nLine, pRun->nCol1, pRun->nCol2 );
    StarBASIC::Stop();
}

// can be unequal to pRTBasic
StarBASIC* GetCurrentBasic( StarBASIC* pRTBasic )
{
    StarBASIC* pCurBasic = pRTBasic;
    SbModule* pActiveModule = StarBASIC::GetActiveModule();
    if( pActiveModule )
    {
        SbxObject* pParent = pActiveModule->GetParent();
        if (StarBASIC *pBasic = dynamic_cast<StarBASIC*>(pParent))
            pCurBasic = pBasic;
    }
    return pCurBasic;
}

SbModule* SbiInstance::GetActiveModule()
{
    if( pRun )
    {
        return pRun->GetModule();
    }
    else
    {
        return nullptr;
    }
}

SbMethod* SbiInstance::GetCaller( sal_uInt16 nLevel )
{
    SbiRuntime* p = pRun;
    while( nLevel-- && p )
    {
        p = p->pNext;
    }
    return p ? p->GetCaller() : nullptr;
}

//                              SbiInstance

// Attention: pMeth can also be NULL (on a call of the init-code)

SbiRuntime::SbiRuntime( SbModule* pm, SbMethod* pe, sal_uInt32 nStart )
         : rBasic( *static_cast<StarBASIC*>(pm->pParent) ), pInst( GetSbData()->pInst ),
           pMod( pm ), pMeth( pe ), pImg( pMod->pImage.get() )
{
    nFlags    = pe ? pe->GetDebugFlags() : BasicDebugFlags::NONE;
    pIosys    = pInst->GetIoSystem();
    pCode     =
    pStmnt    = pImg->GetCode() + nStart;
    refExprStk = new SbxArray;
    SetVBAEnabled( pMod->IsVBASupport() );
    SetParameters( pe ? pe->GetParameters() : nullptr );
}

SbiRuntime::~SbiRuntime()
{
    ClearArgvStack();
    ClearForStack();
}

void SbiRuntime::SetVBAEnabled(bool bEnabled )
{
    bVBAEnabled = bEnabled;
    if ( bVBAEnabled )
    {
        if ( pMeth )
        {
            mpExtCaller = pMeth->mCaller;
        }
    }
    else
    {
        mpExtCaller = nullptr;
    }
}

// tdf#79426, tdf#125180 - adds the information about a missing parameter
void SbiRuntime::SetIsMissing( SbxVariable* pVar )
{
    SbxInfo* pInfo = pVar->GetInfo() ? pVar->GetInfo() : new SbxInfo();
    pInfo->AddParam( pVar->GetName(), SbxMISSING, pVar->GetFlags() );
    pVar->SetInfo( pInfo );
}

// tdf#79426, tdf#125180 - checks if a variable contains the information about a missing parameter
bool SbiRuntime::IsMissing( SbxVariable* pVar, sal_uInt16 nIdx )
{
    return pVar->GetInfo() && pVar->GetInfo()->GetParam( nIdx ) && pVar->GetInfo()->GetParam( nIdx )->eType & SbxMISSING;
}

// Construction of the parameter list. All ByRef-parameters are directly
// taken over; copies of ByVal-parameters are created. If a particular
// data type is requested, it is converted.

void SbiRuntime::SetParameters( SbxArray* pParams )
{
    refParams = new SbxArray;
    // for the return value
    refParams->Put(pMeth, 0);

    SbxInfo* pInfo = pMeth ? pMeth->GetInfo() : nullptr;
    sal_uInt32 nParamCount = pParams ? pParams->Count() : 1;
    assert(nParamCount <= std::numeric_limits<sal_uInt16>::max());
    if( nParamCount > 1 )
    {
        for( sal_uInt32 i = 1 ; i < nParamCount ; i++ )
        {
            const SbxParamInfo* p = pInfo ? pInfo->GetParam( sal::static_int_cast<sal_uInt16>(i) ) : nullptr;

            // #111897 ParamArray
            if( p && (p->nUserData & PARAM_INFO_PARAMARRAY) != 0 )
            {
                SbxDimArray* pArray = new SbxDimArray( SbxVARIANT );
                sal_uInt32 nParamArrayParamCount = nParamCount - i;
                pArray->unoAddDim(0, nParamArrayParamCount - 1);
                for (sal_uInt32 j = i; j < nParamCount ; ++j)
                {
                    SbxVariable* v = pParams->Get(j);
                    sal_Int32 aDimIndex[1];
                    aDimIndex[0] = j - i;
                    pArray->Put(v, aDimIndex);
                }
                SbxVariable* pArrayVar = new SbxVariable( SbxVARIANT );
                pArrayVar->SetFlag( SbxFlagBits::ReadWrite );
                pArrayVar->PutObject( pArray );
                refParams->Put(pArrayVar, i);

                // Block ParamArray for missing parameter
                pInfo = nullptr;
                break;
            }

            SbxVariable* v = pParams->Get(i);
            assert(v);
            // methods are always byval!
            bool bByVal = dynamic_cast<const SbxMethod *>(v) != nullptr;
            SbxDataType t = v->GetType();
            bool bTargetTypeIsArray = false;
            if( p )
            {
                bByVal |= ( p->eType & SbxBYREF ) == 0;
                // tdf#79426, tdf#125180 - don't convert missing arguments to the requested parameter type
                if ( !IsMissing( v, 1 ) )
                {
                    t = static_cast<SbxDataType>( p->eType & 0x0FFF );
                }

                if( !bByVal && t != SbxVARIANT &&
                    (!v->IsFixed() || static_cast<SbxDataType>(v->GetType() & 0x0FFF ) != t) )
                {
                    bByVal = true;
                }

                bTargetTypeIsArray = (p->nUserData & PARAM_INFO_WITHBRACKETS) != 0;
            }
            if( bByVal )
            {
                // tdf#79426, tdf#125180 - don't convert missing arguments to the requested parameter type
                if( bTargetTypeIsArray && !IsMissing( v, 1 ) )
                {
                    t = SbxOBJECT;
                }
                SbxVariable* v2 = new SbxVariable( t );
                v2->SetFlag( SbxFlagBits::ReadWrite );
                // tdf#79426, tdf#125180 - if parameter was missing, readd additional information about a missing parameter
                if ( IsMissing( v, 1 ) )
                {
                    SetIsMissing( v2 );
                }
                *v2 = *v;
                refParams->Put(v2, i);
            }
            else
            {
                // tdf#79426, tdf#125180 - don't convert missing arguments to the requested parameter type
                if( t != SbxVARIANT && !IsMissing( v, 1 ) && t != ( v->GetType() & 0x0FFF ) )
                {
                    if( p && (p->eType & SbxARRAY) )
                    {
                        Error( ERRCODE_BASIC_CONVERSION );
                    }
                    else
                    {
                        v->Convert( t );
                    }
                }
                refParams->Put(v, i);
            }
            if( p )
            {
                refParams->PutAlias(p->aName, i);
            }
        }
    }

    // ParamArray for missing parameter
    if( !pInfo )
        return;

    // #111897 Check first missing parameter for ParamArray
    const SbxParamInfo* p = pInfo->GetParam(sal::static_int_cast<sal_uInt16>(nParamCount));
    if( p && (p->nUserData & PARAM_INFO_PARAMARRAY) != 0 )
    {
        SbxDimArray* pArray = new SbxDimArray( SbxVARIANT );
        pArray->unoAddDim(0, -1);
        SbxVariable* pArrayVar = new SbxVariable( SbxVARIANT );
        pArrayVar->SetFlag( SbxFlagBits::ReadWrite );
        pArrayVar->PutObject( pArray );
        refParams->Put(pArrayVar, nParamCount);
    }
}


// execute a P-Code

bool SbiRuntime::Step()
{
    if( bRun )
    {
        static sal_uInt32 nLastTime = osl_getGlobalTimer();

        // in any case check casually!
        if( !( ++nOps & 0xF ) && pInst->IsReschedule() )
        {
            sal_uInt32 nTime = osl_getGlobalTimer();
            if (nTime - nLastTime > 5) // 20 ms
            {
                nLastTime = nTime;
                Application::Reschedule();
            }
        }

        // #i48868 blocked by next call level?
        while( bBlocked )
        {
            if( pInst->IsReschedule() ) // And what if not? Busy loop?
            {
                Application::Reschedule();
                nLastTime = osl_getGlobalTimer();
            }
        }

        SbiOpcode eOp = static_cast<SbiOpcode>( *pCode++ );
        sal_uInt32 nOp1;
        if (eOp <= SbiOpcode::SbOP0_END)
        {
            (this->*( aStep0[ int(eOp) ] ) )();
        }
        else if (eOp >= SbiOpcode::SbOP1_START && eOp <= SbiOpcode::SbOP1_END)
        {
            nOp1 = *pCode++; nOp1 |= *pCode++ << 8; nOp1 |= *pCode++ << 16; nOp1 |= *pCode++ << 24;

            (this->*( aStep1[ int(eOp) - int(SbiOpcode::SbOP1_START) ] ) )( nOp1 );
        }
        else if (eOp >= SbiOpcode::SbOP2_START && eOp <= SbiOpcode::SbOP2_END)
        {
            nOp1 = *pCode++; nOp1 |= *pCode++ << 8; nOp1 |= *pCode++ << 16; nOp1 |= *pCode++ << 24;
            sal_uInt32 nOp2 = *pCode++; nOp2 |= *pCode++ << 8; nOp2 |= *pCode++ << 16; nOp2 |= *pCode++ << 24;
            (this->*( aStep2[ int(eOp) - int(SbiOpcode::SbOP2_START) ] ) )( nOp1, nOp2 );
        }
        else
        {
            StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        }

        ErrCode nErrCode = SbxBase::GetError();
        Error( nErrCode.IgnoreWarning() );

        // from 13.2.1997, new error handling:
        // ATTENTION: nError can be set already even if !nErrCode
        // since nError can now also be set from other RT-instances

        if( nError )
        {
            SbxBase::ResetError();
        }

        // from 15.3.96: display errors only if BASIC is still active
        // (especially not after compiler errors at the runtime)
        if( nError && bRun )
        {
            ErrCode err = nError;
            ClearExprStack();
            nError = ERRCODE_NONE;
            pInst->nErr = err;
            pInst->nErl = nLine;
            pErrCode    = pCode;
            pErrStmnt   = pStmnt;
            // An error occurred in an error handler
            // force parent handler ( if there is one )
            // to handle the error
            bool bLetParentHandleThis = false;

            // in the error handler? so std-error
            if ( !bInError )
            {
                bInError = true;

                if( !bError )           // On Error Resume Next
                {
                    StepRESUME( 1 );
                }
                else if( pError )       // On Error Goto ...
                {
                    pCode = pError;
                }
                else
                {
                    bLetParentHandleThis = true;
                }
            }
            else
            {
                bLetParentHandleThis = true;
                pError = nullptr; //terminate the handler
            }
            if ( bLetParentHandleThis )
            {
                // from 13.2.1997, new error handling:
                // consider superior error handlers

                // there's no error handler -> find one farther above
                SbiRuntime* pRtErrHdl = nullptr;
                SbiRuntime* pRt = this;
                while( (pRt = pRt->pNext) != nullptr )
                {
                    if( !pRt->bError || pRt->pError != nullptr )
                    {
                        pRtErrHdl = pRt;
                        break;
                    }
                }


                if( pRtErrHdl )
                {
                    // manipulate all the RTs that are below in the call-stack
                    pRt = this;
                    do
                    {
                        pRt->nError = err;
                        if( pRt != pRtErrHdl )
                        {
                            pRt->bRun = false;
                        }
                        else
                        {
                            break;
                        }
                        pRt = pRt->pNext;
                    }
                    while( pRt );
                }
                // no error-hdl found -> old behaviour
                else
                {
                    pInst->Abort();
                }
            }
        }
    }
    return bRun;
}

void SbiRuntime::Error( ErrCode n, bool bVBATranslationAlreadyDone )
{
    if( !n )
        return;

    nError = n;
    if( !isVBAEnabled() || bVBATranslationAlreadyDone )
        return;

    OUString aMsg = pInst->GetErrorMsg();
    sal_Int32 nVBAErrorNumber = translateErrorToVba( nError, aMsg );
    SbxVariable* pSbxErrObjVar = SbxErrObject::getErrObject().get();
    SbxErrObject* pGlobErr = static_cast< SbxErrObject* >( pSbxErrObjVar );
    if( pGlobErr != nullptr )
    {
        pGlobErr->setNumberAndDescription( nVBAErrorNumber, aMsg );
    }
    pInst->aErrorMsg = aMsg;
    nError = ERRCODE_BASIC_COMPAT;
}

void SbiRuntime::Error( ErrCode _errCode, const OUString& _details )
{
    if ( !_errCode )
        return;

    // Not correct for class module usage, remove for now
    //OSL_WARN_IF( pInst->pRun != this, "basic", "SbiRuntime::Error: can't propagate the error message details!" );
    if ( pInst->pRun == this )
    {
        pInst->Error( _errCode, _details );
        //OSL_WARN_IF( nError != _errCode, "basic", "SbiRuntime::Error: the instance is expected to propagate the error code back to me!" );
    }
    else
    {
        nError = _errCode;
    }
}

void SbiRuntime::FatalError( ErrCode n )
{
    StepSTDERROR();
    Error( n );
}

void SbiRuntime::FatalError( ErrCode _errCode, const OUString& _details )
{
    StepSTDERROR();
    Error( _errCode, _details );
}

sal_Int32 SbiRuntime::translateErrorToVba( ErrCode nError, OUString& rMsg )
{
    // If a message is defined use that ( in preference to
    // the defined one for the error ) NB #TODO
    // if there is an error defined it more than likely
    // is not the one you want ( some are the same though )
    // we really need a new vba compatible error list
    // tdf#123144 - always translate an error number to a vba error message
    StarBASIC::MakeErrorText( nError, rMsg );
    rMsg = StarBASIC::GetErrorText();
    // no num? most likely then it *is* really a vba err
    sal_uInt16 nVBErrorCode = StarBASIC::GetVBErrorCode( nError );
    sal_Int32 nVBAErrorNumber = ( nVBErrorCode == 0 ) ? sal_uInt32(nError) : nVBErrorCode;
    return nVBAErrorNumber;
}

//  Stacks

// The expression-stack is available for the continuous evaluation
// of expressions.

void SbiRuntime::PushVar( SbxVariable* pVar )
{
    if( pVar )
    {
        refExprStk->Put(pVar, nExprLvl++);
    }
}

SbxVariableRef SbiRuntime::PopVar()
{
#ifdef DBG_UTIL
    if( !nExprLvl )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        return new SbxVariable;
    }
#endif
    SbxVariableRef xVar = refExprStk->Get(--nExprLvl);
    SAL_INFO_IF( xVar->GetName() == "Cells", "basic", "PopVar: Name equals 'Cells'" );
    // methods hold themselves in parameter 0
    if( dynamic_cast<const SbxMethod *>(xVar.get()) != nullptr )
    {
        xVar->SetParameters(nullptr);
    }
    return xVar;
}

void SbiRuntime::ClearExprStack()
{
    // Attention: Clear() doesn't suffice as methods must be deleted
    while ( nExprLvl )
    {
        PopVar();
    }
    refExprStk->Clear();
}

// Take variable from the expression-stack without removing it
// n counts from 0

SbxVariable* SbiRuntime::GetTOS()
{
    short n = nExprLvl - 1;
#ifdef DBG_UTIL
    if( n < 0 )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        return new SbxVariable;
    }
#endif
    return refExprStk->Get(static_cast<sal_uInt32>(n));
}


void SbiRuntime::TOSMakeTemp()
{
    SbxVariable* p = refExprStk->Get(nExprLvl - 1);
    if ( p->GetType() == SbxEMPTY )
    {
        p->Broadcast( SfxHintId::BasicDataWanted );
    }

    SbxVariable* pDflt = nullptr;
    if ( bVBAEnabled &&  ( p->GetType() == SbxOBJECT || p->GetType() == SbxVARIANT  ) && ((pDflt = getDefaultProp(p)) != nullptr) )
    {
        pDflt->Broadcast( SfxHintId::BasicDataWanted );
        // replacing new p on stack causes object pointed by
        // pDft->pParent to be deleted, when p2->Compute() is
        // called below pParent is accessed (but it's deleted)
        // so set it to NULL now
        pDflt->SetParent( nullptr );
        p = new SbxVariable( *pDflt );
        p->SetFlag( SbxFlagBits::ReadWrite );
        refExprStk->Put(p, nExprLvl - 1);
    }
    else if( p->GetRefCount() != 1 )
    {
        SbxVariable* pNew = new SbxVariable( *p );
        pNew->SetFlag( SbxFlagBits::ReadWrite );
        refExprStk->Put(pNew, nExprLvl - 1);
    }
}

// the GOSUB-stack collects return-addresses for GOSUBs
void SbiRuntime::PushGosub( const sal_uInt8* pc )
{
    if( pGosubStk.size() >= MAXRECURSION )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_STACK_OVERFLOW );
    }
    pGosubStk.emplace_back(pc, nForLvl);
}

void SbiRuntime::PopGosub()
{
    if( pGosubStk.empty() )
    {
        Error( ERRCODE_BASIC_NO_GOSUB );
    }
    else
    {
        pCode = pGosubStk.back().pCode;
        pGosubStk.pop_back();
    }
}

// the Argv-stack collects current argument-vectors

void SbiRuntime::PushArgv()
{
    pArgvStk.emplace_back(refArgv, nArgc);
    nArgc = 1;
    refArgv.clear();
}

void SbiRuntime::PopArgv()
{
    if( !pArgvStk.empty() )
    {
        refArgv = pArgvStk.back().refArgv;
        nArgc = pArgvStk.back().nArgc;
        pArgvStk.pop_back();
    }
}


void SbiRuntime::ClearArgvStack()
{
    while( !pArgvStk.empty() )
    {
        PopArgv();
    }
}

// Push of the for-stack. The stack has increment, end, begin and variable.
// After the creation of the stack-element the stack's empty.

void SbiRuntime::PushFor()
{
    SbiForStack* p = new SbiForStack;
    p->eForType = ForType::To;
    p->pNext = pForStk;
    pForStk = p;

    p->refInc = PopVar();
    p->refEnd = PopVar();
    if (isVBAEnabled())
    {
        // tdf#150458: only calculate these once, coercing to double
        // tdf#150460: shouldn't we do it in non-VBA / compat mode, too?
        SbxVariableRef incCopy(new SbxVariable(SbxDOUBLE));
        *incCopy = *p->refInc;
        p->refInc = std::move(incCopy);
        SbxVariableRef endCopy(new SbxVariable(SbxDOUBLE));
        *endCopy = *p->refEnd;
        p->refEnd = std::move(endCopy);
    }
    SbxVariableRef xBgn = PopVar();
    p->refVar = PopVar();
    // tdf#85371 - grant explicitly write access to the index variable
    // since it could be the name of a method itself used in the next statement.
    ScopedWritableGuard aGuard(p->refVar, p->refVar.get() == pMeth);
    *(p->refVar) = *xBgn;
    nForLvl++;
}

void SbiRuntime::PushForEach()
{
    SbiForStack* p = new SbiForStack;
    // Set default value in case of error which is ignored in Resume Next
    p->eForType = ForType::EachArray;
    p->pNext = pForStk;
    pForStk = p;

    SbxVariableRef xObjVar = PopVar();
    SbxBase* pObj(nullptr);
    if (xObjVar)
    {
        SbxValues v(SbxVARIANT);
        // Here it may retrieve the value, and change the type from SbxEMPTY to SbxOBJECT
        xObjVar->Get(v);
        if (v.eType == SbxOBJECT)
            pObj = v.pObj;
    }

    if (SbxDimArray* pArray = dynamic_cast<SbxDimArray*>(pObj))
    {
        p->refEnd = reinterpret_cast<SbxVariable*>(pArray);

        sal_Int32 nDims = pArray->GetDims();
        p->pArrayLowerBounds.reset( new sal_Int32[nDims] );
        p->pArrayUpperBounds.reset( new sal_Int32[nDims] );
        p->pArrayCurIndices.reset( new sal_Int32[nDims] );
        sal_Int32 lBound, uBound;
        for( sal_Int32 i = 0 ; i < nDims ; i++ )
        {
            pArray->GetDim(i + 1, lBound, uBound);
            p->pArrayCurIndices[i] = p->pArrayLowerBounds[i] = lBound;
            p->pArrayUpperBounds[i] = uBound;
        }
    }
    else if (BasicCollection* pCollection = dynamic_cast<BasicCollection*>(pObj))
    {
        p->eForType = ForType::EachCollection;
        p->refEnd = pCollection;
        p->nCurCollectionIndex = 0;
    }
    else if (SbUnoObject* pUnoObj = dynamic_cast<SbUnoObject*>(pObj))
    {
        // XEnumerationAccess or XIndexAccess?
        Any aAny = pUnoObj->getUnoAny();
        Reference<XIndexAccess> xIndexAccess;
        Reference< XEnumerationAccess > xEnumerationAccess;
        if( aAny >>= xEnumerationAccess )
        {
            p->xEnumeration = xEnumerationAccess->createEnumeration();
            p->eForType = ForType::EachXEnumeration;
        }
        // tdf#130307 - support for each loop for objects exposing XIndexAccess
        else if (aAny >>= xIndexAccess)
        {
            p->eForType = ForType::EachXIndexAccess;
            p->xIndexAccess = std::move(xIndexAccess);
            p->nCurCollectionIndex = 0;
        }
        else if ( isVBAEnabled() && pUnoObj->isNativeCOMObject() )
        {
            uno::Reference< script::XInvocation > xInvocation;
            if ( ( aAny >>= xInvocation ) && xInvocation.is() )
            {
                try
                {
                    p->xEnumeration = new ComEnumerationWrapper( xInvocation );
                    p->eForType = ForType::EachXEnumeration;
                }
                catch(const uno::Exception& )
                {}
            }
        }
    }

    // Container variable
    p->refVar = PopVar();
    nForLvl++;
}


void SbiRuntime::PopFor()
{
    if( pForStk )
    {
        SbiForStack* p = pForStk;
        pForStk = p->pNext;
        delete p;
        nForLvl--;
    }
}


void SbiRuntime::ClearForStack()
{
    while( pForStk )
    {
        PopFor();
    }
}

SbiForStack* SbiRuntime::FindForStackItemForCollection( class BasicCollection const * pCollection )
{
    for (SbiForStack *p = pForStk; p; p = p->pNext)
    {
        SbxVariable* pVar = p->refEnd.is() ? p->refEnd.get() : nullptr;
        if( p->eForType == ForType::EachCollection
         && pVar != nullptr
         && dynamic_cast<BasicCollection*>( pVar) == pCollection  )
        {
            return p;
        }
    }

    return nullptr;
}


//  DLL-calls

void SbiRuntime::DllCall
    ( std::u16string_view aFuncName,
      std::u16string_view aDLLName,
      SbxArray* pArgs,          // parameter (from index 1, can be NULL)
      SbxDataType eResType,     // return value
      bool bCDecl )         // true: according to C-conventions
{
    SbxVariable* pRes = new SbxVariable( eResType );
    SbiDllMgr* pDllMgr = pInst->GetDllMgr();
    ErrCode nErr = pDllMgr->Call( aFuncName, aDLLName, pArgs, *pRes, bCDecl );
    if( nErr )
    {
        Error( nErr );
    }
    PushVar( pRes );
}

bool SbiRuntime::IsImageFlag( SbiImageFlags n ) const
{
    return pImg->IsFlag( n );
}

sal_uInt16 SbiRuntime::GetBase() const
{
    return pImg->GetBase();
}

void SbiRuntime::StepNOP()
{}

void SbiRuntime::StepArith( SbxOperator eOp )
{
    SbxVariableRef p1 = PopVar();
    TOSMakeTemp();
    SbxVariable* p2 = GetTOS();

    // tdf#144353 - do not compute any operation with a missing optional variable
    if ((p1->GetType() == SbxERROR && IsMissing(p1.get(), 1))
        || (p2->GetType() == SbxERROR && IsMissing(p2, 1)))
    {
        Error(ERRCODE_BASIC_NOT_OPTIONAL);
        return;
    }

    p2->ResetFlag( SbxFlagBits::Fixed );
    p2->Compute( eOp, *p1 );

    checkArithmeticOverflow( p2 );
}

void SbiRuntime::StepUnary( SbxOperator eOp )
{
    TOSMakeTemp();
    SbxVariable* p = GetTOS();
    // tdf#144353 - do not compute any operation with a missing optional variable
    if (p->GetType() == SbxERROR && IsMissing(p, 1))
    {
        Error(ERRCODE_BASIC_NOT_OPTIONAL);
        return;
    }
    p->Compute( eOp, *p );
}

void SbiRuntime::StepCompare( SbxOperator eOp )
{
    SbxVariableRef p1 = PopVar();
    SbxVariableRef p2 = PopVar();

    // tdf#144353 - do not compare a missing optional variable
    if ((p1->GetType() == SbxERROR && SbiRuntime::IsMissing(p1.get(), 1))
        || (p2->GetType() == SbxERROR && SbiRuntime::IsMissing(p2.get(), 1)))
    {
        SbxBase::SetError(ERRCODE_BASIC_NOT_OPTIONAL);
        return;
    }

    // Make sure objects with default params have
    // values ( and type ) set as appropriate
    SbxDataType p1Type = p1->GetType();
    SbxDataType p2Type = p2->GetType();
    if ( p1Type == SbxEMPTY )
    {
        p1->Broadcast( SfxHintId::BasicDataWanted );
        p1Type = p1->GetType();
    }
    if ( p2Type == SbxEMPTY )
    {
        p2->Broadcast( SfxHintId::BasicDataWanted );
        p2Type = p2->GetType();
    }
    if ( p1Type == p2Type )
    {
        // if both sides are an object and have default props
        // then we need to use the default props
        // we don't need to worry if only one side ( lhs, rhs ) is an
        // object ( object side will get coerced to correct type in
        // Compare )
        if ( p1Type ==  SbxOBJECT )
        {
            SbxVariable* pDflt = getDefaultProp( p1.get() );
            if ( pDflt )
            {
                p1 = pDflt;
                p1->Broadcast( SfxHintId::BasicDataWanted );
            }
            pDflt = getDefaultProp( p2.get() );
            if ( pDflt )
            {
                p2 = pDflt;
                p2->Broadcast( SfxHintId::BasicDataWanted );
            }
        }

    }
    static SbxVariable* pTRUE = nullptr;
    static SbxVariable* pFALSE = nullptr;
    // why do this on non-windows ?
    // why do this at all ?
    // I dumbly follow the pattern :-/
    if ( bVBAEnabled && ( p1->IsNull() || p2->IsNull() ) )
    {
        static SbxVariable* pNULL = []() {
            SbxVariable* p = new SbxVariable;
            p->PutNull();
            p->AddFirstRef();
            return p;
        }();
        PushVar( pNULL );
    }
    else if( p2->Compare( eOp, *p1 ) )
    {
        if( !pTRUE )
        {
            pTRUE = new SbxVariable;
            pTRUE->PutBool( true );
            pTRUE->AddFirstRef();
        }
        PushVar( pTRUE );
    }
    else
    {
        if( !pFALSE )
        {
            pFALSE = new SbxVariable;
            pFALSE->PutBool( false );
            pFALSE->AddFirstRef();
        }
        PushVar( pFALSE );
    }
}

void SbiRuntime::StepEXP()      { StepArith( SbxEXP );      }
void SbiRuntime::StepMUL()      { StepArith( SbxMUL );      }
void SbiRuntime::StepDIV()      { StepArith( SbxDIV );      }
void SbiRuntime::StepIDIV()     { StepArith( SbxIDIV );     }
void SbiRuntime::StepMOD()      { StepArith( SbxMOD );      }
void SbiRuntime::StepPLUS()     { StepArith( SbxPLUS );     }
void SbiRuntime::StepMINUS()        { StepArith( SbxMINUS );    }
void SbiRuntime::StepCAT()      { StepArith( SbxCAT );      }
void SbiRuntime::StepAND()      { StepArith( SbxAND );      }
void SbiRuntime::StepOR()       { StepArith( SbxOR );       }
void SbiRuntime::StepXOR()      { StepArith( SbxXOR );      }
void SbiRuntime::StepEQV()      { StepArith( SbxEQV );      }
void SbiRuntime::StepIMP()      { StepArith( SbxIMP );      }

void SbiRuntime::StepNEG()      { StepUnary( SbxNEG );      }
void SbiRuntime::StepNOT()      { StepUnary( SbxNOT );      }

void SbiRuntime::StepEQ()       { StepCompare( SbxEQ );     }
void SbiRuntime::StepNE()       { StepCompare( SbxNE );     }
void SbiRuntime::StepLT()       { StepCompare( SbxLT );     }
void SbiRuntime::StepGT()       { StepCompare( SbxGT );     }
void SbiRuntime::StepLE()       { StepCompare( SbxLE );     }
void SbiRuntime::StepGE()       { StepCompare( SbxGE );     }

namespace
{
    OUString VBALikeToRegexp(std::u16string_view sIn)
    {
        OUStringBuffer sResult("\\A"); // Match at the beginning of the input

        for (auto start = sIn.begin(), end = sIn.end(); start < end;)
        {
            switch (auto ch = *start++)
            {
            case '?':
                sResult.append('.');
                break;
            case '*':
                sResult.append(".*");
                break;
            case '#':
                sResult.append("[0-9]");
                break;
            case '[':
                sResult.append(ch);
                if (start < end)
                {
                    if (*start == '!')
                    {
                        sResult.append('^');
                        ++start;
                    }
                    else if (*start == '^')
                        sResult.append('\\');
                }
                for (bool seenright = false; start < end && !seenright; ++start)
                {
                    switch (*start)
                    {
                    case '[':
                    case '\\':
                        sResult.append('\\');
                        break;
                    case ']':
                        seenright = true;
                        break;
                    }
                    sResult.append(*start);
                }
                break;
            case '.':
            case '^':
            case '$':
            case '+':
            case '\\':
            case '|':
            case '{':
            case '}':
            case '(':
            case ')':
                sResult.append('\\');
                [[fallthrough]];
            default:
                sResult.append(ch);
            }
        }

        sResult.append("\\z"); // Match if the current position is at the end of input

        return sResult.makeStringAndClear();
    }
}

void SbiRuntime::StepLIKE()
{
    SbxVariableRef refVar1 = PopVar();
    SbxVariableRef refVar2 = PopVar();

    OUString value = refVar2->GetOUString();
    OUString regex = VBALikeToRegexp(refVar1->GetOUString());

    bool bTextMode(true);
    bool bCompatibility = ( GetSbData()->pInst && GetSbData()->pInst->IsCompatibility() );
    if( bCompatibility )
    {
        bTextMode = IsImageFlag( SbiImageFlags::COMPARETEXT );
    }
    sal_uInt32 searchFlags = UREGEX_UWORD | UREGEX_DOTALL; // Dot matches newline
    if( bTextMode )
    {
        searchFlags |= UREGEX_CASE_INSENSITIVE;
    }

    static sal_uInt32 cachedSearchFlags = 0;
    static OUString cachedRegex;
    static std::optional<icu::RegexMatcher> oRegexMatcher;
    UErrorCode nIcuErr = U_ZERO_ERROR;
    if (regex != cachedRegex || searchFlags != cachedSearchFlags || !oRegexMatcher)
    {
        cachedRegex = regex;
        cachedSearchFlags = searchFlags;
        icu::UnicodeString sRegex(false, reinterpret_cast<const UChar*>(cachedRegex.getStr()),
                                  cachedRegex.getLength());
        oRegexMatcher.emplace(sRegex, cachedSearchFlags, nIcuErr);
    }

    icu::UnicodeString sSource(false, reinterpret_cast<const UChar*>(value.getStr()),
                               value.getLength());
    oRegexMatcher->reset(sSource);

    bool bRes = oRegexMatcher->matches(nIcuErr);
    if (nIcuErr)
    {
        Error(ERRCODE_BASIC_INTERNAL_ERROR);
    }
    SbxVariable* pRes = new SbxVariable;
    pRes->PutBool( bRes );

    PushVar( pRes );
}

// TOS and TOS-1 are both object variables and contain the same pointer

void SbiRuntime::StepIS()
{
    SbxVariableRef refVar1 = PopVar();
    SbxVariableRef refVar2 = PopVar();

    SbxDataType eType1 = refVar1->GetType();
    SbxDataType eType2 = refVar2->GetType();
    if ( eType1 == SbxEMPTY )
    {
        refVar1->Broadcast( SfxHintId::BasicDataWanted );
        eType1 = refVar1->GetType();
    }
    if ( eType2 == SbxEMPTY )
    {
        refVar2->Broadcast( SfxHintId::BasicDataWanted );
        eType2 = refVar2->GetType();
    }

    bool bRes = ( eType1 == SbxOBJECT && eType2 == SbxOBJECT );
    if ( bVBAEnabled  && !bRes )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
    }
    bRes = ( bRes && refVar1->GetObject() == refVar2->GetObject() );
    SbxVariable* pRes = new SbxVariable;
    pRes->PutBool( bRes );
    PushVar( pRes );
}

// update the value of TOS

void SbiRuntime::StepGET()
{
    SbxVariable* p = GetTOS();
    p->Broadcast( SfxHintId::BasicDataWanted );
}

// #67607 copy Uno-Structs
static bool checkUnoStructCopy( bool bVBA, SbxVariableRef const & refVal, SbxVariableRef const & refVar )
{
    SbxDataType eVarType = refVar->GetType();
    SbxDataType eValType = refVal->GetType();

    // tdf#144353 - do not assign a missing optional variable to a property
    if (refVal->GetType() == SbxERROR && SbiRuntime::IsMissing(refVal.get(), 1))
    {
        SbxBase::SetError(ERRCODE_BASIC_NOT_OPTIONAL);
        return true;
    }

    if ( ( bVBA && ( eVarType == SbxEMPTY ) ) || !refVar->CanWrite() )
        return false;

    if ( eValType != SbxOBJECT )
        return false;
    // we seem to be duplicating parts of SbxValue=operator, maybe we should just move this to
    // there :-/ not sure if for every '=' we would want struct handling
    if( eVarType != SbxOBJECT )
    {
        if ( refVar->IsFixed() )
            return false;
    }
    // #115826: Exclude ProcedureProperties to avoid call to Property Get procedure
    else if( dynamic_cast<const SbProcedureProperty*>( refVar.get() ) != nullptr )
        return false;

    SbxObjectRef xValObj = static_cast<SbxObject*>(refVal->GetObject());
    if( !xValObj.is() || dynamic_cast<const SbUnoAnyObject*>( xValObj.get() ) != nullptr )
        return false;

    SbUnoObject* pUnoVal =  dynamic_cast<SbUnoObject*>( xValObj.get() );
    SbUnoStructRefObject* pUnoStructVal = dynamic_cast<SbUnoStructRefObject*>( xValObj.get() );
    Any aAny;
    // make doubly sure value is either a Uno object or
    // a uno struct
    if ( pUnoVal || pUnoStructVal )
        aAny = pUnoVal ? pUnoVal->getUnoAny() : pUnoStructVal->getUnoAny();
    else
        return false;
    if (  aAny.getValueTypeClass() != TypeClass_STRUCT )
        return false;

    refVar->SetType( SbxOBJECT );
    ErrCode eOldErr = SbxBase::GetError();
    // There are some circumstances when calling GetObject
    // will trigger an error, we need to squash those here.
    // Alternatively it is possible that the same scenario
    // could overwrite and existing error. Let's prevent that
    SbxObjectRef xVarObj = static_cast<SbxObject*>(refVar->GetObject());
    if ( eOldErr != ERRCODE_NONE )
        SbxBase::SetError( eOldErr );
    else
        SbxBase::ResetError();

    SbUnoStructRefObject* pUnoStructObj = dynamic_cast<SbUnoStructRefObject*>( xVarObj.get() );

    OUString sClassName = pUnoVal ? pUnoVal->GetClassName() : pUnoStructVal->GetClassName();
    OUString sName = pUnoVal ? pUnoVal->GetName() : pUnoStructVal->GetName();

    if ( pUnoStructObj )
    {
        StructRefInfo aInfo = pUnoStructObj->getStructInfo();
        aInfo.setValue( aAny );
    }
    else
    {
        SbUnoObject* pNewUnoObj = new SbUnoObject( sName, aAny );
        // #70324: adopt ClassName
        pNewUnoObj->SetClassName( sClassName );
        refVar->PutObject( pNewUnoObj );
    }
    return true;
}


// laying down TOS in TOS-1

void SbiRuntime::StepPUT()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    // store on its own method (inside a function)?
    bool bFlagsChanged = false;
    SbxFlagBits n = SbxFlagBits::NONE;
    if( refVar.get() == pMeth )
    {
        bFlagsChanged = true;
        n = refVar->GetFlags();
        refVar->SetFlag( SbxFlagBits::Write );
    }

    // if left side arg is an object or variant and right handside isn't
    // either an object or a variant then try and see if a default
    // property exists.
    // to use e.g. Range{"A1") = 34
    // could equate to Range("A1").Value = 34
    if ( bVBAEnabled )
    {
        // yet more hacking at this, I feel we don't quite have the correct
        // heuristics for dealing with obj1 = obj2 ( where obj2 ( and maybe
        // obj1 ) has default member/property ) ) It seems that default props
        // aren't dealt with if the object is a member of some parent object
        bool bObjAssign = false;
        if ( refVar->GetType() == SbxEMPTY )
            refVar->Broadcast( SfxHintId::BasicDataWanted );
        if ( refVar->GetType() == SbxOBJECT )
        {
            if  ( dynamic_cast<const SbxMethod *>(refVar.get()) != nullptr || ! refVar->GetParent() )
            {
                SbxVariable* pDflt = getDefaultProp( refVar.get() );

                if ( pDflt )
                    refVar = pDflt;
            }
            else
                bObjAssign = true;
        }
        if (  refVal->GetType() == SbxOBJECT  && !bObjAssign && ( dynamic_cast<const SbxMethod *>(refVal.get()) != nullptr || ! refVal->GetParent() ) )
        {
            SbxVariable* pDflt = getDefaultProp( refVal.get() );
            if ( pDflt )
                refVal = pDflt;
        }
    }

    if ( !checkUnoStructCopy( bVBAEnabled, refVal, refVar ) )
        *refVar = *refVal;

    if( bFlagsChanged )
        refVar->SetFlags( n );
}

namespace {

// VBA Dim As New behavior handling, save init object information
struct DimAsNewRecoverItem
{
    OUString        m_aObjClass;
    OUString        m_aObjName;
    SbxObject*      m_pObjParent;
    SbModule*       m_pClassModule;

    DimAsNewRecoverItem()
        : m_pObjParent( nullptr )
        , m_pClassModule( nullptr )
    {}

    DimAsNewRecoverItem( OUString aObjClass, OUString aObjName,
                         SbxObject* pObjParent, SbModule* pClassModule )
            : m_aObjClass(std::move( aObjClass ))
            , m_aObjName(std::move( aObjName ))
            , m_pObjParent( pObjParent )
            , m_pClassModule( pClassModule )
    {}

};


struct SbxVariablePtrHash
{
    size_t operator()( SbxVariable* pVar ) const
        { return reinterpret_cast<size_t>(pVar); }
};

}

typedef std::unordered_map< SbxVariable*, DimAsNewRecoverItem,
                              SbxVariablePtrHash >  DimAsNewRecoverHash;

namespace {

DimAsNewRecoverHash gaDimAsNewRecoverHash;

}

void removeDimAsNewRecoverItem( SbxVariable* pVar )
{
    DimAsNewRecoverHash::iterator it = gaDimAsNewRecoverHash.find( pVar );
    if( it != gaDimAsNewRecoverHash.end() )
    {
        gaDimAsNewRecoverHash.erase( it );
    }
}


// saving object variable
// not-object variables will cause errors

constexpr OUString pCollectionStr = u"Collection"_ustr;

void SbiRuntime::StepSET_Impl( SbxVariableRef& refVal, SbxVariableRef& refVar, bool bHandleDefaultProp )
{
    // #67733 types with array-flag are OK too

    // Check var, !object is no error for sure if, only if type is fixed
    SbxDataType eVarType = refVar->GetType();
    if( !bHandleDefaultProp && eVarType != SbxOBJECT && !(eVarType & SbxARRAY) && refVar->IsFixed() )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
        return;
    }

    // Check value, !object is no error for sure if, only if type is fixed
    SbxDataType eValType = refVal->GetType();
    if( !bHandleDefaultProp && eValType != SbxOBJECT && !(eValType & SbxARRAY) && refVal->IsFixed() )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
        return;
    }

    // Getting in here causes problems with objects with default properties
    // if they are SbxEMPTY I guess
    if ( !bHandleDefaultProp || eValType == SbxOBJECT )
    {
    // activate GetObject for collections on refVal
        SbxBase* pObjVarObj = refVal->GetObject();
        if( pObjVarObj )
        {
            SbxVariableRef refObjVal = dynamic_cast<SbxObject*>( pObjVarObj );

            if( refObjVal.is() )
            {
                refVal = std::move(refObjVal);
            }
            else if( !(eValType & SbxARRAY) )
            {
                refVal = nullptr;
            }
        }
    }

    // #52896 refVal can be invalid here, if uno-sequences - or more
    // general arrays - are assigned to variables that are declared
    // as an object!
    if( !refVal.is() )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
    }
    else
    {
        bool bFlagsChanged = false;
        SbxFlagBits n = SbxFlagBits::NONE;
        if( refVar.get() == pMeth )
        {
            bFlagsChanged = true;
            n = refVar->GetFlags();
            refVar->SetFlag( SbxFlagBits::Write );
        }
        SbProcedureProperty* pProcProperty = dynamic_cast<SbProcedureProperty*>( refVar.get() );
        if( pProcProperty )
        {
            pProcProperty->setSet( true );
        }
        if ( bHandleDefaultProp )
        {
            // get default properties for lhs & rhs where necessary
            // SbxVariable* defaultProp = NULL; unused variable
            // LHS try determine if a default prop exists
            // again like in StepPUT (see there too ) we are tweaking the
            // heuristics again for when to assign an object reference or
            // use default members if they exist
            // #FIXME we really need to get to the bottom of this mess
            bool bObjAssign = false;
            if ( refVar->GetType() == SbxOBJECT )
            {
                if ( dynamic_cast<const SbxMethod *>(refVar.get()) != nullptr || ! refVar->GetParent() )
                {
                    SbxVariable* pDflt = getDefaultProp( refVar.get() );
                    if ( pDflt )
                    {
                        refVar = pDflt;
                    }
                }
                else
                    bObjAssign = true;
            }
            // RHS only get a default prop is the rhs has one
            if (  refVal->GetType() == SbxOBJECT )
            {
                // check if lhs is a null object
                // if it is then use the object not the default property
                SbxObject* pObj = dynamic_cast<SbxObject*>( refVar.get() );

                // calling GetObject on a SbxEMPTY variable raises
                // object not set errors, make sure it's an Object
                if ( !pObj && refVar->GetType() == SbxOBJECT )
                {
                    SbxBase* pObjVarObj = refVar->GetObject();
                    pObj = dynamic_cast<SbxObject*>( pObjVarObj );
                }
                SbxVariable* pDflt = nullptr;
                if ( pObj && !bObjAssign )
                {
                    // lhs is either a valid object || or has a defaultProp
                    pDflt = getDefaultProp( refVal.get() );
                }
                if ( pDflt )
                {
                    refVal = pDflt;
                }
            }
        }

        // Handle Dim As New
        bool bDimAsNew = bVBAEnabled && refVar->IsSet( SbxFlagBits::DimAsNew );
        SbxBaseRef xPrevVarObj;
        if( bDimAsNew )
        {
            xPrevVarObj = refVar->GetObject();
        }
        // Handle withevents
        bool bWithEvents = refVar->IsSet( SbxFlagBits::WithEvents );
        if ( bWithEvents )
        {
            Reference< XInterface > xComListener;

            SbxBase* pObj = refVal->GetObject();
            SbUnoObject* pUnoObj = dynamic_cast<SbUnoObject*>( pObj );
            if( pUnoObj != nullptr )
            {
                Any aControlAny = pUnoObj->getUnoAny();
                OUString aDeclareClassName = refVar->GetDeclareClassName();
                OUString aPrefix = refVar->GetName();
                SbxObjectRef xScopeObj = refVar->GetParent();
                xComListener = createComListener( aControlAny, aDeclareClassName, aPrefix, xScopeObj );

                refVal->SetDeclareClassName( aDeclareClassName );
                refVal->SetComListener( xComListener, &rBasic );        // Hold reference
            }

        }

        // lhs is a property who's value is currently (Empty e.g. no broadcast yet)
        // in this case if there is a default prop involved the value of the
        // default property may in fact be void so the type will also be SbxEMPTY
        // in this case we do not want to call checkUnoStructCopy 'cause that will
        // cause an error also
        if ( !checkUnoStructCopy( bHandleDefaultProp, refVal, refVar ) )
        {
            *refVar = *refVal;
        }
        if ( bDimAsNew )
        {
            if( dynamic_cast<const SbxObject*>( refVar.get() ) == nullptr )
            {
                SbxBase* pValObjBase = refVal->GetObject();
                if( pValObjBase == nullptr )
                {
                    if( xPrevVarObj.is() )
                    {
                        // Object is overwritten with NULL, instantiate init object
                        DimAsNewRecoverHash::iterator it = gaDimAsNewRecoverHash.find( refVar.get() );
                        if( it != gaDimAsNewRecoverHash.end() )
                        {
                            const DimAsNewRecoverItem& rItem = it->second;
                            if( rItem.m_pClassModule != nullptr )
                            {
                                SbClassModuleObject* pNewObj = new SbClassModuleObject(*rItem.m_pClassModule);
                                pNewObj->SetName( rItem.m_aObjName );
                                pNewObj->SetParent( rItem.m_pObjParent );
                                refVar->PutObject( pNewObj );
                            }
                            else if( rItem.m_aObjClass.equalsIgnoreAsciiCase( pCollectionStr ) )
                            {
                                BasicCollection* pNewCollection = new BasicCollection( pCollectionStr );
                                pNewCollection->SetName( rItem.m_aObjName );
                                pNewCollection->SetParent( rItem.m_pObjParent );
                                refVar->PutObject( pNewCollection );
                            }
                        }
                    }
                }
                else
                {
                    // Does old value exist?
                    bool bFirstInit = !xPrevVarObj.is();
                    if( bFirstInit )
                    {
                        // Store information to instantiate object later
                        SbxObject* pValObj = dynamic_cast<SbxObject*>( pValObjBase );
                        if( pValObj != nullptr )
                        {
                            OUString aObjClass = pValObj->GetClassName();

                            SbClassModuleObject* pClassModuleObj = dynamic_cast<SbClassModuleObject*>( pValObjBase );
                            if( pClassModuleObj != nullptr )
                            {
                                SbModule& rClassModule = pClassModuleObj->getClassModule();
                                gaDimAsNewRecoverHash[refVar.get()] =
                                    DimAsNewRecoverItem( aObjClass, pValObj->GetName(), pValObj->GetParent(), &rClassModule );
                            }
                            else if( aObjClass.equalsIgnoreAsciiCase( "Collection" ) )
                            {
                                gaDimAsNewRecoverHash[refVar.get()] =
                                    DimAsNewRecoverItem( aObjClass, pValObj->GetName(), pValObj->GetParent(), nullptr );
                            }
                        }
                    }
                }
            }
        }

        if( bFlagsChanged )
        {
            refVar->SetFlags( n );
        }
    }
}

void SbiRuntime::StepSET()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    StepSET_Impl( refVal, refVar, bVBAEnabled ); // this is really assignment
}

void SbiRuntime::StepVBASET()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    // don't handle default property
    StepSET_Impl( refVal, refVar ); // set obj = something
}


void SbiRuntime::StepLSET()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    if( refVar->GetType() != SbxSTRING ||
        refVal->GetType() != SbxSTRING )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
    }
    else
    {
        SbxFlagBits n = refVar->GetFlags();
        if( refVar.get() == pMeth )
        {
            refVar->SetFlag( SbxFlagBits::Write );
        }
        OUString aRefVarString = refVar->GetOUString();
        OUString aRefValString = refVal->GetOUString();

        sal_Int32 nVarStrLen = aRefVarString.getLength();
        sal_Int32 nValStrLen = aRefValString.getLength();
        OUString aNewStr;
        if( nVarStrLen > nValStrLen )
        {
            OUStringBuffer buf(aRefValString);
            comphelper::string::padToLength(buf, nVarStrLen, ' ');
            aNewStr = buf.makeStringAndClear();
        }
        else
        {
            aNewStr = aRefValString.copy( 0, nVarStrLen );
        }

        refVar->PutString(aNewStr);
        refVar->SetFlags( n );
    }
}

void SbiRuntime::StepRSET()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    if( refVar->GetType() != SbxSTRING || refVal->GetType() != SbxSTRING )
    {
        Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
    }
    else
    {
        SbxFlagBits n = refVar->GetFlags();
        if( refVar.get() == pMeth )
        {
            refVar->SetFlag( SbxFlagBits::Write );
        }
        OUString aRefVarString = refVar->GetOUString();
        OUString aRefValString = refVal->GetOUString();
        sal_Int32 nVarStrLen = aRefVarString.getLength();
        sal_Int32 nValStrLen = aRefValString.getLength();

        OUStringBuffer aNewStr(nVarStrLen);
        if (nVarStrLen > nValStrLen)
        {
            comphelper::string::padToLength(aNewStr, nVarStrLen - nValStrLen, ' ');
            aNewStr.append(aRefValString);
        }
        else
        {
            aNewStr.append(aRefValString.subView(0, nVarStrLen));
        }
        refVar->PutString(aNewStr.makeStringAndClear());

        refVar->SetFlags( n );
    }
}

// laying down TOS in TOS-1, then set ReadOnly-Bit

void SbiRuntime::StepPUTC()
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    refVar->SetFlag( SbxFlagBits::Write );
    *refVar = *refVal;
    refVar->ResetFlag( SbxFlagBits::Write );
    refVar->SetFlag( SbxFlagBits::Const );
}

// DIM
// TOS = variable for the array with dimension information as parameter

void SbiRuntime::StepDIM()
{
    SbxVariableRef refVar = PopVar();
    DimImpl( refVar );
}

// #56204 swap out DIM-functionality into a help method (step0.cxx)
void SbiRuntime::DimImpl(const SbxVariableRef& refVar)
{
    // If refDim then this DIM statement is terminating a ReDIM and
    // previous StepERASE_CLEAR for an array, the following actions have
    // been delayed from ( StepERASE_CLEAR ) 'till here
    if ( refRedim.is() )
    {
        if ( !refRedimpArray.is() ) // only erase the array not ReDim Preserve
        {
            lcl_eraseImpl( refVar, bVBAEnabled );
        }
        SbxDataType eType = refVar->GetType();
        lcl_clearImpl( refVar, eType );
        refRedim = nullptr;
    }
    SbxArray* pDims = refVar->GetParameters();
    // must have an even number of arguments
    // have in mind that Arg[0] does not count!
    if (pDims && !(pDims->Count() & 1))
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    }
    else
    {
        SbxDataType eType = refVar->IsFixed() ? refVar->GetType() : SbxVARIANT;
        SbxDimArray* pArray = new SbxDimArray( eType );
        // allow arrays without dimension information, too (VB-compatible)
        if( pDims )
        {
            refVar->ResetFlag( SbxFlagBits::VarToDim );

            for (sal_uInt32 i = 1; i < pDims->Count();)
            {
                sal_Int32 lb = pDims->Get(i++)->GetLong();
                sal_Int32 ub = pDims->Get(i++)->GetLong();
                if( ub < lb )
                {
                    Error( ERRCODE_BASIC_OUT_OF_RANGE );
                    ub = lb;
                }
                pArray->AddDim(lb, ub);
                if ( lb != ub )
                {
                    pArray->setHasFixedSize( true );
                }
            }
        }
        else
        {
            // #62867 On creating an array of the length 0, create
            // a dimension (like for Uno-Sequences of the length 0)
            pArray->unoAddDim(0, -1);
        }
        SbxFlagBits nSavFlags = refVar->GetFlags();
        refVar->ResetFlag( SbxFlagBits::Fixed );
        refVar->PutObject( pArray );
        refVar->SetFlags( nSavFlags );
        refVar->SetParameters( nullptr );
    }
}

// REDIM
// TOS  = variable for the array
// argv = dimension information

void SbiRuntime::StepREDIM()
{
    // Nothing different than dim at the moment because
    // a double dim is already recognized by the compiler.
    StepDIM();
}


// Helper function for StepREDIMP and StepDCREATE_IMPL / bRedimp = true
static void implCopyDimArray( SbxDimArray* pNewArray, SbxDimArray* pOldArray, sal_Int32 nMaxDimIndex,
    sal_Int32 nActualDim, sal_Int32* pActualIndices, sal_Int32* pLowerBounds, sal_Int32* pUpperBounds )
{
    sal_Int32& ri = pActualIndices[nActualDim];
    for( ri = pLowerBounds[nActualDim] ; ri <= pUpperBounds[nActualDim] ; ri++ )
    {
        if( nActualDim < nMaxDimIndex )
        {
            implCopyDimArray( pNewArray, pOldArray, nMaxDimIndex, nActualDim + 1,
                pActualIndices, pLowerBounds, pUpperBounds );
        }
        else
        {
            SbxVariable* pSource = pOldArray->Get(pActualIndices);
            if (pSource && pOldArray->GetRefCount() > 1)
                // tdf#134692: old array will stay alive after the redim - we need to copy deep
                pSource = new SbxVariable(*pSource);
            pNewArray->Put(pSource, pActualIndices);
        }
    }
}

// Returns true when actually restored
static bool implRestorePreservedArray(SbxDimArray* pNewArray, SbxArrayRef& rrefRedimpArray, bool* pbWasError = nullptr)
{
    assert(pNewArray);
    bool bResult = false;
    if (pbWasError)
        *pbWasError = false;
    if (rrefRedimpArray)
    {
        SbxDimArray* pOldArray = static_cast<SbxDimArray*>(rrefRedimpArray.get());
        const sal_Int32 nDimsNew = pNewArray->GetDims();
        const sal_Int32 nDimsOld = pOldArray->GetDims();

        if (nDimsOld != nDimsNew)
        {
            StarBASIC::Error(ERRCODE_BASIC_OUT_OF_RANGE);
            if (pbWasError)
                *pbWasError = true;
        }
        else if (nDimsNew > 0)
        {
            // Store dims to use them for copying later
            std::unique_ptr<sal_Int32[]> pLowerBounds(new sal_Int32[nDimsNew]);
            std::unique_ptr<sal_Int32[]> pUpperBounds(new sal_Int32[nDimsNew]);
            std::unique_ptr<sal_Int32[]> pActualIndices(new sal_Int32[nDimsNew]);
            bool bNeedsPreallocation = true;

            // Compare bounds
            for (sal_Int32 i = 1; i <= nDimsNew; i++)
            {
                sal_Int32 lBoundNew, uBoundNew;
                sal_Int32 lBoundOld, uBoundOld;
                pNewArray->GetDim(i, lBoundNew, uBoundNew);
                pOldArray->GetDim(i, lBoundOld, uBoundOld);
                lBoundNew = std::max(lBoundNew, lBoundOld);
                uBoundNew = std::min(uBoundNew, uBoundOld);
                sal_Int32 j = i - 1;
                pActualIndices[j] = pLowerBounds[j] = lBoundNew;
                pUpperBounds[j] = uBoundNew;
                if (lBoundNew > uBoundNew) // No elements in the dimension -> no elements to restore
                    bNeedsPreallocation = false;
            }

            // Optimization: pre-allocate underlying container
            if (bNeedsPreallocation)
                pNewArray->Put(nullptr, pUpperBounds.get());

            // Copy data from old array by going recursively through all dimensions
            // (It would be faster to work on the flat internal data array of an
            // SbyArray but this solution is clearer and easier)
            implCopyDimArray(pNewArray, pOldArray, nDimsNew - 1, 0, pActualIndices.get(),
                             pLowerBounds.get(), pUpperBounds.get());
            bResult = true;
        }

        rrefRedimpArray.clear();
    }
    return bResult;
}

// REDIM PRESERVE
// TOS  = variable for the array
// argv = dimension information

void SbiRuntime::StepREDIMP()
{
    SbxVariableRef refVar = PopVar();
    DimImpl( refVar );

    // Now check, if we can copy from the old array
    if( refRedimpArray.is() )
    {
        if (SbxDimArray* pNewArray = dynamic_cast<SbxDimArray*>(refVar->GetObject()))
            implRestorePreservedArray(pNewArray, refRedimpArray);
    }
}

// REDIM_COPY
// TOS  = Array-Variable, Reference to array is copied
//        Variable is cleared as in ERASE

void SbiRuntime::StepREDIMP_ERASE()
{
    SbxVariableRef refVar = PopVar();
    refRedim = refVar;
    SbxDataType eType = refVar->GetType();
    if( eType & SbxARRAY )
    {
        SbxBase* pElemObj = refVar->GetObject();
        SbxDimArray* pDimArray = dynamic_cast<SbxDimArray*>( pElemObj );
        if( pDimArray )
        {
            refRedimpArray = pDimArray;
        }

    }
    else if( refVar->IsFixed() )
    {
        refVar->Clear();
    }
    else
    {
        refVar->SetType( SbxEMPTY );
    }
}

static void lcl_clearImpl( SbxVariableRef const & refVar, SbxDataType const & eType )
{
    SbxFlagBits nSavFlags = refVar->GetFlags();
    refVar->ResetFlag( SbxFlagBits::Fixed );
    refVar->SetType( SbxDataType(eType & 0x0FFF) );
    refVar->SetFlags( nSavFlags );
    refVar->Clear();
}

static void lcl_eraseImpl( SbxVariableRef const & refVar, bool bVBAEnabled )
{
    SbxDataType eType = refVar->GetType();
    if( eType & SbxARRAY )
    {
        if ( bVBAEnabled )
        {
            SbxBase* pElemObj = refVar->GetObject();
            SbxDimArray* pDimArray = dynamic_cast<SbxDimArray*>( pElemObj );
            if( pDimArray )
            {
                if ( pDimArray->hasFixedSize() )
                {
                    // Clear all Value(s)
                    pDimArray->SbxArray::Clear();
                }
                else
                {
                    pDimArray->Clear(); // clear dims and values
                }
            }
            else
            {
                SbxArray* pArray = dynamic_cast<SbxArray*>( pElemObj );
                if ( pArray )
                {
                    pArray->Clear();
                }
            }
        }
        else
        {
            // Arrays have on an erase to VB quite a complex behaviour. Here are
            // only the type problems at REDIM (#26295) removed at first:
            // Set type hard onto the array-type, because a variable with array is
            // SbxOBJECT. At REDIM there's an SbxOBJECT-array generated then and
            // the original type is lost -> runtime error
            lcl_clearImpl( refVar, eType );
        }
    }
    else if( refVar->IsFixed() )
    {
        refVar->Clear();
    }
    else
    {
        refVar->SetType( SbxEMPTY );
    }
}

// delete variable
// TOS = variable

void SbiRuntime::StepERASE()
{
    SbxVariableRef refVar = PopVar();
    lcl_eraseImpl( refVar, bVBAEnabled );
}

void SbiRuntime::StepERASE_CLEAR()
{
    refRedim = PopVar();
}

void SbiRuntime::StepARRAYACCESS()
{
    if( !refArgv.is() )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    }
    SbxVariableRef refVar = PopVar();
    refVar->SetParameters( refArgv.get() );
    PopArgv();
    PushVar( CheckArray( refVar.get() ) );
}

void SbiRuntime::StepBYVAL()
{
    // Copy variable on stack to break call by reference
    SbxVariableRef pVar = PopVar();
    SbxDataType t = pVar->GetType();

    SbxVariable* pCopyVar = new SbxVariable( t );
    pCopyVar->SetFlag( SbxFlagBits::ReadWrite );
    *pCopyVar = *pVar;

    PushVar( pCopyVar );
}

// establishing an argv
// nOp1 stays as it is -> 1st element is the return value

void SbiRuntime::StepARGC()
{
    PushArgv();
    refArgv = new SbxArray;
    nArgc = 1;
}

// storing an argument in Argv

void SbiRuntime::StepARGV()
{
    if( !refArgv.is() )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    }
    else
    {
        SbxVariableRef pVal = PopVar();

        // Before fix of #94916:
        if( dynamic_cast<const SbxMethod*>( pVal.get() ) != nullptr
            || dynamic_cast<const SbUnoProperty*>( pVal.get() ) != nullptr
            || dynamic_cast<const SbProcedureProperty*>( pVal.get() ) != nullptr )
        {
            // evaluate methods and properties!
            SbxVariable* pRes = new SbxVariable( *pVal );
            pVal = pRes;
        }
        refArgv->Put(pVal.get(), nArgc++);
    }
}

// Input to Variable. The variable is on TOS and is
// is removed afterwards.
void SbiRuntime::StepINPUT()
{
    OUStringBuffer sin;
    char ch = 0;
    ErrCode err;
    // Skip whitespace
    while( ( err = pIosys->GetError() ) == ERRCODE_NONE )
    {
        ch = pIosys->Read();
        if( ch != ' ' && ch != '\t' && ch != '\n' )
        {
            break;
        }
    }
    if( !err )
    {
        // Scan until comma or whitespace
        char sep = ( ch == '"' ) ? ch : 0;
        if( sep )
        {
            ch = pIosys->Read();
        }
        while( ( err = pIosys->GetError() ) == ERRCODE_NONE )
        {
            if( ch == sep )
            {
                ch = pIosys->Read();
                if( ch != sep )
                {
                    break;
                }
            }
            else if( !sep && (ch == ',' || ch == '\n') )
            {
                break;
            }
            sin.append( ch );
            ch = pIosys->Read();
        }
        // skip whitespace
        if( ch == ' ' || ch == '\t' )
        {
            while( ( err = pIosys->GetError() ) == ERRCODE_NONE )
            {
                if( ch != ' ' && ch != '\t' && ch != '\n' )
                {
                    break;
                }
                ch = pIosys->Read();
            }
        }
    }
    if( !err )
    {
        OUString s = sin.makeStringAndClear();
        SbxVariableRef pVar = GetTOS();
        // try to fill the variable with a numeric value first,
        // then with a string value
        if( !pVar->IsFixed() || pVar->IsNumeric() )
        {
            sal_Int32 nLen = 0;
            if( !pVar->Scan( s, &nLen ) )
            {
                err = SbxBase::GetError();
                SbxBase::ResetError();
            }
            // the value has to be scanned in completely
            else if( nLen != s.getLength() && !pVar->PutString( s ) )
            {
                err = SbxBase::GetError();
                SbxBase::ResetError();
            }
            else if( nLen != s.getLength() && pVar->IsNumeric() )
            {
                err = SbxBase::GetError();
                SbxBase::ResetError();
                if( !err )
                {
                    err = ERRCODE_BASIC_CONVERSION;
                }
            }
        }
        else
        {
            pVar->PutString( s );
            err = SbxBase::GetError();
            SbxBase::ResetError();
        }
    }
    if( err == ERRCODE_BASIC_USER_ABORT )
    {
        Error( err );
    }
    else if( err )
    {
        if( pRestart && !pIosys->GetChannel() )
        {
            pCode = pRestart;
        }
        else
        {
            Error( err );
        }
    }
    else
    {
        PopVar();
    }
}

// Line Input to Variable. The variable is on TOS and is
// deleted afterwards.

void SbiRuntime::StepLINPUT()
{
    OString aInput;
    pIosys->Read( aInput );
    Error( pIosys->GetError() );
    SbxVariableRef p = PopVar();
    p->PutString(OStringToOUString(aInput, osl_getThreadTextEncoding()));
}

// end of program

void SbiRuntime::StepSTOP()
{
    pInst->Stop();
}


void SbiRuntime::StepINITFOR()
{
    PushFor();
}

void SbiRuntime::StepINITFOREACH()
{
    PushForEach();
}

// increment FOR-variable

void SbiRuntime::StepNEXT()
{
    if( !pForStk )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        return;
    }
    if (pForStk->eForType != ForType::To)
        return;
    if (!pForStk->refVar)
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        return;
    }
    // tdf#85371 - grant explicitly write access to the index variable
    // since it could be the name of a method itself used in the next statement.
    ScopedWritableGuard aGuard(pForStk->refVar, pForStk->refVar.get() == pMeth);
    pForStk->refVar->Compute( SbxPLUS, *pForStk->refInc );
}

// beginning CASE: TOS in CASE-stack

void SbiRuntime::StepCASE()
{
    if( !refCaseStk.is() )
    {
        refCaseStk = new SbxArray;
    }
    SbxVariableRef xVar = PopVar();
    refCaseStk->Put(xVar.get(), refCaseStk->Count());
}

// end CASE: free variable

void SbiRuntime::StepENDCASE()
{
    if (!refCaseStk.is() || !refCaseStk->Count())
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    }
    else
    {
        refCaseStk->Remove(refCaseStk->Count() - 1);
    }
}


void SbiRuntime::StepSTDERROR()
{
    pError = nullptr; bError = true;
    pInst->aErrorMsg.clear();
    pInst->nErr = ERRCODE_NONE;
    pInst->nErl = 0;
    nError = ERRCODE_NONE;
    SbxErrObject::getUnoErrObject()->Clear();
}

void SbiRuntime::StepNOERROR()
{
    pInst->aErrorMsg.clear();
    pInst->nErr = ERRCODE_NONE;
    pInst->nErl = 0;
    nError = ERRCODE_NONE;
    SbxErrObject::getUnoErrObject()->Clear();
    bError = false;
}

// leave UP

void SbiRuntime::StepLEAVE()
{
    bRun = false;
        // If VBA and we are leaving an ErrorHandler then clear the error ( it's been processed )
    if ( bInError && pError )
    {
        SbxErrObject::getUnoErrObject()->Clear();
    }
}

void SbiRuntime::StepCHANNEL()      // TOS = channel number
{
    SbxVariableRef pChan = PopVar();
    short nChan = pChan->GetInteger();
    pIosys->SetChannel( nChan );
    Error( pIosys->GetError() );
}

void SbiRuntime::StepCHANNEL0()
{
    pIosys->ResetChannel();
}

void SbiRuntime::StepPRINT()        // print TOS
{
    SbxVariableRef p = PopVar();
    OUString s1 = p->GetOUString();
    OUString s;
    if( p->GetType() >= SbxINTEGER && p->GetType() <= SbxDOUBLE )
    {
        s = " ";    // one blank before
    }
    s += s1;
    pIosys->Write( s );
    Error( pIosys->GetError() );
}

void SbiRuntime::StepPRINTF()       // print TOS in field
{
    SbxVariableRef p = PopVar();
    OUString s1 = p->GetOUString();
    OUStringBuffer s;
    if( p->GetType() >= SbxINTEGER && p->GetType() <= SbxDOUBLE )
    {
        s.append(' ');
    }
    s.append(s1);
    comphelper::string::padToLength(s, 14, ' ');
    pIosys->Write( s );
    Error( pIosys->GetError() );
}

void SbiRuntime::StepWRITE()        // write TOS
{
    SbxVariableRef p = PopVar();
    // Does the string have to be encapsulated?
    char ch = 0;
    switch (p->GetType() )
    {
    case SbxSTRING: ch = '"'; break;
    case SbxCURRENCY:
    case SbxBOOL:
    case SbxDATE: ch = '#'; break;
    default: break;
    }
    OUString s;
    if( ch )
    {
        s += OUStringChar(ch);
    }
    s += p->GetOUString();
    if( ch )
    {
        s += OUStringChar(ch);
    }
    pIosys->Write( s );
    Error( pIosys->GetError() );
}

void SbiRuntime::StepRENAME()       // Rename Tos+1 to Tos
{
    SbxVariableRef pTos1 = PopVar();
    SbxVariableRef pTos  = PopVar();
    OUString aDest = pTos1->GetOUString();
    OUString aSource = pTos->GetOUString();

    if( hasUno() )
    {
        implStepRenameUCB( aSource, aDest );
    }
    else
    {
        implStepRenameOSL( aSource, aDest );
    }
}

// TOS = Prompt

void SbiRuntime::StepPROMPT()
{
    SbxVariableRef p = PopVar();
    pIosys->SetPrompt(p->GetOUString());
}

// Set Restart point

void SbiRuntime::StepRESTART()
{
    pRestart = pCode;
}

// empty expression on stack for missing parameter

void SbiRuntime::StepEMPTY()
{
    // #57915 The semantics of StepEMPTY() is the representation of a missing argument.
    // This is represented by the value 448 (ERRCODE_BASIC_NAMED_NOT_FOUND) of the type error
    // in VB. StepEmpty should now rather be named StepMISSING() but the name is kept
    // to simplify matters.
    SbxVariableRef xVar = new SbxVariable( SbxVARIANT );
    xVar->PutErr( 448 );
    // tdf#79426, tdf#125180 - add additional information about a missing parameter
    SetIsMissing( xVar.get() );
    PushVar( xVar.get() );
}

// TOS = error code

void SbiRuntime::StepERROR()
{
    SbxVariableRef refCode = PopVar();
    sal_uInt16 n = refCode->GetUShort();
    ErrCode error = StarBASIC::GetSfxFromVBError( n );
    if ( bVBAEnabled )
    {
        pInst->Error( error );
    }
    else
    {
        Error( error );
    }
}

// loading a numeric constant (+ID)

void SbiRuntime::StepLOADNC( sal_uInt32 nOp1 )
{
    // tdf#143707 - check if the data type character was added after the string termination symbol
    SbxDataType eTypeStr;
    // #57844 use localized function
    OUString aStr = pImg->GetString(nOp1, &eTypeStr);
    // also allow , !!!
    sal_Int32 iComma = aStr.indexOf(',');
    if( iComma >= 0 )
    {
        aStr = aStr.replaceAt(iComma, 1, u".");
    }
    sal_Int32 nParseEnd = 0;
    rtl_math_ConversionStatus eStatus = rtl_math_ConversionStatus_Ok;
    double n = ::rtl::math::stringToDouble( aStr, '.', ',', &eStatus, &nParseEnd );

    // tdf#131296 - retrieve data type put in SbiExprNode::Gen
    SbxDataType eType = SbxDOUBLE;
    if ( nParseEnd < aStr.getLength() )
    {
        // tdf#143707 - Check if there was a data type character after the numeric constant,
        // added by older versions of the fix of the default values for strings.
        switch ( aStr[nParseEnd] )
        {
            // See GetSuffixType in basic/source/comp/scanner.cxx for type characters
            case '%': eType = SbxINTEGER; break;
            case '&': eType = SbxLONG; break;
            case '!': eType = SbxSINGLE; break;
            case '@': eType = SbxCURRENCY; break;
            // tdf#142460 - properly handle boolean values in string pool
            case 'b': eType = SbxBOOL; break;
        }
    }
    // tdf#143707 - if the data type character is different from the default value, it was added
    // in basic/source/comp/symtbl.cxx. Hence, change the type of the numeric constant to be loaded.
    else if (eTypeStr != SbxSTRING)
    {
        eType = eTypeStr;
    }
    SbxVariable* p = new SbxVariable( eType );
    p->PutDouble( n );
    // tdf#133913 - create variable with Variant/Type in order to prevent type conversion errors
    p->ResetFlag( SbxFlagBits::Fixed );
    PushVar( p );
}

// loading a string constant (+ID)

void SbiRuntime::StepLOADSC( sal_uInt32 nOp1 )
{
    SbxVariable* p = new SbxVariable;
    p->PutString( pImg->GetString( nOp1 ) );
    PushVar( p );
}

// Immediate Load (+value)
// The opcode is not generated in SbiExprNode::Gen anymore; used for legacy images

void SbiRuntime::StepLOADI( sal_uInt32 nOp1 )
{
    SbxVariable* p = new SbxVariable;
    p->PutInteger( static_cast<sal_Int16>( nOp1 ) );
    PushVar( p );
}

// store a named argument in Argv (+Arg-no. from 1!)

void SbiRuntime::StepARGN( sal_uInt32 nOp1 )
{
    if( !refArgv.is() )
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    else
    {
        OUString aAlias( pImg->GetString( nOp1 ) );
        SbxVariableRef pVal = PopVar();
        if( bVBAEnabled &&
                ( dynamic_cast<const SbxMethod*>( pVal.get()) != nullptr
                  || dynamic_cast<const SbUnoProperty*>( pVal.get()) != nullptr
                  || dynamic_cast<const SbProcedureProperty*>( pVal.get()) != nullptr ) )
        {
            // named variables ( that are Any especially properties ) can be empty at this point and need a broadcast
            if ( pVal->GetType() == SbxEMPTY )
                pVal->Broadcast( SfxHintId::BasicDataWanted );
            // evaluate methods and properties!
            SbxVariable* pRes = new SbxVariable( *pVal );
            pVal = pRes;
        }
        refArgv->Put(pVal.get(), nArgc);
        refArgv->PutAlias(aAlias, nArgc++);
    }
}

// converting the type of an argument in Argv for DECLARE-Fkt. (+type)

void SbiRuntime::StepARGTYP( sal_uInt32 nOp1 )
{
    if( !refArgv.is() )
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    else
    {
        bool bByVal = (nOp1 & 0x8000) != 0;         // Is BYVAL requested?
        SbxDataType t = static_cast<SbxDataType>(nOp1 & 0x7FFF);
        SbxVariable* pVar = refArgv->Get(refArgv->Count() - 1); // last Arg

        // check BYVAL
        if( pVar->GetRefCount() > 2 )       // 2 is normal for BYVAL
        {
            // parameter is a reference
            if( bByVal )
            {
                // Call by Value is requested -> create a copy
                pVar = new SbxVariable( *pVar );
                pVar->SetFlag( SbxFlagBits::ReadWrite );
                refExprStk->Put(pVar, refArgv->Count() - 1);
            }
            else
                pVar->SetFlag( SbxFlagBits::Reference );     // Ref-Flag for DllMgr
        }
        else
        {
            // parameter is NO reference
            if( bByVal )
                pVar->ResetFlag( SbxFlagBits::Reference );   // no reference -> OK
            else
                Error( ERRCODE_BASIC_BAD_PARAMETERS );      // reference needed
        }

        if( pVar->GetType() != t )
        {
            // variant for correct conversion
            // besides error, if SbxBYREF
            pVar->Convert( SbxVARIANT );
            pVar->Convert( t );
        }
    }
}

// bring string to a definite length (+length)

void SbiRuntime::StepPAD( sal_uInt32 nOp1 )
{
    SbxVariable* p = GetTOS();
    OUString s = p->GetOUString();
    sal_Int32 nLen(nOp1);
    if( s.getLength() == nLen )
        return;

    OUStringBuffer aBuf(s);
    if (aBuf.getLength() > nLen)
    {
        comphelper::string::truncateToLength(aBuf, nLen);
    }
    else
    {
        comphelper::string::padToLength(aBuf, nLen, ' ');
    }
    s = aBuf.makeStringAndClear();
    // Do not modify the original variable inadvertently
    PopVar();
    p = new SbxVariable;
    p->PutString(s);
    PushVar(p);
}

// jump (+target)

void SbiRuntime::StepJUMP( sal_uInt32 nOp1 )
{
#ifdef DBG_UTIL
    // #QUESTION shouldn't this be
    // if( (sal_uInt8*)( nOp1+pImagGetCode() ) >= pImg->GetCodeSize() )
    if( nOp1 >= pImg->GetCodeSize() )
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
#endif
    pCode = pImg->GetCode() + nOp1;
}

bool SbiRuntime::EvaluateTopOfStackAsBool()
{
    SbxVariableRef tos = PopVar();
    // In a test e.g. If Null then
        // will evaluate Null will act as if False
    if ( bVBAEnabled && tos->IsNull() )
    {
        return false;
    }

    // tdf#151503 - do not evaluate a missing optional variable to a boolean
    if (tos->GetType() == SbxERROR && IsMissing(tos.get(), 1))
    {
        Error(ERRCODE_BASIC_NOT_OPTIONAL);
        return false;
    }

    if ( tos->IsObject() )
    {
        //GetBool applied to an Object attempts to dereference and evaluate
            //the underlying value as Bool. Here, we're checking rather that
            //it is not null
        return tos->GetObject();
    }
    else
    {
        return tos->GetBool();
    }
}

// evaluate TOS, conditional jump (+target)

void SbiRuntime::StepJUMPT( sal_uInt32 nOp1 )
{
    if ( EvaluateTopOfStackAsBool() )
    {
        StepJUMP( nOp1 );
    }
}

// evaluate TOS, conditional jump (+target)

void SbiRuntime::StepJUMPF( sal_uInt32 nOp1 )
{
    if ( !EvaluateTopOfStackAsBool() )
    {
        StepJUMP( nOp1 );
    }
}

// evaluate TOS, jump into JUMP-table (+MaxVal)
// looks like this:
// ONJUMP 2
// JUMP target1
// JUMP target2

// if 0x8000 is set in the operand, push the return address (ON..GOSUB)

void SbiRuntime::StepONJUMP( sal_uInt32 nOp1 )
{
    SbxVariableRef p = PopVar();
    sal_Int16 n = p->GetInteger();

    if (nOp1 & 0x8000)
        nOp1 &= 0x7FFF;

    // tdf#160321 - do not execute the jump statement if the expression is out of range
    if (n < 1 || o3tl::make_unsigned(n) > nOp1)
        n = static_cast<sal_Int16>(nOp1 + 1);
    else if (nOp1 & 0x8000)
        PushGosub(pCode + 5 * nOp1);

    nOp1 = static_cast<sal_uInt32>(pCode - pImg->GetCode()) + 5 * --n;
    StepJUMP( nOp1 );
}

// UP-call (+target)

void SbiRuntime::StepGOSUB( sal_uInt32 nOp1 )
{
    PushGosub( pCode );
    if( nOp1 >= pImg->GetCodeSize() )
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    pCode = pImg->GetCode() + nOp1;
}

// UP-return (+0 or target)

void SbiRuntime::StepRETURN( sal_uInt32 nOp1 )
{
    PopGosub();
    if( nOp1 )
        StepJUMP( nOp1 );
}

// check FOR-variable (+Endlabel)

void SbiRuntime::StepTESTFOR( sal_uInt32 nOp1 )
{
    if( !pForStk )
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        return;
    }

    bool bEndLoop = false;
    switch( pForStk->eForType )
    {
        case ForType::To:
        {
            SbxOperator eOp = ( pForStk->refInc->GetDouble() < 0 ) ? SbxLT : SbxGT;
            if( pForStk->refVar->Compare( eOp, *pForStk->refEnd ) )
                bEndLoop = true;
            if (SbxBase::IsError())
                pForStk->eForType = ForType::Error; // terminate loop at the next iteration
            break;
        }
        case ForType::EachArray:
        {
            SbiForStack* p = pForStk;
            if (!p->refEnd)
            {
                SbxBase::SetError(ERRCODE_BASIC_CONVERSION);
                pForStk->eForType = ForType::Error; // terminate loop at the next iteration
            }
            else if (p->pArrayCurIndices == nullptr)
            {
                bEndLoop = true;
            }
            else
            {
                SbxDimArray* pArray = reinterpret_cast<SbxDimArray*>(p->refEnd.get());
                sal_Int32 nDims = pArray->GetDims();

                // Empty array?
                if( nDims == 1 && p->pArrayLowerBounds[0] > p->pArrayUpperBounds[0] )
                {
                    bEndLoop = true;
                    break;
                }
                SbxVariable* pVal = pArray->Get(p->pArrayCurIndices.get());
                *(p->refVar) = *pVal;

                bool bFoundNext = false;
                for(sal_Int32 i = 0 ; i < nDims ; i++ )
                {
                    if( p->pArrayCurIndices[i] < p->pArrayUpperBounds[i] )
                    {
                        bFoundNext = true;
                        p->pArrayCurIndices[i]++;
                        for( sal_Int32 j = i - 1 ; j >= 0 ; j-- )
                            p->pArrayCurIndices[j] = p->pArrayLowerBounds[j];
                        break;
                    }
                }
                if( !bFoundNext )
                {
                    p->pArrayCurIndices.reset();
                }
            }
            break;
        }
        case ForType::EachCollection:
        {
            if (!pForStk->refEnd)
            {
                SbxBase::SetError(ERRCODE_BASIC_CONVERSION);
                pForStk->eForType = ForType::Error; // terminate loop at the next iteration
                break;
            }

            BasicCollection* pCollection = static_cast<BasicCollection*>(pForStk->refEnd.get());
            SbxArrayRef xItemArray = pCollection->xItemArray;
            sal_Int32 nCount = xItemArray->Count();
            if( pForStk->nCurCollectionIndex < nCount )
            {
                SbxVariable* pRes = xItemArray->Get(pForStk->nCurCollectionIndex);
                pForStk->nCurCollectionIndex++;
                (*pForStk->refVar) = *pRes;
            }
            else
            {
                bEndLoop = true;
            }
            break;
        }
        case ForType::EachXEnumeration:
        {
            SbiForStack* p = pForStk;
            if (!p->xEnumeration)
            {
                SbxBase::SetError(ERRCODE_BASIC_CONVERSION);
                pForStk->eForType = ForType::Error; // terminate loop at the next iteration
            }
            else if (p->xEnumeration->hasMoreElements())
            {
                Any aElem = p->xEnumeration->nextElement();
                SbxVariableRef xVar = new SbxVariable( SbxVARIANT );
                unoToSbxValue( xVar.get(), aElem );
                (*pForStk->refVar) = *xVar;
            }
            else
            {
                bEndLoop = true;
            }
            break;
        }
        // tdf#130307 - support for each loop for objects exposing XIndexAccess
        case ForType::EachXIndexAccess:
        {
            SbiForStack* p = pForStk;
            if (!p->xIndexAccess)
            {
                SbxBase::SetError(ERRCODE_BASIC_CONVERSION);
                pForStk->eForType = ForType::Error; // terminate loop at the next iteration
            }
            else if (pForStk->nCurCollectionIndex < p->xIndexAccess->getCount())
            {
                Any aElem = p->xIndexAccess->getByIndex(pForStk->nCurCollectionIndex);
                pForStk->nCurCollectionIndex++;
                SbxVariableRef xVar = new SbxVariable(SbxVARIANT);
                unoToSbxValue(xVar.get(), aElem);
                (*pForStk->refVar) = *xVar;
            }
            else
            {
                bEndLoop = true;
            }
            break;
        }
        case ForType::Error:
        {
            // We are in Resume Next mode after failed loop initialization
            bEndLoop = true;
            Error(ERRCODE_BASIC_BAD_PARAMETER);
            break;
        }
    }
    if( bEndLoop )
    {
        PopFor();
        StepJUMP( nOp1 );
    }
}

// Tos+1 <= Tos+2 <= Tos, 2xremove (+Target)

void SbiRuntime::StepCASETO( sal_uInt32 nOp1 )
{
    if (!refCaseStk.is() || !refCaseStk->Count())
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    else
    {
        SbxVariableRef xTo   = PopVar();
        SbxVariableRef xFrom = PopVar();
        SbxVariableRef xCase = refCaseStk->Get(refCaseStk->Count() - 1);
        if( *xCase >= *xFrom && *xCase <= *xTo )
            StepJUMP( nOp1 );
    }
}


void SbiRuntime::StepERRHDL( sal_uInt32 nOp1 )
{
    const sal_uInt8* p = pCode;
    StepJUMP( nOp1 );
    pError = pCode;
    pCode = p;
    pInst->aErrorMsg.clear();
    pInst->nErr = ERRCODE_NONE;
    pInst->nErl = 0;
    nError = ERRCODE_NONE;
    SbxErrObject::getUnoErrObject()->Clear();
}

// Resume after errors (+0=statement, 1=next or Label)

void SbiRuntime::StepRESUME( sal_uInt32 nOp1 )
{
    // #32714 Resume without error? -> error
    if( !bInError )
    {
        Error( ERRCODE_BASIC_BAD_RESUME );
        return;
    }
    if( nOp1 )
    {
        // set Code-pointer to the next statement
        sal_uInt16 n1, n2;
        pCode = pMod->FindNextStmnt( pErrCode, n1, n2, true, pImg );
    }
    else
        pCode = pErrStmnt;
    if ( pError ) // current in error handler ( and got a Resume Next statement )
        SbxErrObject::getUnoErrObject()->Clear();

    if( nOp1 > 1 )
        StepJUMP( nOp1 );
    pInst->aErrorMsg.clear();
    pInst->nErr = ERRCODE_NONE;
    pInst->nErl = 0;
    nError = ERRCODE_NONE;
    bInError = false;
}

// close channel (+channel, 0=all)
void SbiRuntime::StepCLOSE( sal_uInt32 nOp1 )
{
    ErrCode err;
    if( !nOp1 )
        pIosys->Shutdown();
    else
    {
        err = pIosys->GetError();
        if( !err )
        {
            pIosys->Close();
        }
    }
    err = pIosys->GetError();
    Error( err );
}

// output character (+char)

void SbiRuntime::StepPRCHAR( sal_uInt32 nOp1 )
{
    OUString s(static_cast<sal_Unicode>(nOp1));
    pIosys->Write( s );
    Error( pIosys->GetError() );
}

// check whether TOS is a certain object class (+StringID)

bool SbiRuntime::implIsClass( SbxObject const * pObj, const OUString& aClass )
{
    bool bRet = true;

    if( !aClass.isEmpty() )
    {
        bRet = pObj->IsClass( aClass );
        if( !bRet )
            bRet = aClass.equalsIgnoreAsciiCase( "object" );
        if( !bRet )
        {
            const OUString& aObjClass = pObj->GetClassName();
            SbModule* pClassMod = GetSbData()->pClassFac->FindClass( aObjClass );
            if( pClassMod )
            {
                SbClassData* pClassData = pClassMod->pClassData.get();
                if (pClassData != nullptr )
                {
                    SbxVariable* pClassVar = pClassData->mxIfaces->Find( aClass, SbxClassType::DontCare );
                    bRet = (pClassVar != nullptr);
                }
            }
        }
    }
    return bRet;
}

bool SbiRuntime::checkClass_Impl( const SbxVariableRef& refVal,
    const OUString& aClass, bool bRaiseErrors, bool bDefault )
{
    bool bOk = bDefault;

    SbxDataType t = refVal->GetType();
    SbxVariable* pVal = refVal.get();
    // we don't know the type of uno properties that are (maybevoid)
    if ( t == SbxEMPTY )
    {
        if ( auto pProp = dynamic_cast<SbUnoProperty*>( refVal.get() ) )
        {
            t = pProp->getRealType();
        }
    }
    if( t == SbxOBJECT || bVBAEnabled )
    {
        SbxObject* pObj = dynamic_cast<SbxObject*>(pVal);
        if (!pObj)
        {
            pObj = dynamic_cast<SbxObject*>(refVal->GetObject());
        }
        if( pObj )
        {
            if( !implIsClass( pObj, aClass ) )
            {
                SbUnoObject* pUnoObj(nullptr);
                if (bVBAEnabled || CodeCompleteOptions::IsExtendedTypeDeclaration())
                {
                    pUnoObj = dynamic_cast<SbUnoObject*>(pObj);
                }

                if (pUnoObj)
                    bOk = checkUnoObjectType(*pUnoObj, aClass);
                else
                    bOk = false;
                if ( !bOk && bRaiseErrors )
                    Error( ERRCODE_BASIC_INVALID_USAGE_OBJECT );
            }
            else
            {
                bOk = true;

                SbClassModuleObject* pClassModuleObject = dynamic_cast<SbClassModuleObject*>( pObj );
                if( pClassModuleObject != nullptr )
                    pClassModuleObject->triggerInitializeEvent();
            }
        }
    }
    else
    {
        if( bRaiseErrors )
            Error( ERRCODE_BASIC_NEEDS_OBJECT );
        bOk = false;
    }
    return bOk;
}

void SbiRuntime::StepSETCLASS_impl( sal_uInt32 nOp1, bool bHandleDflt )
{
    SbxVariableRef refVal = PopVar();
    SbxVariableRef refVar = PopVar();
    OUString aClass( pImg->GetString( nOp1 ) );

    bool bOk = checkClass_Impl( refVal, aClass, true, true );
    if( bOk )
    {
        StepSET_Impl( refVal, refVar, bHandleDflt ); // don't do handle default prop for a "proper" set
    }
}

void SbiRuntime::StepVBASETCLASS( sal_uInt32 nOp1 )
{
    StepSETCLASS_impl( nOp1, false );
}

void SbiRuntime::StepSETCLASS( sal_uInt32 nOp1 )
{
    StepSETCLASS_impl( nOp1, true );
}

void SbiRuntime::StepTESTCLASS( sal_uInt32 nOp1 )
{
    SbxVariableRef xObjVal = PopVar();
    OUString aClass( pImg->GetString( nOp1 ) );
    bool bDefault = !bVBAEnabled;
    bool bOk = checkClass_Impl( xObjVal, aClass, false, bDefault );

    SbxVariable* pRet = new SbxVariable;
    pRet->PutBool( bOk );
    PushVar( pRet );
}

// define library for following declare-call

void SbiRuntime::StepLIB( sal_uInt32 nOp1 )
{
    aLibName = pImg->GetString( nOp1 );
}

// TOS is incremented by BASE, BASE is pushed before (+BASE)
// This opcode is pushed before DIM/REDIM-commands,
// if there's been only one index named.

void SbiRuntime::StepBASED( sal_uInt32 nOp1 )
{
    SbxVariable* p1 = new SbxVariable;
    SbxVariableRef x2 = PopVar();

    // #109275 Check compatibility mode
    bool bCompatible = ((nOp1 & 0x8000) != 0);
    sal_uInt16 uBase = static_cast<sal_uInt16>(nOp1 & 1);       // Can only be 0 or 1
    p1->PutInteger( uBase );
    if( !bCompatible )
    {
        // tdf#85371 - grant explicitly write access to the dimension variable
        // since in Star/OpenOffice Basic the upper index border is affected,
        // and the dimension variable could be the name of the method itself.
        ScopedWritableGuard aGuard(x2, x2.get() == pMeth);
        x2->Compute( SbxPLUS, *p1 );
    }
    PushVar( x2.get() );  // first the Expr
    PushVar( p1 );  // then the Base
}

// the bits in the String-ID:
// 0x8000 - Argv is reserved

SbxVariable* SbiRuntime::FindElement( SbxObject* pObj, sal_uInt32 nOp1, sal_uInt32 nOp2,
                                      ErrCode nNotFound, bool bLocal, bool bStatic )
{
    bool bIsVBAInterOp = SbiRuntime::isVBAEnabled();
    if( bIsVBAInterOp )
    {
        StarBASIC* pMSOMacroRuntimeLib = GetSbData()->pMSOMacroRuntimLib;
        if( pMSOMacroRuntimeLib != nullptr )
        {
            pMSOMacroRuntimeLib->ResetFlag( SbxFlagBits::ExtSearch );
        }
    }

    SbxVariable* pElem = nullptr;
    if( !pObj )
    {
        Error( ERRCODE_BASIC_NO_OBJECT );
        pElem = new SbxVariable;
    }
    else
    {
        bool bFatalError = false;
        SbxDataType t = static_cast<SbxDataType>(nOp2);
        OUString aName( pImg->GetString( nOp1 & 0x7FFF ) );
        // Hacky capture of Evaluate [] syntax
        // this should be tackled I feel at the pcode level
        if ( bIsVBAInterOp && aName.startsWith("[") )
        {
            // emulate pcode here
            StepARGC();
            // pseudo StepLOADSC
            OUString sArg = aName.copy( 1, aName.getLength() - 2 );
            SbxVariable* p = new SbxVariable;
            p->PutString( sArg );
            PushVar( p );
            StepARGV();
            nOp1 = nOp1 | 0x8000; // indicate params are present
            aName = "Evaluate";
        }
        if( bLocal )
        {
            if ( bStatic && pMeth )
            {
                pElem = pMeth->GetStatics()->Find( aName, SbxClassType::DontCare );
            }

            if ( !pElem )
            {
                pElem = refLocals->Find( aName, SbxClassType::DontCare );
            }
        }
        if( !pElem )
        {
            bool bSave = rBasic.bNoRtl;
            rBasic.bNoRtl = true;
            pElem = pObj->Find( aName, SbxClassType::DontCare );

            // #110004, #112015: Make private really private
            if( bLocal && pElem )   // Local as flag for global search
            {
                if( pElem->IsSet( SbxFlagBits::Private ) )
                {
                    SbiInstance* pInst_ = GetSbData()->pInst;
                    if( pInst_ && pInst_->IsCompatibility() && pObj != pElem->GetParent() )
                    {
                        pElem = nullptr;   // Found but in wrong module!
                    }
                    // Interfaces: Use SbxFlagBits::ExtFound
                }
            }
            rBasic.bNoRtl = bSave;

            // is it a global uno-identifier?
            if( bLocal && !pElem )
            {
                bool bSetName = true; // preserve normal behaviour

                // i#i68894# if VBAInterOp favour searching vba globals
                // over searching for uno classes
                if ( bVBAEnabled )
                {
                    // Try Find in VBA symbols space
                    pElem = rBasic.VBAFind( aName, SbxClassType::DontCare );
                    if ( pElem )
                    {
                        bSetName = false; // don't overwrite uno name
                    }
                    else
                    {
                        pElem = VBAConstantHelper::instance().getVBAConstant( aName );
                    }
                }

                if( !pElem )
                {
                    // #72382 ATTENTION! ALWAYS returns a result now
                    // because of unknown modules!
                    SbUnoClass* pUnoClass = findUnoClass( aName );
                    if( pUnoClass )
                    {
                        pElem = new SbxVariable( t );
                        SbxValues aRes( SbxOBJECT );
                        aRes.pObj = pUnoClass;
                        pElem->SbxVariable::Put( aRes );
                    }
                }

                // #62939 If a uno-class has been found, the wrapper
                // object has to be held, because the uno-class, e. g.
                // "stardiv", has to be read out of the registry
                // every time again otherwise
                if( pElem )
                {
                    // #63774 May not be saved too!!!
                    pElem->SetFlag( SbxFlagBits::DontStore );
                    pElem->SetFlag( SbxFlagBits::NoModify);

                    // #72382 save locally, all variables that have been declared
                    // implicit would become global automatically otherwise!
                    if ( bSetName )
                    {
                        pElem->SetName( aName );
                    }
                    refLocals->Put(pElem, refLocals->Count());
                }
            }

            if( !pElem )
            {
                // not there and not in the object?
                // don't establish if that thing has parameters!
                if( nOp1 & 0x8000 )
                {
                    bFatalError = true;
                }

                // else, if there are parameters, use different error code
                if( !bLocal || pImg->IsFlag( SbiImageFlags::EXPLICIT ) )
                {
                    // #39108 if explicit and as ELEM always a fatal error
                    bFatalError = true;


                    if( !( nOp1 & 0x8000 ) && nNotFound == ERRCODE_BASIC_PROC_UNDEFINED )
                    {
                        nNotFound = ERRCODE_BASIC_VAR_UNDEFINED;
                    }
                }
                if( bFatalError )
                {
                    // #39108 use dummy variable instead of fatal error
                    if( !xDummyVar.is() )
                    {
                        xDummyVar = new SbxVariable( SbxVARIANT );
                    }
                    pElem = xDummyVar.get();

                    ClearArgvStack();

                    Error( nNotFound, aName );
                }
                else
                {
                    if ( bStatic )
                    {
                        pElem = StepSTATIC_Impl( aName, t, 0 );
                    }
                    if ( !pElem )
                    {
                        pElem = new SbxVariable( t );
                        if( t != SbxVARIANT )
                        {
                            pElem->SetFlag( SbxFlagBits::Fixed );
                        }
                        pElem->SetName( aName );
                        refLocals->Put(pElem, refLocals->Count());
                    }
                }
            }
        }
        // #39108 Args can already be deleted!
        if( !bFatalError )
        {
            SetupArgs( pElem, nOp1 );
        }
        // because a particular call-type is requested
        if (SbxMethod* pMethod = dynamic_cast<SbxMethod*>(pElem))
        {
            // shall the type be converted?
            SbxDataType t2 = pElem->GetType();
            bool bSet = false;
            if( (pElem->GetFlags() & SbxFlagBits::Fixed) == SbxFlagBits::NONE )
            {
                if( t != SbxVARIANT && t != t2 &&
                    t >= SbxINTEGER && t <= SbxSTRING )
                {
                    pElem->SetType( t );
                    bSet = true;
                }
            }
            // assign pElem to a Ref, to delete a temp-var if applicable
            SbxVariableRef xDeleteRef = pElem;

            // remove potential rests of the last call of the SbxMethod
            // free Write before, so that there's no error
            SbxFlagBits nSavFlags = pElem->GetFlags();
            pElem->SetFlag( SbxFlagBits::ReadWrite | SbxFlagBits::NoBroadcast );
            pElem->SbxValue::Clear();
            pElem->SetFlags( nSavFlags );

            // don't touch before setting, as e. g. LEFT()
            // has to know the difference between Left$() and Left()

            // because the methods' parameters are cut away in PopVar()
            SbxVariable* pNew = new SbxMethod(*pMethod);
            //OLD: SbxVariable* pNew = new SbxVariable( *pElem );

            pElem->SetParameters(nullptr);
            pNew->SetFlag( SbxFlagBits::ReadWrite );

            if( bSet )
            {
                pElem->SetType( t2 );
            }
            pElem = pNew;
        }
        // consider index-access for UnoObjects
        // definitely we want this for VBA where properties are often
        // collections ( which need index access ), but let's only do
        // this if we actually have params following
        else if( bVBAEnabled && dynamic_cast<const SbUnoProperty*>( pElem) != nullptr && pElem->GetParameters() )
        {
            SbxVariableRef xDeleteRef = pElem;

            // dissolve the notify while copying variable
            SbxVariable* pNew = new SbxVariable( *pElem );
            pElem->SetParameters( nullptr );
            pElem = pNew;
        }
    }
    return CheckArray( pElem );
}

// for current scope (e. g. query from BASIC-IDE)
SbxBase* SbiRuntime::FindElementExtern( const OUString& rName )
{
    // don't expect pMeth to be != 0, as there are none set
    // in the RunInit yet

    SbxVariable* pElem = nullptr;
    if( !pMod || rName.isEmpty() )
    {
        return nullptr;
    }
    if( refLocals.is() )
    {
        pElem = refLocals->Find( rName, SbxClassType::DontCare );
    }
    if ( !pElem && pMeth )
    {
        const OUString aMethName = pMeth->GetName();
        // tdf#57308 - check if the name is the current method instance
        if (pMeth->GetName() == rName)
        {
            pElem = pMeth;
        }
        else
        {
            // for statics, set the method's name in front
            pElem = pMod->Find(aMethName + ":" + rName, SbxClassType::DontCare);
        }
    }


    // search in parameter list
    if( !pElem && pMeth )
    {
        SbxInfo* pInfo = pMeth->GetInfo();
        if( pInfo && refParams.is() )
        {
            sal_uInt32 nParamCount = refParams->Count();
            assert(nParamCount <= std::numeric_limits<sal_uInt16>::max());
            sal_uInt16 j = 1;
            const SbxParamInfo* pParam = pInfo->GetParam( j );
            while( pParam )
            {
                if( pParam->aName.equalsIgnoreAsciiCase( rName ) )
                {
                    if( j >= nParamCount )
                    {
                        // Parameter is missing
                        pElem = new SbxVariable( SbxSTRING );
                        pElem->PutString( u"<missing parameter>"_ustr);
                    }
                    else
                    {
                        pElem = refParams->Get(j);
                    }
                    break;
                }
                pParam = pInfo->GetParam( ++j );
            }
        }
    }

    // search in module
    if( !pElem )
    {
        bool bSave = rBasic.bNoRtl;
        rBasic.bNoRtl = true;
        pElem = pMod->Find( rName, SbxClassType::DontCare );
        rBasic.bNoRtl = bSave;
    }
    return pElem;
}


void SbiRuntime::SetupArgs( SbxVariable* p, sal_uInt32 nOp1 )
{
    if( nOp1 & 0x8000 )
    {
        if( !refArgv.is() )
        {
            StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
        }
        bool bHasNamed = false;
        sal_uInt32 i;
        sal_uInt32 nArgCount = refArgv->Count();
        for( i = 1 ; i < nArgCount ; i++ )
        {
            if (!refArgv->GetAlias(i).isEmpty())
            {
                bHasNamed = true; break;
            }
        }
        if( bHasNamed )
        {
            SbxInfo* pInfo = p->GetInfo();
            if( !pInfo )
            {
                bool bError_ = true;

                SbUnoMethod* pUnoMethod = dynamic_cast<SbUnoMethod*>( p );
                SbUnoProperty* pUnoProperty = dynamic_cast<SbUnoProperty*>( p );
                if( pUnoMethod || pUnoProperty )
                {
                    SbUnoObject* pParentUnoObj = dynamic_cast<SbUnoObject*>( p->GetParent()  );
                    if( pParentUnoObj )
                    {
                        Any aUnoAny = pParentUnoObj->getUnoAny();
                        Reference< XInvocation > xInvocation;
                        aUnoAny >>= xInvocation;
                        if( xInvocation.is() )  // TODO: if( xOLEAutomation.is() )
                        {
                            bError_ = false;

                            sal_uInt32 nCurPar = 1;
                            AutomationNamedArgsSbxArray* pArg =
                                new AutomationNamedArgsSbxArray( nArgCount );
                            OUString* pNames = pArg->getNames().getArray();
                            for( i = 1 ; i < nArgCount ; i++ )
                            {
                                SbxVariable* pVar = refArgv->Get(i);
                                OUString aName = refArgv->GetAlias(i);
                                if (!aName.isEmpty())
                                {
                                    pNames[i] = aName;
                                }
                                pArg->Put(pVar, nCurPar++);
                            }
                            refArgv = pArg;
                        }
                    }
                }
                else if( bVBAEnabled && p->GetType() == SbxOBJECT && (dynamic_cast<const SbxMethod*>( p) == nullptr || !p->IsBroadcaster()) )
                {
                    // Check for default method with named parameters
                    SbxBaseRef xObj = p->GetObject();
                    if (SbUnoObject* pUnoObj = dynamic_cast<SbUnoObject*>( xObj.get() ))
                    {
                        Any aAny = pUnoObj->getUnoAny();

                        if( aAny.getValueTypeClass() == TypeClass_INTERFACE )
                        {
                            Reference< XDefaultMethod > xDfltMethod( aAny, UNO_QUERY );

                            OUString sDefaultMethod;
                            if ( xDfltMethod.is() )
                            {
                                sDefaultMethod = xDfltMethod->getDefaultMethodName();
                            }
                            if ( !sDefaultMethod.isEmpty() )
                            {
                                SbxVariable* meth = pUnoObj->Find( sDefaultMethod, SbxClassType::Method );
                                if( meth != nullptr )
                                {
                                    pInfo = meth->GetInfo();
                                }
                                if( pInfo )
                                {
                                    bError_ = false;
                                }
                            }
                        }
                    }
                }
                if( bError_ )
                {
                    Error( ERRCODE_BASIC_NO_NAMED_ARGS );
                }
            }
            else
            {
                sal_uInt32 nCurPar = 1;
                SbxArray* pArg = new SbxArray;
                for( i = 1 ; i < nArgCount ; i++ )
                {
                    SbxVariable* pVar = refArgv->Get(i);
                    OUString aName = refArgv->GetAlias(i);
                    if (!aName.isEmpty())
                    {
                        // nCurPar is set to the found parameter
                        sal_uInt16 j = 1;
                        const SbxParamInfo* pParam = pInfo->GetParam( j );
                        while( pParam )
                        {
                            if( pParam->aName.equalsIgnoreAsciiCase( aName ) )
                            {
                                nCurPar = j;
                                break;
                            }
                            pParam = pInfo->GetParam( ++j );
                        }
                        if( !pParam )
                        {
                            Error( ERRCODE_BASIC_NAMED_NOT_FOUND ); break;
                        }
                    }
                    pArg->Put(pVar, nCurPar++);
                }
                refArgv = pArg;
            }
        }
        // own var as parameter 0
        refArgv->Put(p, 0);
        p->SetParameters( refArgv.get() );
        PopArgv();
    }
    else
    {
        p->SetParameters( nullptr );
    }
}

// getting an array element

SbxVariable* SbiRuntime::CheckArray( SbxVariable* pElem )
{
    assert(pElem);
    if( ( pElem->GetType() & SbxARRAY ) && refRedim.get() != pElem )
    {
        SbxBase* pElemObj = pElem->GetObject();
        SbxArray* pPar = pElem->GetParameters();
        if (SbxDimArray* pDimArray = dynamic_cast<SbxDimArray*>(pElemObj))
        {
            // parameters may be missing, if an array is
            // passed as an argument
            if( pPar )
            {
                bool parIsArrayIndex = true;
                if (dynamic_cast<const SbxMethod*>(pElem))
                {
                    // If this was a method, then there are two possibilities:
                    // 1. pPar is this method's parameters.
                    // 2. pPar is the indexes into the array returned from the method.
                    // To disambiguate, check the 0th element of pPar.
                    if (dynamic_cast<const SbxMethod*>(pPar->Get(0)))
                    {
                        // pPar was the parameters to the method, not indexes into the array
                        parIsArrayIndex = false;
                    }
                }
                if (parIsArrayIndex)
                    pElem = pDimArray->Get(pPar);
            }
        }
        else if (SbxArray* pArray = dynamic_cast<SbxArray*>(pElemObj))
        {
            if( !pPar )
            {
                Error( ERRCODE_BASIC_OUT_OF_RANGE );
                pElem = new SbxVariable;
            }
            else
            {
                pElem = pArray->Get(pPar->Get(1)->GetInteger());
            }
        }

        // #42940, set parameter 0 to NULL so that var doesn't contain itself
        if( pPar )
        {
            pPar->Put(nullptr, 0);
        }
    }
    // consider index-access for UnoObjects
    else if( pElem->GetType() == SbxOBJECT &&
            dynamic_cast<const SbxMethod*>( pElem) == nullptr &&
            ( !bVBAEnabled || dynamic_cast<const SbxProperty*>( pElem) == nullptr ) )
    {
        if (SbxArray* pPar = pElem->GetParameters())
        {
            // is it a uno-object?
            SbxBaseRef pObj = pElem->GetObject();
            if( pObj.is() )
            {
                if (SbUnoObject* pUnoObj = dynamic_cast<SbUnoObject*>( pObj.get()))
                {
                    Any aAny = pUnoObj->getUnoAny();

                    if( aAny.getValueTypeClass() == TypeClass_INTERFACE )
                    {
                        Reference< XIndexAccess > xIndexAccess( aAny, UNO_QUERY );
                        if ( !bVBAEnabled )
                        {
                            if( xIndexAccess.is() )
                            {
                                sal_uInt32 nParamCount = pPar->Count() - 1;
                                if( nParamCount != 1 )
                                {
                                    StarBASIC::Error( ERRCODE_BASIC_BAD_ARGUMENT );
                                    return pElem;
                                }

                                // get index
                                sal_Int32 nIndex = pPar->Get(1)->GetLong();
                                Reference< XInterface > xRet;
                                try
                                {
                                    Any aAny2 = xIndexAccess->getByIndex( nIndex );
                                    aAny2 >>= xRet;
                                }
                                catch (const IndexOutOfBoundsException&)
                                {
                                    // usually expect converting problem
                                    StarBASIC::Error( ERRCODE_BASIC_OUT_OF_RANGE );
                                }

                                // #57847 always create a new variable, else error
                                // due to PutObject(NULL) at ReadOnly-properties
                                pElem = new SbxVariable( SbxVARIANT );
                                if( xRet.is() )
                                {
                                    aAny <<= xRet;

                                    // #67173 don't specify a name so that the real class name is entered
                                    SbxObjectRef xWrapper = static_cast<SbxObject*>(new SbUnoObject( OUString(), aAny ));
                                    pElem->PutObject( xWrapper.get() );
                                }
                                else
                                {
                                    pElem->PutObject( nullptr );
                                }
                            }
                        }
                        else
                        {
                            // check if there isn't a default member between the current variable
                            // and the params, e.g.
                            //   Dim rst1 As New ADODB.Recordset
                            //      "
                            //   val = rst1("FirstName")
                            // has the default 'Fields' member between rst1 and '("FirstName")'
                            Any x = aAny;
                            SbxVariable* pDflt = getDefaultProp( pElem );
                            if ( pDflt )
                            {
                                pDflt->Broadcast( SfxHintId::BasicDataWanted );
                                SbxBaseRef pDfltObj = pDflt->GetObject();
                                if( pDfltObj.is() )
                                {
                                    if (SbUnoObject* pSbObj = dynamic_cast<SbUnoObject*>(pDfltObj.get()))
                                    {
                                        pUnoObj = pSbObj;
                                        Any aUnoAny = pUnoObj->getUnoAny();

                                        if( aUnoAny.getValueTypeClass() == TypeClass_INTERFACE )
                                            x = std::move(aUnoAny);
                                        pElem = pDflt;
                                    }
                                }
                            }
                            OUString sDefaultMethod;

                            Reference< XDefaultMethod > xDfltMethod( x, UNO_QUERY );

                            if ( xDfltMethod.is() )
                            {
                                sDefaultMethod = xDfltMethod->getDefaultMethodName();
                            }
                            else if( xIndexAccess.is() )
                            {
                                sDefaultMethod = "getByIndex";
                            }
                            if ( !sDefaultMethod.isEmpty() )
                            {
                                SbxVariable* meth = pUnoObj->Find( sDefaultMethod, SbxClassType::Method );
                                SbxVariableRef refTemp = meth;
                                if ( refTemp.is() )
                                {
                                    meth->SetParameters( pPar );
                                    SbxVariable* pNew = new SbxMethod( *static_cast<SbxMethod*>(meth) );
                                    pElem = pNew;
                                }
                            }
                        }
                    }

                    // #42940, set parameter 0 to NULL so that var doesn't contain itself
                    pPar->Put(nullptr, 0);
                }
                else if (BasicCollection* pCol = dynamic_cast<BasicCollection*>(pObj.get()))
                {
                    pElem = new SbxVariable( SbxVARIANT );
                    pPar->Put(pElem, 0);
                    pCol->CollItem( pPar );
                }
            }
            else if( bVBAEnabled )  // !pObj
            {
                SbxArray* pParam = pElem->GetParameters();
                if( pParam != nullptr && !pElem->IsSet( SbxFlagBits::VarToDim ) )
                {
                    Error( ERRCODE_BASIC_NO_OBJECT );
                }
            }
        }
    }

    return pElem;
}

// loading an element from the runtime-library (+StringID+type)

void SbiRuntime::StepRTL( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    PushVar( FindElement( rBasic.pRtl.get(), nOp1, nOp2, ERRCODE_BASIC_PROC_UNDEFINED, false ) );
}

void SbiRuntime::StepFIND_Impl( SbxObject* pObj, sal_uInt32 nOp1, sal_uInt32 nOp2,
                                ErrCode nNotFound, bool bStatic )
{
    if( !refLocals.is() )
    {
        refLocals = new SbxArray;
    }
    PushVar( FindElement( pObj, nOp1, nOp2, nNotFound, true/*bLocal*/, bStatic ) );
}
// loading a local/global variable (+StringID+type)

void SbiRuntime::StepFIND( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    StepFIND_Impl( pMod, nOp1, nOp2, ERRCODE_BASIC_PROC_UNDEFINED );
}

// Search inside a class module (CM) to enable global search in time
void SbiRuntime::StepFIND_CM( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{

    SbClassModuleObject* pClassModuleObject = dynamic_cast<SbClassModuleObject*>( pMod );
    if( pClassModuleObject )
    {
        pMod->SetFlag( SbxFlagBits::GlobalSearch );
    }
    StepFIND_Impl( pMod, nOp1, nOp2, ERRCODE_BASIC_PROC_UNDEFINED);

    if( pClassModuleObject )
    {
        pMod->ResetFlag( SbxFlagBits::GlobalSearch );
    }
}

void SbiRuntime::StepFIND_STATIC( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    StepFIND_Impl( pMod, nOp1, nOp2, ERRCODE_BASIC_PROC_UNDEFINED, true );
}

// loading an object-element (+StringID+type)
// the object lies on TOS

void SbiRuntime::StepELEM( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    SbxVariableRef pObjVar = PopVar();

    SbxObject* pObj = dynamic_cast<SbxObject*>( pObjVar.get() );
    if( !pObj )
    {
        SbxBase* pObjVarObj = pObjVar->GetObject();
        pObj = dynamic_cast<SbxObject*>( pObjVarObj );
    }

    // #56368 save reference at StepElem, otherwise objects could
    // lose their reference too early in qualification chains like
    // ActiveComponent.Selection(0).Text
    // #74254 now per list
    if( pObj )
    {
        aRefSaved.emplace_back(pObj );
    }
    PushVar( FindElement( pObj, nOp1, nOp2, ERRCODE_BASIC_NO_METHOD, false ) );
}

/** Loading of a parameter (+offset+type)
    If the data type is wrong, create a copy and search for optionals including
    the default value. The data type SbxEMPTY shows that no parameters are given.
    Get( 0 ) may be EMPTY

    @param nOp1
    the index of the current parameter being processed,
    where the entry of the index 0 is for the return value.

    @param nOp2
    the data type of the parameter.
 */
void SbiRuntime::StepPARAM( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    sal_uInt16 nIdx = static_cast<sal_uInt16>( nOp1 & 0x7FFF );
    SbxDataType eType = static_cast<SbxDataType>(nOp2);
    SbxVariable* pVar;

    // #57915 solve missing in a cleaner way
    sal_uInt32 nParamCount = refParams->Count();
    if( nIdx >= nParamCount )
    {
        sal_uInt16 iLoop = nIdx;
        while( iLoop >= nParamCount )
        {
            pVar = new SbxVariable();
            pVar->PutErr( 448 );       // like in VB: Error-Code 448 (ERRCODE_BASIC_NAMED_NOT_FOUND)
            // tdf#79426, tdf#125180 - add additional information about a missing parameter
            SetIsMissing( pVar );
            refParams->Put(pVar, iLoop);
            iLoop--;
        }
    }
    pVar = refParams->Get(nIdx);

    // tdf#79426, tdf#125180 - check for optionals only if the parameter is actually missing
    if( pVar->GetType() == SbxERROR && IsMissing( pVar, 1 ) && nIdx )
    {
        // if there's a parameter missing, it can be OPTIONAL
        bool bOpt = false;
        if( pMeth )
        {
            SbxInfo* pInfo = pMeth->GetInfo();
            if ( pInfo )
            {
                const SbxParamInfo* pParam = pInfo->GetParam( nIdx );
                if( pParam && ( pParam->nFlags & SbxFlagBits::Optional ) )
                {
                    // tdf#136143 - reset SbxFlagBits::Fixed in order to prevent type conversion errors
                    pVar->ResetFlag( SbxFlagBits::Fixed );
                    // Default value?
                    sal_uInt16 nDefaultId = static_cast<sal_uInt16>(pParam->nUserData & 0x0ffff);
                    if( nDefaultId > 0 )
                    {
                        // tdf#143707 - check if the data type character was added after the string
                        // termination symbol, and convert the variable if it was present. The
                        // data type character was added in basic/source/comp/symtbl.cxx.
                        SbxDataType eTypeStr;
                        OUString aDefaultStr = pImg->GetString( nDefaultId, &eTypeStr );
                        pVar = new SbxVariable(pParam-> eType);
                        pVar->PutString( aDefaultStr );
                        if (eTypeStr != SbxSTRING)
                            pVar->Convert(eTypeStr);
                        refParams->Put(pVar, nIdx);
                    }
                    else if ( SbiRuntime::isVBAEnabled() && eType != SbxVARIANT )
                    {
                        // tdf#36737 - initialize the parameter with the default value of its type
                        pVar = new SbxVariable( pParam->eType );
                        refParams->Put(pVar, nIdx);
                    }
                    bOpt = true;
                }
            }
        }
        if( !bOpt )
        {
            Error( ERRCODE_BASIC_NOT_OPTIONAL );
        }
    }
    else if( eType != SbxVARIANT && static_cast<SbxDataType>(pVar->GetType() & 0x0FFF ) != eType )
    {
        // tdf#43003 - convert parameter to the requested type
        pVar->Convert(eType);
    }
    SetupArgs( pVar, nOp1 );
    PushVar( CheckArray( pVar ) );
}

// Case-Test (+True-Target+Test-Opcode)

void SbiRuntime::StepCASEIS( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    if (!refCaseStk.is() || !refCaseStk->Count())
    {
        StarBASIC::FatalError( ERRCODE_BASIC_INTERNAL_ERROR );
    }
    else
    {
        SbxVariableRef xComp = PopVar();
        SbxVariableRef xCase = refCaseStk->Get(refCaseStk->Count() - 1);
        if( xCase->Compare( static_cast<SbxOperator>(nOp2), *xComp ) )
        {
            StepJUMP( nOp1 );
        }
    }
}

// call of a DLL-procedure (+StringID+type)
// the StringID's MSB shows that Argv is occupied

void SbiRuntime::StepCALL( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    OUString aName = pImg->GetString( nOp1 & 0x7FFF );
    SbxArray* pArgs = nullptr;
    if( nOp1 & 0x8000 )
    {
        pArgs = refArgv.get();
    }
    DllCall( aName, aLibName, pArgs, static_cast<SbxDataType>(nOp2), false );
    aLibName.clear();
    if( nOp1 & 0x8000 )
    {
        PopArgv();
    }
}

// call of a DLL-procedure after CDecl (+StringID+type)

void SbiRuntime::StepCALLC( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    OUString aName = pImg->GetString( nOp1 & 0x7FFF );
    SbxArray* pArgs = nullptr;
    if( nOp1 & 0x8000 )
    {
        pArgs = refArgv.get();
    }
    DllCall( aName, aLibName, pArgs, static_cast<SbxDataType>(nOp2), true );
    aLibName.clear();
    if( nOp1 & 0x8000 )
    {
        PopArgv();
    }
}


// beginning of a statement (+Line+Col)

void SbiRuntime::StepSTMNT( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    // If the Expr-Stack at the beginning of a statement contains a variable,
    // some fool has called X as a function, although it's a variable!
    bool bFatalExpr = false;
    OUString sUnknownMethodName;
    if( nExprLvl > 1 )
    {
        bFatalExpr = true;
    }
    else if( nExprLvl )
    {
        SbxVariable* p = refExprStk->Get(0);
        if( p->GetRefCount() > 1 &&
            refLocals.is() && refLocals->Find( p->GetName(), p->GetClass() ) )
        {
            sUnknownMethodName = p->GetName();
            bFatalExpr = true;
        }
    }

    ClearExprStack();

    aRefSaved.clear();

    // We have to cancel hard here because line and column
    // would be wrong later otherwise!
    if( bFatalExpr)
    {
        StarBASIC::FatalError( ERRCODE_BASIC_NO_METHOD, sUnknownMethodName );
        return;
    }
    pStmnt = pCode - 9;
    sal_uInt16 nOld = nLine;
    nLine = static_cast<short>( nOp1 );

    // #29955 & 0xFF, to filter out for-loop-level
    nCol1 = static_cast<short>( nOp2 & 0xFF );

    // find the next STMNT-command to set the final column
    // of this statement

    nCol2 = 0xffff;
    sal_uInt16 n1, n2;
    const sal_uInt8* p = pMod->FindNextStmnt( pCode, n1, n2 );
    if( p )
    {
        if( n1 == nOp1 )
        {
            // #29955 & 0xFF, to filter out for-loop-level
            nCol2 = (n2 & 0xFF) - 1;
        }
    }

    // #29955 correct for-loop-level, #67452 NOT in the error-handler
    if( !bInError )
    {
        // (there's a difference here in case of a jump out of a loop)
        sal_uInt16 nExpectedForLevel = static_cast<sal_uInt16>( nOp2 / 0x100 );
        if( !pGosubStk.empty() )
        {
            nExpectedForLevel = nExpectedForLevel + pGosubStk.back().nStartForLvl;
        }

        // if the actual for-level is too small it'd jump out
        // of a loop -> corrected
        while( nForLvl > nExpectedForLevel )
        {
            PopFor();
        }
    }

    // 16.10.96: #31460 new concept for StepInto/Over/Out
    // see explanation at SbiInstance::CalcBreakCallLevel
    if( pInst->nCallLvl <= pInst->nBreakCallLvl )
    {
        StarBASIC* pStepBasic = GetCurrentBasic( &rBasic );
        BasicDebugFlags nNewFlags = pStepBasic->StepPoint( nLine, nCol1, nCol2 );

        pInst->CalcBreakCallLevel( nNewFlags );
    }

    // break points only at STMNT-commands in a new line!
    else if( ( nOp1 != nOld )
        && ( nFlags & BasicDebugFlags::Break )
        && pMod->IsBP( static_cast<sal_uInt16>( nOp1 ) ) )
    {
        StarBASIC* pBreakBasic = GetCurrentBasic( &rBasic );
        BasicDebugFlags nNewFlags = pBreakBasic->BreakPoint( nLine, nCol1, nCol2 );

        pInst->CalcBreakCallLevel( nNewFlags );
    }
}

// (+StreamMode+Flags)
// Stack: block length
//        channel number
//        file name

void SbiRuntime::StepOPEN( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    SbxVariableRef pName = PopVar();
    SbxVariableRef pChan = PopVar();
    SbxVariableRef pLen  = PopVar();
    short nBlkLen = pLen->GetInteger();
    short nChan   = pChan->GetInteger();
    OUString aName = pName->GetOUString();
    pIosys->Open( nChan, aName, static_cast<StreamMode>( nOp1 ),
                  static_cast<SbiStreamFlags>( nOp2 ), nBlkLen );
    Error( pIosys->GetError() );
}

// create object (+StringID+StringID)

void SbiRuntime::StepCREATE( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    OUString aClass( pImg->GetString( nOp2 ) );
    SbxObjectRef pObj = SbxBase::CreateObject( aClass );
    if( !pObj )
    {
        Error( ERRCODE_BASIC_INVALID_OBJECT );
    }
    else
    {
        OUString aName( pImg->GetString( nOp1 ) );
        pObj->SetName( aName );
        // the object must be able to call the BASIC
        pObj->SetParent( &rBasic );
        SbxVariableRef pNew = new SbxVariable;
        pNew->PutObject( pObj.get() );
        PushVar( pNew.get() );
    }
}

void SbiRuntime::StepDCREATE( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    StepDCREATE_IMPL( nOp1, nOp2 );
}

void SbiRuntime::StepDCREATE_REDIMP( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    StepDCREATE_IMPL( nOp1, nOp2 );
}

// #56204 create object array (+StringID+StringID), DCREATE == Dim-Create
void SbiRuntime::StepDCREATE_IMPL( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    SbxVariableRef refVar = PopVar();

    DimImpl( refVar );

    // fill the array with instances of the requested class
    SbxBase* pObj = refVar->GetObject();
    if (!pObj)
    {
        StarBASIC::Error( ERRCODE_BASIC_INVALID_OBJECT );
        return;
    }

    SbxDimArray* pArray = dynamic_cast<SbxDimArray*>(pObj);
    if (!pArray)
        return;

    const sal_Int32 nDims = pArray->GetDims();
    sal_Int32 nTotalSize = nDims > 0 ? 1 : 0;

    // must be a one-dimensional array
    sal_Int32 nLower, nUpper;
    for( sal_Int32 i = 0 ; i < nDims ; ++i )
    {
        pArray->GetDim(i + 1, nLower, nUpper);
        const sal_Int32 nSize = nUpper - nLower + 1;
        nTotalSize *= nSize;
    }

    // Optimization: pre-allocate underlying container
    if (nTotalSize > 0)
        pArray->SbxArray::GetRef(nTotalSize - 1);

    // First, fill those parts of the array that are preserved
    bool bWasError = false;
    const bool bRestored = implRestorePreservedArray(pArray, refRedimpArray, &bWasError);
    if (bWasError)
        nTotalSize = 0; // on error, don't create objects

    // create objects and insert them into the array
    OUString aClass( pImg->GetString( nOp2 ) );
    OUString aName;
    for( sal_Int32 i = 0 ; i < nTotalSize ; ++i )
    {
        if (!bRestored || !pArray->SbxArray::GetRef(i)) // For those left unset after preserve
        {
            SbxObjectRef pClassObj = SbxBase::CreateObject(aClass);
            if (!pClassObj)
            {
                Error(ERRCODE_BASIC_INVALID_OBJECT);
                break;
            }
            else
            {
                if (aName.isEmpty())
                    aName = pImg->GetString(nOp1);
                pClassObj->SetName(aName);
                // the object must be able to call the basic
                pClassObj->SetParent(&rBasic);
                pArray->SbxArray::Put(pClassObj.get(), i);
            }
        }
    }
}

void SbiRuntime::StepTCREATE( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    OUString aName( pImg->GetString( nOp1 ) );
    OUString aClass( pImg->GetString( nOp2 ) );

    SbxObjectRef pCopyObj = createUserTypeImpl( aClass );
    if( pCopyObj )
    {
        pCopyObj->SetName( aName );
    }
    SbxVariableRef pNew = new SbxVariable;
    pNew->PutObject( pCopyObj.get() );
    pNew->SetDeclareClassName( aClass );
    PushVar( pNew.get() );
}

void SbiRuntime::implHandleSbxFlags( SbxVariable* pVar, SbxDataType t, sal_uInt32 nOp2 )
{
    bool bWithEvents = ((t & 0xff) == SbxOBJECT && (nOp2 & SBX_TYPE_WITH_EVENTS_FLAG) != 0);
    if( bWithEvents )
    {
        pVar->SetFlag( SbxFlagBits::WithEvents );
    }
    bool bDimAsNew = ((nOp2 & SBX_TYPE_DIM_AS_NEW_FLAG) != 0);
    if( bDimAsNew )
    {
        pVar->SetFlag( SbxFlagBits::DimAsNew );
    }
    bool bFixedString = ((t & 0xff) == SbxSTRING && (nOp2 & SBX_FIXED_LEN_STRING_FLAG) != 0);
    if( bFixedString )
    {
        sal_uInt16 nCount = static_cast<sal_uInt16>( nOp2 >> 17 );      // len = all bits above 0x10000
        OUStringBuffer aBuf(nCount);
        comphelper::string::padToLength(aBuf, nCount);
        pVar->PutString(aBuf.makeStringAndClear());
    }

    bool bVarToDim = ((nOp2 & SBX_TYPE_VAR_TO_DIM_FLAG) != 0);
    if( bVarToDim )
    {
        pVar->SetFlag( SbxFlagBits::VarToDim );
    }
}

// establishing a local variable (+StringID+type)

void SbiRuntime::StepLOCAL( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    if( !refLocals.is() )
    {
        refLocals = new SbxArray;
    }
    OUString aName( pImg->GetString( nOp1 ) );
    if( refLocals->Find( aName, SbxClassType::DontCare ) == nullptr )
    {
        SbxDataType t = static_cast<SbxDataType>(nOp2 & 0xffff);
        SbxVariable* p = new SbxVariable( t );
        p->SetName( aName );
        implHandleSbxFlags( p, t, nOp2 );
        refLocals->Put(p, refLocals->Count());
    }
}

// establishing a module-global variable (+StringID+type)

void SbiRuntime::StepPUBLIC_Impl( sal_uInt32 nOp1, sal_uInt32 nOp2, bool bUsedForClassModule )
{
    OUString aName( pImg->GetString( nOp1 ) );
    SbxDataType t = static_cast<SbxDataType>(nOp2 & 0xffff);
    bool bFlag = pMod->IsSet( SbxFlagBits::NoModify );
    pMod->SetFlag( SbxFlagBits::NoModify );
    SbxVariableRef p = pMod->Find( aName, SbxClassType::Property );
    if( p.is() )
    {
        pMod->Remove (p.get());
    }
    SbProperty* pProp = pMod->GetProperty( aName, t );
    if( !bUsedForClassModule )
    {
        pProp->SetFlag( SbxFlagBits::Private );
    }
    if( !bFlag )
    {
        pMod->ResetFlag( SbxFlagBits::NoModify );
    }
    if( pProp )
    {
        pProp->SetFlag( SbxFlagBits::DontStore );
        // from 2.7.1996: HACK because of 'reference can't be saved'
        pProp->SetFlag( SbxFlagBits::NoModify);

        implHandleSbxFlags( pProp, t, nOp2 );
    }
}

void SbiRuntime::StepPUBLIC( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    StepPUBLIC_Impl( nOp1, nOp2, false );
}

void SbiRuntime::StepPUBLIC_P( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    // Creates module variable that isn't reinitialised when
    // between invocations ( for VBASupport & document basic only )
    if( pMod->pImage->bFirstInit )
    {
        bool bUsedForClassModule = pImg->IsFlag( SbiImageFlags::CLASSMODULE );
        StepPUBLIC_Impl( nOp1, nOp2, bUsedForClassModule );
    }
}

// establishing a global variable (+StringID+type)

void SbiRuntime::StepGLOBAL( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    if( pImg->IsFlag( SbiImageFlags::CLASSMODULE ) )
    {
        StepPUBLIC_Impl( nOp1, nOp2, true );
    }
    OUString aName( pImg->GetString( nOp1 ) );
    SbxDataType t = static_cast<SbxDataType>(nOp2 & 0xffff);

    // Store module scope variables at module scope
    // in non vba mode these are stored at the library level :/
    // not sure if this really should not be enabled for ALL basic
    SbxObject* pStorage = &rBasic;
    if ( SbiRuntime::isVBAEnabled() )
    {
        pStorage = pMod;
        pMod->AddVarName( aName );
    }

    bool bFlag = pStorage->IsSet( SbxFlagBits::NoModify );
    rBasic.SetFlag( SbxFlagBits::NoModify );
    SbxVariableRef p = pStorage->Find( aName, SbxClassType::Property );
    if( p.is() )
    {
        pStorage->Remove (p.get());
    }
    p = pStorage->Make( aName, SbxClassType::Property, t );
    if( !bFlag )
    {
        pStorage->ResetFlag( SbxFlagBits::NoModify );
    }
    if( p.is() )
    {
        p->SetFlag( SbxFlagBits::DontStore );
        // from 2.7.1996: HACK because of 'reference can't be saved'
        p->SetFlag( SbxFlagBits::NoModify);
    }
}


// Creates global variable that isn't reinitialised when
// basic is restarted, P=PERSIST (+StringID+Typ)

void SbiRuntime::StepGLOBAL_P( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    if( pMod->pImage->bFirstInit )
    {
        StepGLOBAL( nOp1, nOp2 );
    }
}


// Searches for global variable, behavior depends on the fact
// if the variable is initialised for the first time

void SbiRuntime::StepFIND_G( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    if( pMod->pImage->bFirstInit )
    {
        // Behave like always during first init
        StepFIND( nOp1, nOp2 );
    }
    else
    {
        // Return dummy variable
        SbxDataType t = static_cast<SbxDataType>(nOp2);
        OUString aName( pImg->GetString( nOp1 & 0x7FFF ) );

        SbxVariable* pDummyVar = new SbxVariable( t );
        pDummyVar->SetName( aName );
        PushVar( pDummyVar );
    }
}


SbxVariable* SbiRuntime::StepSTATIC_Impl(
    OUString const & aName, SbxDataType t, sal_uInt32 nOp2 )
{
    SbxVariable* p = nullptr;
    if ( pMeth )
    {
        SbxArray* pStatics = pMeth->GetStatics();
        if( pStatics && ( pStatics->Find( aName, SbxClassType::DontCare ) == nullptr ) )
        {
            p = new SbxVariable( t );
            if( t != SbxVARIANT )
            {
                p->SetFlag( SbxFlagBits::Fixed );
            }
            p->SetName( aName );
            implHandleSbxFlags( p, t, nOp2 );
            pStatics->Put(p, pStatics->Count());
        }
    }
    return p;
}
// establishing a static variable (+StringID+type)
void SbiRuntime::StepSTATIC( sal_uInt32 nOp1, sal_uInt32 nOp2 )
{
    OUString aName( pImg->GetString( nOp1 ) );
    SbxDataType t = static_cast<SbxDataType>(nOp2 & 0xffff);
    StepSTATIC_Impl( aName, t, nOp2 );
}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
