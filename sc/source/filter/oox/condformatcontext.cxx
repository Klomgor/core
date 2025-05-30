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

#include <condformatcontext.hxx>
#include <extlstcontext.hxx>

#include <biffhelper.hxx>
#include <condformatbuffer.hxx>
#include <oox/token/namespaces.hxx>
#include <utility>

namespace oox::xls {

using ::oox::core::ContextHandlerRef;

ColorScaleContext::ColorScaleContext( CondFormatContext& rFragment, CondFormatRule& rRule ) :
    WorksheetContextBase( rFragment ),
    mrRule( rRule )
{
}

ContextHandlerRef ColorScaleContext::onCreateContext( sal_Int32 nElement, const AttributeList& )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( cfRule ):
            return (nElement == XLS_TOKEN( colorScale )) ? this : nullptr;
        case XLS_TOKEN( colorScale ):
            if (nElement == XLS_TOKEN( cfvo ))
                return this;
            else if (nElement == XLS_TOKEN( color ))
                return this;
            else
                return nullptr;
    }
    return nullptr;
}

void ColorScaleContext::onStartElement( const AttributeList& rAttribs )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( cfvo ):
            mrRule.getColorScale()->importCfvo( rAttribs );
        break;
        case XLS_TOKEN( color ):
            mrRule.getColorScale()->importColor( rAttribs );
        break;
    }
}

DataBarContext::DataBarContext( CondFormatContext& rFragment, CondFormatRule& rRule ) :
    WorksheetContextBase( rFragment ),
    mrRule( rRule )
{
}

ContextHandlerRef DataBarContext::onCreateContext( sal_Int32 nElement, const AttributeList& )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( cfRule ):
            return (nElement == XLS_TOKEN( dataBar )) ? this : nullptr;
        case XLS_TOKEN( dataBar ):
            if (nElement == XLS_TOKEN( cfvo ))
                return this;
            else if (nElement == XLS_TOKEN( color ))
                return this;
            else
                return nullptr;
    }
    return nullptr;
}

void DataBarContext::onStartElement( const AttributeList& rAttribs )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( dataBar ):
            mrRule.getDataBar()->importAttribs( rAttribs );
        break;
        case XLS_TOKEN( cfvo ):
            mrRule.getDataBar()->importCfvo( rAttribs );
        break;
        case XLS_TOKEN( color ):
            mrRule.getDataBar()->importColor( rAttribs );
        break;
    }
}

IconSetContext::IconSetContext(WorksheetContextBase& rParent, IconSetRule* pIconSet) :
    WorksheetContextBase(rParent),
    mpIconSet(pIconSet)
{
}

ContextHandlerRef IconSetContext::onCreateContext( sal_Int32 nElement, const AttributeList& )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( cfRule ):
        case XLS14_TOKEN( cfRule ):
            return (nElement == XLS_TOKEN( iconSet ) || nElement == XLS14_TOKEN(iconSet)) ? this : nullptr;
        case XLS_TOKEN( iconSet ):
        case XLS14_TOKEN(iconSet):
            if (nElement == XLS_TOKEN( cfvo ) ||
                    nElement == XLS14_TOKEN(cfvo) ||
                    nElement == XLS14_TOKEN(cfIcon))
                return this;
            else
                return nullptr;
        case XLS14_TOKEN(cfvo):
            if (nElement == XM_TOKEN(f))
                return this;
    }
    return nullptr;
}

void IconSetContext::onStartElement( const AttributeList& rAttribs )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( iconSet ):
        case XLS14_TOKEN( iconSet ):
            mpIconSet->importAttribs( rAttribs );
        break;
        case XLS_TOKEN( cfvo ):
        case XLS14_TOKEN( cfvo ):
            mpIconSet->importCfvo( rAttribs );
        break;
        case XLS14_TOKEN(cfIcon):
            mpIconSet->importIcon(rAttribs);
        break;
    }
}

void IconSetContext::onCharacters(const OUString& rChars)
{
    maChars = rChars;
}

void IconSetContext::onEndElement()
{
    switch(getCurrentElement())
    {
        case XM_TOKEN(f):
            mpIconSet->importFormula(maChars);
            maChars = OUString();
        break;
    }
}

CondFormatContext::CondFormatContext( WorksheetFragmentBase& rFragment ) :
    WorksheetContextBase( rFragment )
{
}

ContextHandlerRef CondFormatContext::onCreateContext( sal_Int32 nElement, const AttributeList& )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( conditionalFormatting ):
            return (nElement == XLS_TOKEN( cfRule )) ? this : nullptr;
        case XLS_TOKEN( cfRule ):
            if (nElement == XLS_TOKEN( formula ))
                return this;
            else if (nElement == XLS_TOKEN( colorScale ) )
                return new ColorScaleContext( *this, *mxRule );
            else if (nElement == XLS_TOKEN( dataBar ) )
                return new DataBarContext( *this, *mxRule );
            else if (nElement == XLS_TOKEN( iconSet ) )
                return new IconSetContext(*this, mxRule->getIconSet());
            else if (nElement == XLS_TOKEN( extLst ) )
                return new ExtLstLocalContext( *this, mxRule->getDataBar()->getDataBarFormatData() );
            else
                return nullptr;
    }
    return nullptr;
}

void CondFormatContext::onEndElement()
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( conditionalFormatting ):
            if(mxCondFmt)
                mxCondFmt->setReadyForFinalize();
            break;
        case XLS_TOKEN( cfRule ):
            if (mxCondFmt && mxRule)
                mxCondFmt->insertRule(std::move(mxRule));
        break;
    }
}

void CondFormatContext::onStartElement( const AttributeList& rAttribs )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( conditionalFormatting ):
            mxCondFmt = getCondFormats().importConditionalFormatting( rAttribs );
        break;
        case XLS_TOKEN( cfRule ):
            if( mxCondFmt ) mxRule = mxCondFmt->importCfRule( rAttribs );
        break;
    }
}

void CondFormatContext::onCharacters( const OUString& rChars )
{
    if( isCurrentElement( XLS_TOKEN( formula ) ) && mxCondFmt && mxRule )
        mxRule->appendFormula( rChars );
}

ContextHandlerRef CondFormatContext::onCreateRecordContext( sal_Int32 nRecId, SequenceInputStream& )
{
    switch( getCurrentElement() )
    {
        case BIFF12_ID_CONDFORMATTING:
            return (nRecId == BIFF12_ID_CFRULE) ? this : nullptr;
    }
    return nullptr;
}

void CondFormatContext::onStartRecord( SequenceInputStream& rStrm )
{
    switch( getCurrentElement() )
    {
        case BIFF12_ID_CONDFORMATTING:
            mxCondFmt = getCondFormats().importCondFormatting( rStrm );
        break;
        case BIFF12_ID_CFRULE:
            if( mxCondFmt ) mxCondFmt->importCfRule( rStrm );
        break;
    }
}

void CondFormatContext::onEndRecord()
{
    switch( getCurrentElement() )
    {
        case BIFF12_ID_CONDFORMATTING:
            if( mxCondFmt )
            {
                mxCondFmt->setReadyForFinalize();
            }
            break;
    }
}

} // namespace oox::xls

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
