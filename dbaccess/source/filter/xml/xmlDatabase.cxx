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

#include "xmlDatabase.hxx"
#include "xmlfilter.hxx"
#include <xmloff/xmltoken.hxx>
#include <xmloff/ProgressBarHelper.hxx>
#include "xmlDataSource.hxx"
#include "xmlDocuments.hxx"
#include "xmlEnums.hxx"
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sdb/XReportDocumentsSupplier.hpp>
#include <com/sun/star/sdb/XFormDocumentsSupplier.hpp>
#include <com/sun/star/sdbcx/XTablesSupplier.hpp>
#include <com/sun/star/sdb/XQueryDefinitionsSupplier.hpp>
#include <strings.hxx>
#include <connectivity/dbtools.hxx>

namespace dbaxml
{
    using namespace ::com::sun::star::uno;
    using namespace ::com::sun::star::sdb;
    using namespace ::com::sun::star::sdbcx;
    using namespace ::com::sun::star::xml::sax;

OXMLDatabase::OXMLDatabase( ODBFilter& rImport ) :
    SvXMLImportContext( rImport )
{

}

OXMLDatabase::~OXMLDatabase()
{

}

css::uno::Reference< css::xml::sax::XFastContextHandler > OXMLDatabase::createFastChildContext(
            sal_Int32 nElement, const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList )
{
    SvXMLImportContext *pContext = nullptr;

    switch( nElement & TOKEN_MASK )
    {
        case XML_DATA_SOURCE:
            GetOwnImport().GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
            pContext = new OXMLDataSource( GetOwnImport(), xAttrList, OXMLDataSource::eDataSource );
            break;
        case XML_FORMS:
            {
                GetOwnImport().GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                Any aValue;
                OUString sService;
                dbtools::getDataSourceSetting(GetOwnImport().getDataSource(), u"Forms"_ustr, aValue);
                aValue >>= sService;
                if ( sService.isEmpty() )
                {
                    Reference<XFormDocumentsSupplier> xSup(GetOwnImport().GetModel(),UNO_QUERY);
                    if ( xSup.is() )
                        pContext = new OXMLDocuments( GetOwnImport(), xSup->getFormDocuments(),SERVICE_NAME_FORM_COLLECTION,SERVICE_SDB_DOCUMENTDEFINITION);
                }
            }
            break;
        case XML_REPORTS:
            {
                GetOwnImport().GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                Any aValue;
                OUString sService;
                dbtools::getDataSourceSetting(GetOwnImport().getDataSource(), u"Reports"_ustr, aValue);
                aValue >>= sService;
                if ( sService.isEmpty() )
                {
                    Reference<XReportDocumentsSupplier> xSup(GetOwnImport().GetModel(),UNO_QUERY);
                    if ( xSup.is() )
                        pContext = new OXMLDocuments( GetOwnImport(), xSup->getReportDocuments(),SERVICE_NAME_REPORT_COLLECTION,SERVICE_SDB_DOCUMENTDEFINITION);
                }
            }
            break;
        case XML_QUERIES:
            {
                GetOwnImport().GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                Any aValue;
                OUString sService;
                dbtools::getDataSourceSetting(GetOwnImport().getDataSource(), u"CommandDefinitions"_ustr, aValue);
                aValue >>= sService;
                if ( sService.isEmpty() )
                {
                    Reference<XQueryDefinitionsSupplier> xSup(GetOwnImport().getDataSource(),UNO_QUERY);
                    if ( xSup.is() )
                        pContext = new OXMLDocuments( GetOwnImport(), xSup->getQueryDefinitions(),SERVICE_NAME_QUERY_COLLECTION);
                }
            }
            break;
        case XML_TABLE_REPRESENTATIONS:
        case XML_SCHEMA_DEFINITION:
            {
                GetOwnImport().GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                Reference<XTablesSupplier> xSup(GetOwnImport().getDataSource(),UNO_QUERY);
                if ( xSup.is() )
                    pContext = new OXMLDocuments( GetOwnImport(), xSup->getTables());
            }
            break;
    }

    return pContext;
}

ODBFilter& OXMLDatabase::GetOwnImport()
{
    return static_cast<ODBFilter&>(GetImport());
}

void OXMLDatabase::endFastElement(sal_Int32)
{
    GetOwnImport().setPropertyInfo();
}

} // namespace dbaxml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
