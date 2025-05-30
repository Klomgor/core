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

#include "SelectionBrowseBox.hxx"
#include <com/sun/star/sdbc/XDatabaseMetaData.hpp>
#include <com/sun/star/sdbc/DataType.hpp>
#include <JoinExchange.hxx>
#include <QueryDesignView.hxx>
#include <querycontroller.hxx>
#include <sqlbison.hxx>
#include <QueryTableView.hxx>
#include <browserids.hxx>
#include <comphelper/stl_types.hxx>
#include <comphelper/string.hxx>
#include "TableFieldInfo.hxx"
#include <core_resource.hxx>
#include <strings.hrc>
#include <strings.hxx>
#include <helpids.h>
#include "QTableWindow.hxx"
#include <vcl/weld.hxx>
#include <vcl/settings.hxx>
#include "QueryDesignFieldUndoAct.hxx"
#include <sqlmessage.hxx>
#include <UITools.hxx>
#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <i18nlangtag/languagetag.hxx>
#include <vcl/commandevent.hxx>
#include <vcl/svapp.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <o3tl/string_view.hxx>

using namespace ::svt;
using namespace ::dbaui;
using namespace ::connectivity;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::sdbc;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::accessibility;

#define DEFAULT_QUERY_COLS  20
#define DEFAULT_SIZE        GetTextWidth(u"0"_ustr) * 30
#define HANDLE_ID            0
#define HANDLE_COLUMN_WIDTH 70
#define SORT_COLUMN_NONE    0xFFFFFFFF

namespace
{
    bool isFieldNameAsterisk(std::u16string_view _sFieldName )
    {
        bool bAsterisk = _sFieldName.empty() || _sFieldName[0] == '*';
        if ( !bAsterisk )
        {
            sal_Int32 nTokenCount = comphelper::string::getTokenCount(_sFieldName, '.');
            if (    (nTokenCount == 2 && o3tl::getToken(_sFieldName,1,'.')[0] == '*' )
                ||  (nTokenCount == 3 && o3tl::getToken(_sFieldName,2,'.')[0] == '*' ) )
            {
                bAsterisk = true;
            }
        }
        return bAsterisk;
    }
    bool lcl_SupportsCoreSQLGrammar(const Reference< XConnection>& _xConnection)
    {
        bool bSupportsCoreGrammar = false;
        if ( _xConnection.is() )
        {
            try
            {
                Reference< XDatabaseMetaData >  xMetaData = _xConnection->getMetaData();
                bSupportsCoreGrammar = xMetaData.is() && xMetaData->supportsCoreSQLGrammar();
            }
            catch(Exception&)
            {
            }
        }
        return bSupportsCoreGrammar;
    }
}

OSelectionBrowseBox::OSelectionBrowseBox( vcl::Window* pParent )
                   :EditBrowseBox( pParent,EditBrowseBoxFlags::NO_HANDLE_COLUMN_CONTENT, WB_3DLOOK, BrowserMode::COLUMNSELECTION | BrowserMode::KEEPHIGHLIGHT |  BrowserMode::HIDESELECT |
                                  BrowserMode::HIDECURSOR | BrowserMode::HLINES | BrowserMode::VLINES )
                   ,m_timerInvalidate("dbaccess OSelectionBrowseBox m_timerInvalidate")
                   ,m_nSeekRow(0)
                   ,m_nMaxColumns(0)
                   ,m_aFunctionStrings(DBA_RES(STR_QUERY_FUNCTIONS))
                   ,m_nVisibleCount(0)
                   ,m_nLastSortColumn(SORT_COLUMN_NONE)
                   ,m_bOrderByUnRelated(true)
                   ,m_bGroupByUnRelated(true)
                   ,m_bStopTimer(false)
                   ,m_bWasEditing(false)
                   ,m_bDisableErrorBox(false)
                   ,m_bInUndoMode(false)
{
    SetHelpId(HID_CTL_QRYDGNCRIT);

    m_nMode =       BrowserMode::COLUMNSELECTION | BrowserMode::HIDESELECT
                |   BrowserMode::KEEPHIGHLIGHT   | BrowserMode::HIDECURSOR
                |   BrowserMode::HLINES      | BrowserMode::VLINES
                |   BrowserMode::HEADERBAR_NEW   ;

    m_pTextCell     = VclPtr<EditControl>::Create(&GetDataWindow());
    m_pVisibleCell  = VclPtr<CheckBoxControl>::Create(&GetDataWindow());
    m_pTableCell    = VclPtr<ListBoxControl>::Create(&GetDataWindow());
    m_pFieldCell    = VclPtr<ComboBoxControl>::Create(&GetDataWindow());
    m_pOrderCell    = VclPtr<ListBoxControl>::Create(&GetDataWindow());
    m_pFunctionCell = VclPtr<ListBoxControl>::Create(&GetDataWindow());

    m_pVisibleCell->SetHelpId(HID_QRYDGN_ROW_VISIBLE);
    m_pTableCell->SetHelpId(HID_QRYDGN_ROW_TABLE);
    m_pFieldCell->SetHelpId(HID_QRYDGN_ROW_FIELD);
    weld::ComboBox& rOrderBox = m_pOrderCell->get_widget();
    m_pOrderCell->SetHelpId(HID_QRYDGN_ROW_ORDER);
    m_pFunctionCell->SetHelpId(HID_QRYDGN_ROW_FUNCTION);

    // switch off triState of css::form::CheckBox
    m_pVisibleCell->EnableTriState( false );

    vcl::Font aTitleFont = OutputDevice::GetDefaultFont( DefaultFontType::SANS_UNICODE,Window::GetSettings().GetLanguageTag().getLanguageType(),GetDefaultFontFlags::OnlyOne);
    aTitleFont.SetFontSize(Size(0, 6));
    SetTitleFont(aTitleFont);

    const OUString aTxt(DBA_RES(STR_QUERY_SORTTEXT));
    for (sal_Int32 nIdx {0}; nIdx>=0;)
        rOrderBox.append_text(OUString(o3tl::getToken(aTxt, 0, ';', nIdx)));

    m_bVisibleRow.insert(m_bVisibleRow.end(), BROW_ROW_CNT, true);

    m_bVisibleRow[BROW_FUNCTION_ROW] = false;   // first hide

    m_timerInvalidate.SetTimeout(200);
    m_timerInvalidate.SetInvokeHandler(LINK(this, OSelectionBrowseBox, OnInvalidateTimer));
    m_timerInvalidate.Start();
}

OSelectionBrowseBox::~OSelectionBrowseBox()
{
    disposeOnce();
}

void OSelectionBrowseBox::dispose()
{
    m_pTextCell.disposeAndClear();
    m_pVisibleCell.disposeAndClear();
    m_pFieldCell.disposeAndClear();
    m_pTableCell.disposeAndClear();
    m_pOrderCell.disposeAndClear();
    m_pFunctionCell.disposeAndClear();
    ::svt::EditBrowseBox::dispose();
}

void OSelectionBrowseBox::initialize()
{
    Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
    if(xConnection.is())
    {
        const IParseContext& rContext = static_cast<OQueryController&>(getDesignView()->getController()).getParser().getContext();
        const IParseContext::InternationalKeyCode eFunctions[] = {
            IParseContext::InternationalKeyCode::Avg,IParseContext::InternationalKeyCode::Count,IParseContext::InternationalKeyCode::Max
            ,IParseContext::InternationalKeyCode::Min,IParseContext::InternationalKeyCode::Sum
            ,IParseContext::InternationalKeyCode::Every
            ,IParseContext::InternationalKeyCode::Any
            ,IParseContext::InternationalKeyCode::Some
            ,IParseContext::InternationalKeyCode::StdDevPop
            ,IParseContext::InternationalKeyCode::StdDevSamp
            ,IParseContext::InternationalKeyCode::VarSamp
            ,IParseContext::InternationalKeyCode::VarPop
            ,IParseContext::InternationalKeyCode::Collect
            ,IParseContext::InternationalKeyCode::Fusion
            ,IParseContext::InternationalKeyCode::Intersection
        };

        OUString sGroup = m_aFunctionStrings.copy(m_aFunctionStrings.lastIndexOf(';')+1);
        m_aFunctionStrings = m_aFunctionStrings.getToken(0, ';');

        for (IParseContext::InternationalKeyCode eFunction : eFunctions)
        {
            m_aFunctionStrings += ";" + OStringToOUString(rContext.getIntlKeywordAscii(eFunction), RTL_TEXTENCODING_UTF8);
        }
        m_aFunctionStrings += ";" + sGroup;

        // Aggregate functions in general available only with Core SQL
        // We slip in a few optionals one, too.
        if ( lcl_SupportsCoreSQLGrammar(xConnection) )
        {
            weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
            for (sal_Int32 nIdx {0}; nIdx>=0;)
                rComboBox.append_text(m_aFunctionStrings.getToken(0, ';', nIdx));
        }
        else // else only COUNT(*) and COUNT("table".*)
        {
            weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
            rComboBox.append_text(m_aFunctionStrings.getToken(0, ';'));
            rComboBox.append_text(m_aFunctionStrings.getToken(2, ';')); // 2 -> COUNT
        }
        try
        {
            Reference< XDatabaseMetaData >  xMetaData = xConnection->getMetaData();
            if ( xMetaData.is() )
            {
                m_bOrderByUnRelated = xMetaData->supportsOrderByUnrelated();
                m_bGroupByUnRelated = xMetaData->supportsGroupByUnrelated();
            }
        }
        catch(Exception&)
        {
        }
    }

    Init();
}

OQueryDesignView* OSelectionBrowseBox::getDesignView()
{
    OSL_ENSURE(static_cast<const OQueryDesignView*>(GetParent()),"Parent isn't an OQueryDesignView!");
    return static_cast<OQueryDesignView*>(GetParent());
}

OQueryDesignView* OSelectionBrowseBox::getDesignView() const
{
    OSL_ENSURE(static_cast<const OQueryDesignView*>(GetParent()),"Parent isn't an OQueryDesignView!");
    return static_cast<OQueryDesignView*>(GetParent());
}

namespace
{
    class OSelectionBrwBoxHeader : public ::svt::EditBrowserHeader
    {
        VclPtr<OSelectionBrowseBox> m_pBrowseBox;
    protected:
        virtual void Select() override;
    public:
        explicit OSelectionBrwBoxHeader(OSelectionBrowseBox* pParent);
        virtual ~OSelectionBrwBoxHeader() override { disposeOnce(); }
        virtual void dispose() override { m_pBrowseBox.reset(); ::svt::EditBrowserHeader::dispose(); }
    };
    OSelectionBrwBoxHeader::OSelectionBrwBoxHeader(OSelectionBrowseBox* pParent)
        : ::svt::EditBrowserHeader(pParent,WB_BUTTONSTYLE|WB_DRAG)
        ,m_pBrowseBox(pParent)
    {
    }

    void OSelectionBrwBoxHeader::Select()
    {
        EditBrowserHeader::Select();
        m_pBrowseBox->GrabFocus();

        BrowserMode nMode = m_pBrowseBox->GetMode();
        if ( 0 == m_pBrowseBox->GetSelectColumnCount() )
        {
            m_pBrowseBox->DeactivateCell();
            // we are in the right mode if a row has been selected row
            if ( nMode & BrowserMode::HIDESELECT )
            {
                nMode &= ~BrowserMode::HIDESELECT;
                nMode |= BrowserMode::MULTISELECTION;
                m_pBrowseBox->SetMode( nMode );
            }
        }
        m_pBrowseBox->SelectColumnId( GetCurItemId() );
        m_pBrowseBox->DeactivateCell();
    }
}

VclPtr<BrowserHeader> OSelectionBrowseBox::imp_CreateHeaderBar(BrowseBox* /*pParent*/)
{
    return VclPtr<OSelectionBrwBoxHeader>::Create(this);
}

void OSelectionBrowseBox::ColumnMoved( sal_uInt16 nColId, bool _bCreateUndo )
{
    EditBrowseBox::ColumnMoved( nColId );
    // swap the two columns
    sal_uInt16 nNewPos = GetColumnPos( nColId );
    OTableFields& rFields = getFields();
    if ( rFields.size() > o3tl::make_unsigned(nNewPos-1) )
    {
        sal_uInt16 nOldPos = 0;
        bool bFoundElem = false;
        for (auto const& field : rFields)
        {
            if (field->GetColumnId() == nColId)
            {
                bFoundElem = true;
                break;
            }
            ++nOldPos;
        }

        OSL_ENSURE( (nNewPos-1) != nOldPos && nOldPos < rFields.size(),"Old and new position are equal!");
        if (bFoundElem)
        {
            OTableFieldDescRef pOldEntry = rFields[nOldPos];
            rFields.erase(rFields.begin() + nOldPos);
            rFields.insert(rFields.begin() + nNewPos - 1,pOldEntry);

            // create the undo action
            if ( !m_bInUndoMode && _bCreateUndo )
            {
                std::unique_ptr<OTabFieldMovedUndoAct> pUndoAct(new OTabFieldMovedUndoAct(this));
                pUndoAct->SetColumnPosition( nOldPos + 1);
                pUndoAct->SetTabFieldDescr(pOldEntry);

                getDesignView()->getController().addUndoActionAndInvalidate(std::move(pUndoAct));
            }
        }
    }
    else
        OSL_FAIL("Invalid column id!");
}

void OSelectionBrowseBox::Init()
{

    EditBrowseBox::Init();

    // set the header bar
    VclPtr<BrowserHeader> pNewHeaderBar = CreateHeaderBar(this);
    pNewHeaderBar->SetMouseTransparent(false);

    SetHeaderBar(pNewHeaderBar);
    SetMode(m_nMode);

    vcl::Font aFont( GetDataWindow().GetFont() );
    aFont.SetWeight( WEIGHT_NORMAL );
    GetDataWindow().SetFont( aFont );

    Size aHeight;
    const Control* pControls[] = { m_pTextCell,m_pVisibleCell,m_pTableCell,m_pFieldCell };

    for (const Control* pControl : pControls)
    {
        const Size aTemp(pControl->GetOptimalSize());
        if ( aTemp.Height() > aHeight.Height() )
            aHeight.setHeight( aTemp.Height() );
    }
    SetDataRowHeight(aHeight.Height());
    SetTitleLines(1);
    // get number of visible rows
    for(tools::Long i=0;i<BROW_ROW_CNT;i++)
    {
        if(m_bVisibleRow[i])
            m_nVisibleCount++;
    }
    RowInserted(0, m_nVisibleCount, false);
    try
    {
        Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
        if(xConnection.is())
        {
            Reference< XDatabaseMetaData >  xMetaData = xConnection->getMetaData();
            m_nMaxColumns = xMetaData.is() ? xMetaData->getMaxColumnsInSelect() : 0;

        }
        else
            m_nMaxColumns = 0;
    }
    catch(const SQLException&)
    {
        TOOLS_WARN_EXCEPTION( "dbaccess", "Caught Exception when asking for database metadata options!");
        m_nMaxColumns = 0;
    }
}

void OSelectionBrowseBox::PreFill()
{
    SetUpdateMode(false);

    if (GetCurRow() != 0)
        GoToRow(0);

    static_cast< OQueryController& >( getDesignView()->getController() ).clearFields();

    DeactivateCell();

    RemoveColumns();
    InsertHandleColumn( HANDLE_COLUMN_WIDTH );
    SetUpdateMode(true);
}

void OSelectionBrowseBox::ClearAll()
{
    SetUpdateMode(false);

    OTableFields::const_reverse_iterator aIter = getFields().rbegin();
    for ( ;aIter != getFields().rend(); ++aIter )
    {
        if ( !(*aIter)->IsEmpty() )
        {
            RemoveField( (*aIter)->GetColumnId() );
            aIter = getFields().rbegin();
        }
    }
    m_nLastSortColumn = SORT_COLUMN_NONE;
    SetUpdateMode(true);
}

void OSelectionBrowseBox::SetReadOnly(bool bRO)
{
    if (bRO)
    {
        DeactivateCell();
        m_nMode &= ~BrowserMode::HIDECURSOR;
        SetMode(m_nMode);
    }
    else
    {
        m_nMode |= BrowserMode::HIDECURSOR;
        SetMode(m_nMode);
        ActivateCell();
    }
}

CellController* OSelectionBrowseBox::GetController(sal_Int32 nRow, sal_uInt16 nColId)
{
    if ( nColId > getFields().size() )
        return nullptr;
    OTableFieldDescRef pEntry = getFields()[nColId-1];
    OSL_ENSURE(pEntry.is(), "OSelectionBrowseBox::GetController : invalid FieldDescription !");

    if (!pEntry.is())
        return nullptr;

    if (static_cast<OQueryController&>(getDesignView()->getController()).isReadOnly())
        return nullptr;

    sal_Int32 nCellIndex = GetRealRow(nRow);
    switch (nCellIndex)
    {
        case BROW_FIELD_ROW:
            return new ComboBoxCellController(m_pFieldCell);
        case BROW_TABLE_ROW:
            return new ListBoxCellController(m_pTableCell);
        case BROW_VIS_ROW:
            return new CheckBoxCellController(m_pVisibleCell);
        case BROW_ORDER_ROW:
            return new ListBoxCellController(m_pOrderCell);
        case BROW_FUNCTION_ROW:
            return new ListBoxCellController(m_pFunctionCell);
        default:
            return new EditCellController(m_pTextCell);
    }
}

void OSelectionBrowseBox::InitController(CellControllerRef& /*rController*/, sal_Int32 nRow, sal_uInt16 nColId)
{
    OSL_ENSURE(nColId != BROWSER_INVALIDID,"An Invalid Id was set!");
    if ( nColId == BROWSER_INVALIDID )
        return;
    sal_uInt16 nPos = GetColumnPos(nColId);
    if ( nPos == 0 || nPos == BROWSER_INVALIDID || nPos > getFields().size() )
        return;
    OTableFieldDescRef pEntry = getFields()[nPos-1];
    OSL_ENSURE(pEntry.is(), "OSelectionBrowseBox::InitController : invalid FieldDescription !");
    sal_Int32 nCellIndex = GetRealRow(nRow);

    switch (nCellIndex)
    {
        case BROW_FIELD_ROW:
        {
            weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
            rComboBox.clear();
            rComboBox.set_entry_text(OUString());

            OUString aField(pEntry->GetField());
            OUString aTable(pEntry->GetAlias());

            getDesignView()->fillValidFields(aTable, rComboBox);

            // replace with alias.*
            if (o3tl::trim(aField) == u"*")
            {
                aField = aTable + ".*";
            }
            rComboBox.set_entry_text(aField);
        }   break;
        case BROW_TABLE_ROW:
        {
            weld::ComboBox& rComboBox = m_pTableCell->get_widget();
            rComboBox.clear();
            enableControl(pEntry, m_pTableCell);
            if ( !pEntry->isCondition() )
            {
                for (auto const& tabWin : getDesignView()->getTableView()->GetTabWinMap())
                    rComboBox.append_text(static_cast<OQueryTableWindow*>(tabWin.second.get())->GetAliasName());

                rComboBox.insert_text(0, DBA_RES(STR_QUERY_NOTABLE));
                if (!pEntry->GetAlias().isEmpty())
                    rComboBox.set_active_text(pEntry->GetAlias());
                else
                    rComboBox.set_active_text(DBA_RES(STR_QUERY_NOTABLE));
            }
        }   break;
        case BROW_VIS_ROW:
        {
            m_pVisibleCell->GetBox().set_active(pEntry->IsVisible());
            m_pVisibleCell->GetBox().save_state();

            enableControl(pEntry,m_pTextCell);

            if(!pEntry->IsVisible() && pEntry->GetOrderDir() != ORDER_NONE && !m_bOrderByUnRelated)
            {
               // a column has to visible in order to show up in ORDER BY
                pEntry->SetVisible();
                m_pVisibleCell->GetBox().set_active(pEntry->IsVisible());
                m_pVisibleCell->GetBox().save_state();
                m_pVisibleCell->GetBox().set_sensitive(false);
                OUString aMessage(DBA_RES(STR_QRY_ORDERBY_UNRELATED));
                OQueryDesignView* paDView = getDesignView();
                std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(paDView ? paDView->GetFrameWeld() : nullptr,
                                                              VclMessageType::Info, VclButtonsType::Ok,
                                                              aMessage));
                xInfoBox->run();
            }
        }   break;
        case BROW_ORDER_ROW:
        {
            weld::ComboBox& rComboBox = m_pOrderCell->get_widget();
            rComboBox.set_active(
                sal::static_int_cast< sal_uInt16 >(pEntry->GetOrderDir()));
            enableControl(pEntry,m_pOrderCell);
            break;
        }
        case BROW_COLUMNALIAS_ROW:
            setTextCellContext(pEntry,pEntry->GetFieldAlias(),HID_QRYDGN_ROW_ALIAS);
            break;
        case BROW_FUNCTION_ROW:
            setFunctionCell(pEntry);
            break;
        default:
        {
            sal_uInt16  nIdx = sal_uInt16(nCellIndex - BROW_CRIT1_ROW);
            setTextCellContext(pEntry,pEntry->GetCriteria( nIdx ),HID_QRYDGN_ROW_CRIT);
        }
    }
    Controller()->SaveValue();
}

void OSelectionBrowseBox::notifyTableFieldChanged(const OUString& _sOldAlias, std::u16string_view _sAlias, bool& _bListAction, sal_uInt16 _nColumnId)
{
    appendUndoAction(_sOldAlias,_sAlias,BROW_TABLE_ROW,_bListAction);
    if ( m_bVisibleRow[BROW_TABLE_ROW] )
        RowModified(GetBrowseRow(BROW_TABLE_ROW), _nColumnId);
}

void OSelectionBrowseBox::notifyFunctionFieldChanged(const OUString& _sOldFunctionName, std::u16string_view _sFunctionName, bool& _bListAction, sal_uInt16 _nColumnId)
{
    appendUndoAction(_sOldFunctionName,_sFunctionName,BROW_FUNCTION_ROW,_bListAction);
    if ( !m_bVisibleRow[BROW_FUNCTION_ROW] )
        SetRowVisible(BROW_FUNCTION_ROW, true);
    RowModified(GetBrowseRow(BROW_FUNCTION_ROW), _nColumnId);
}

void OSelectionBrowseBox::clearEntryFunctionField(std::u16string_view _sFieldName,OTableFieldDescRef const & _pEntry, bool& _bListAction,sal_uInt16 _nColumnId)
{
    if ( !(isFieldNameAsterisk( _sFieldName ) && (!_pEntry->isNoneFunction() || _pEntry->IsGroupBy())) )
        return;

    OUString sFunctionName;
    GetFunctionName(SQL_TOKEN_COUNT,sFunctionName);
    OUString sOldLocalizedFunctionName = _pEntry->GetFunction();
    if ( sOldLocalizedFunctionName != sFunctionName || _pEntry->IsGroupBy() )
    {
        // append undo action for the function field
        _pEntry->SetFunctionType(FKT_NONE);
        _pEntry->SetFunction(OUString());
        _pEntry->SetGroupBy(false);
        notifyFunctionFieldChanged(sOldLocalizedFunctionName,_pEntry->GetFunction(),_bListAction,_nColumnId);
    }
}

bool OSelectionBrowseBox::fillColumnRef(const OSQLParseNode* _pColumnRef, const Reference< XConnection >& _rxConnection, OTableFieldDescRef const & _pEntry, bool& _bListAction )
{
    OSL_ENSURE(_pColumnRef,"No valid parsenode!");
    OUString sColumnName,sTableRange;
    OSQLParseTreeIterator::getColumnRange(_pColumnRef,_rxConnection,sColumnName,sTableRange);
    return fillColumnRef(sColumnName,sTableRange,_rxConnection->getMetaData(),_pEntry,_bListAction);
}

bool OSelectionBrowseBox::fillColumnRef(const OUString& _sColumnName, std::u16string_view _sTableRange, const Reference<XDatabaseMetaData>& _xMetaData, OTableFieldDescRef const & _pEntry, bool& _bListAction)
{
    bool bError = false;
    ::comphelper::UStringMixEqual bCase(_xMetaData->supportsMixedCaseQuotedIdentifiers());
    // check if the table name is the same
    if ( !_sTableRange.empty() && (bCase(_pEntry->GetTable(),_sTableRange) || bCase(_pEntry->GetAlias(),_sTableRange)) )
    { // a table was already inserted and the tables contains that column name

        if ( !_pEntry->GetTabWindow() )
        { // fill tab window
            OUString sOldAlias = _pEntry->GetAlias();
            if ( !fillEntryTable(_pEntry,_pEntry->GetTable()) )
                fillEntryTable(_pEntry,_pEntry->GetAlias()); // only when the first failed
            if ( !bCase(sOldAlias,_pEntry->GetAlias()) )
                notifyTableFieldChanged(sOldAlias,_pEntry->GetAlias(),_bListAction,GetCurColumnId());
        }
    }
    // check if the table window
    OQueryTableWindow* pEntryTab = static_cast<OQueryTableWindow*>(_pEntry->GetTabWindow());
    if ( !pEntryTab ) // no table found with this name so we have to travel through all tables
    {
        sal_uInt16 nTabCount = 0;
        if ( !static_cast<OQueryTableView*>(getDesignView()->getTableView())->FindTableFromField(_sColumnName,_pEntry,nTabCount) ) // error occurred: column not in table window
        {
            OUString sErrorMsg(DBA_RES(RID_STR_FIELD_DOESNT_EXIST));
            sErrorMsg = sErrorMsg.replaceFirst("$name$",_sColumnName);
            OSQLErrorBox aWarning(GetFrameWeld(), sErrorMsg);
            aWarning.run();
            bError = true;
        }
        else
        {
            pEntryTab = static_cast<OQueryTableWindow*>(_pEntry->GetTabWindow());
            notifyTableFieldChanged(OUString(),_pEntry->GetAlias(),_bListAction,GetCurColumnId());
        }
    }
    if ( pEntryTab ) // here we got a valid table
        _pEntry->SetField(_sColumnName);

    return bError;
}

bool OSelectionBrowseBox::saveField(OUString& _sFieldName ,OTableFieldDescRef const & _pEntry, bool& _bListAction)
{
    bool bError = false;

    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());

    // first look if the name can be found in our tables
    sal_uInt16 nTabCount = 0;
    OUString sOldAlias = _pEntry->GetAlias();
    if ( static_cast<OQueryTableView*>(getDesignView()->getTableView())->FindTableFromField(_sFieldName,_pEntry,nTabCount) )
    {
        // append undo action for the alias name
        _pEntry->SetField(_sFieldName);
        notifyTableFieldChanged(sOldAlias,_pEntry->GetAlias(),_bListAction,GetCurColumnId());
        clearEntryFunctionField(_sFieldName,_pEntry,_bListAction,_pEntry->GetColumnId());
        return bError;
    }

    Reference<XConnection> xConnection( rController.getConnection() );
    Reference< XDatabaseMetaData > xMetaData;
    if ( xConnection.is() )
        xMetaData = xConnection->getMetaData();
    OSL_ENSURE( xMetaData.is(), "OSelectionBrowseBox::saveField: invalid connection/meta data!" );
    if ( !xMetaData.is() )
        return true;

    OUString sErrorMsg;
    // second test if the name can be set as select columns in a pseudo statement
    // we have to look which entries  we should quote

    const OUString sFieldAlias = _pEntry->GetFieldAlias();
    ::connectivity::OSQLParser& rParser( rController.getParser() );
    {
        // automatically add parentheses around subqueries
        OUString devnull;
        std::unique_ptr<OSQLParseNode> pParseNode = rParser.parseTree( devnull, _sFieldName, true );
        if (pParseNode == nullptr)
            pParseNode = rParser.parseTree( devnull, _sFieldName );
        if (pParseNode != nullptr && SQL_ISRULE(pParseNode, select_statement))
            _sFieldName = "(" + _sFieldName + ")";
    }

    std::unique_ptr<OSQLParseNode> pParseNode;
    {
        // 4 passes in trying to interpret the field name
        // - don't quote the field name, parse internationally
        // - don't quote the field name, parse en-US
        // - quote the field name, parse internationally
        // - quote the field name, parse en-US
        size_t nPass = 4;
        OUString sQuotedFullFieldName(::dbtools::quoteName( xMetaData->getIdentifierQuoteString(), _sFieldName ));
        OUString sFullFieldName(_sFieldName);

        if  ( _pEntry->isAggregateFunction() )
        {
            OSL_ENSURE(!_pEntry->GetFunction().isEmpty(),"No empty Function name allowed here! ;-(");
            sQuotedFullFieldName = _pEntry->GetFunction() + "(" + sQuotedFullFieldName + ")";
            sFullFieldName = _pEntry->GetFunction() + "(" + sFullFieldName + ")";
        }

        do
        {
            bool bQuote = ( nPass <= 2 );
            bool bInternational = ( nPass % 2 ) == 0;

            OUString sSql {u"SELECT "_ustr};
            if ( bQuote )
                sSql += sQuotedFullFieldName;
            else
                sSql += sFullFieldName;

            if ( !sFieldAlias.isEmpty() )
            { // always quote the alias name: there cannot be a function in it
                sSql += " " + ::dbtools::quoteName( xMetaData->getIdentifierQuoteString(), sFieldAlias );
            }
            sSql += " FROM x";

            pParseNode = rParser.parseTree( sErrorMsg, sSql, bInternational );
        }
        while ( ( pParseNode == nullptr ) && ( --nPass > 0 ) );
    }

    if ( pParseNode == nullptr )
    {
        // something different which we have to check
        OUString sErrorMessage( DBA_RES( STR_QRY_COLUMN_NOT_FOUND ) );
        sErrorMessage = sErrorMessage.replaceFirst("$name$",_sFieldName);
        OSQLErrorBox aWarning(GetFrameWeld(), sErrorMessage);
        aWarning.run();

        return true;
    }

    // we got a valid select column
    // find what type of column has be inserted
    ::connectivity::OSQLParseNode* pSelection = pParseNode->getChild(2);
    if ( SQL_ISRULE(pSelection,selection) ) // we found the asterisk
    {
        _pEntry->SetField(_sFieldName);
        clearEntryFunctionField(_sFieldName,_pEntry,_bListAction,_pEntry->GetColumnId());
    }
    else // travel through the select column parse node
    {
        OTableFieldDescRef aSelEntry = _pEntry;
        sal_uInt16 nColumnId = aSelEntry->GetColumnId();

        sal_uInt32 nCount = pSelection->count();
        for (sal_uInt32 i = 0; i < nCount; ++i)
        {
            if ( i > 0 ) // may we have to append more than one field
            {
                sal_uInt16 nColumnPosition;
                aSelEntry = FindFirstFreeCol(nColumnPosition);
                if ( !aSelEntry.is() )
                {
                    AppendNewCol();
                    aSelEntry = FindFirstFreeCol(nColumnPosition);
                }
                ++nColumnPosition;
                nColumnId = GetColumnId(nColumnPosition);
            }

            ::connectivity::OSQLParseNode* pChild = pSelection->getChild( i );
            OSL_ENSURE(SQL_ISRULE(pChild,derived_column), "No derived column found!");
            // get the column alias
            OUString sColumnAlias = OSQLParseTreeIterator::getColumnAlias(pChild);
            if ( !sColumnAlias.isEmpty() ) // we found an as clause
            {
                OUString aSelectionAlias = aSelEntry->GetFieldAlias();
                aSelEntry->SetFieldAlias( sColumnAlias );
                // append undo
                appendUndoAction(aSelectionAlias,aSelEntry->GetFieldAlias(),BROW_COLUMNALIAS_ROW,_bListAction);
                if ( m_bVisibleRow[BROW_COLUMNALIAS_ROW] )
                    RowModified(GetBrowseRow(BROW_COLUMNALIAS_ROW), nColumnId);
            }

            ::connectivity::OSQLParseNode* pColumnRef = pChild->getChild(0);
            if (
                    pColumnRef->getKnownRuleID() != OSQLParseNode::subquery &&
                    pColumnRef->count() == 3 &&
                    SQL_ISPUNCTUATION(pColumnRef->getChild(0),"(") &&
                    SQL_ISPUNCTUATION(pColumnRef->getChild(2),")")
                )
                pColumnRef = pColumnRef->getChild(1);

            if ( SQL_ISRULE(pColumnRef,column_ref) ) // we found a valid column name or more column names
            {
                // look if we can find the corresponding table
                bError = fillColumnRef( pColumnRef, xConnection, aSelEntry, _bListAction );

                // we found a simple column so we must clear the function fields but only when the column name is '*'
                // and the function is different to count
                clearEntryFunctionField(_sFieldName,aSelEntry,_bListAction,nColumnId);
            }
            // do we have an aggregate function and only a function?
            else if ( SQL_ISRULE(pColumnRef,general_set_fct) )
            {
                OUString sLocalizedFunctionName;
                if ( GetFunctionName(pColumnRef->getChild(0)->getTokenID(),sLocalizedFunctionName) )
                {
                    OUString sOldLocalizedFunctionName = aSelEntry->GetFunction();
                    aSelEntry->SetFunction(sLocalizedFunctionName);
                    sal_uInt32 nFunCount = pColumnRef->count() - 1;
                    sal_Int32 nFunctionType = FKT_AGGREGATE;
                    bool bQuote = false;
                    // may be there exists only one parameter which is a column, fill all information into our fields
                    if ( nFunCount == 4 && SQL_ISRULE(pColumnRef->getChild(3),column_ref) )
                        bError = fillColumnRef( pColumnRef->getChild(3), xConnection, aSelEntry, _bListAction );
                    else if ( nFunCount == 3 ) // we have a COUNT(*) here, so take the first table
                        bError = fillColumnRef( u"*"_ustr, std::u16string_view(), xMetaData, aSelEntry, _bListAction );
                    else
                    {
                        nFunctionType |= FKT_NUMERIC;
                        bQuote = true;
                        aSelEntry->SetDataType(DataType::DOUBLE);
                        aSelEntry->SetFieldType(TAB_NORMAL_FIELD);
                    }

                    // now parse the parameters
                    OUString sParameters;
                    for(sal_uInt32 function = 2; function < nFunCount; ++function) // we only want to parse the parameters of the function
                        pColumnRef->getChild(function)->parseNodeToStr( sParameters, xConnection, &rParser.getContext(), true, bQuote );

                    aSelEntry->SetFunctionType(nFunctionType);
                    aSelEntry->SetField(sParameters);
                    if ( aSelEntry->IsGroupBy() )
                    {
                        sOldLocalizedFunctionName = m_aFunctionStrings.copy(m_aFunctionStrings.lastIndexOf(';')+1);
                        aSelEntry->SetGroupBy(false);
                    }

                    // append undo action
                    notifyFunctionFieldChanged(sOldLocalizedFunctionName,sLocalizedFunctionName,_bListAction, nColumnId);
                }
                else
                    OSL_FAIL("Unsupported function inserted!");

            }
            else
            {
                // so we first clear the function field
                clearEntryFunctionField(_sFieldName,aSelEntry,_bListAction,nColumnId);
                OUString sFunction;
                pColumnRef->parseNodeToStr( sFunction,
                                            xConnection,
                                            &rController.getParser().getContext(),
                                            true); // quote is to true because we need quoted elements inside the function

                getDesignView()->fillFunctionInfo(pColumnRef,sFunction,aSelEntry);

                if( SQL_ISRULEOR3(pColumnRef, position_exp, extract_exp, fold) ||
                    SQL_ISRULEOR3(pColumnRef, char_substring_fct, length_exp, char_value_fct) )
                    // a calculation has been found ( can be calc and function )
                {
                    // now parse the whole statement
                    sal_uInt32 nFunCount = pColumnRef->count();
                    OUString sParameters;
                    for(sal_uInt32 function = 0; function < nFunCount; ++function)
                        pColumnRef->getChild(function)->parseNodeToStr( sParameters, xConnection, &rParser.getContext(), true );

                    sOldAlias = aSelEntry->GetAlias();
                    sal_Int32 nNewFunctionType = aSelEntry->GetFunctionType() | FKT_NUMERIC | FKT_OTHER;
                    aSelEntry->SetFunctionType(nNewFunctionType);
                    aSelEntry->SetField(sParameters);
                }
                else
                {
                    aSelEntry->SetFieldAlias(sColumnAlias);
                    if ( SQL_ISRULE(pColumnRef,set_fct_spec) )
                        aSelEntry->SetFunctionType(/*FKT_NUMERIC | */FKT_OTHER);
                    else
                        aSelEntry->SetFunctionType(FKT_NUMERIC | FKT_OTHER);
                }

                aSelEntry->SetAlias(OUString());
                notifyTableFieldChanged(sOldAlias,aSelEntry->GetAlias(),_bListAction, nColumnId);
            }

            if ( i > 0 && !InsertField(aSelEntry,BROWSER_INVALIDID,true,false).is() ) // may we have to append more than one field
            { // the field could not be inserted
                OUString sErrorMessage( DBA_RES( RID_STR_FIELD_DOESNT_EXIST ) );
                sErrorMessage = sErrorMessage.replaceFirst("$name$",aSelEntry->GetField());
                OSQLErrorBox aWarning(GetFrameWeld(), sErrorMessage);
                aWarning.run();
                bError = true;
            }
        }
    }

    return bError;
}

bool OSelectionBrowseBox::SaveModified()
{
    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());
    OTableFieldDescRef pEntry;
    sal_uInt16 nCurrentColumnPos = GetColumnPos(GetCurColumnId());
    if(getFields().size() > o3tl::make_unsigned(nCurrentColumnPos - 1))
        pEntry = getEntry(nCurrentColumnPos - 1);

    bool bWasEmpty      = pEntry.is() && pEntry->IsEmpty();
    bool bError         = false;
    bool bListAction    = false;

    if (pEntry.is() && Controller().is() && Controller()->IsValueChangedFromSaved())
    {
        // for the Undo-action
        OUString strOldCellContents,sNewValue;
        sal_Int32 nRow = GetRealRow(GetCurRow());
        bool bAppendRow = false;
        switch (nRow)
        {
            case BROW_VIS_ROW:
                {
                    bool bOldValue = m_pVisibleCell->GetBox().get_saved_state() != TRISTATE_FALSE;
                    strOldCellContents
                        = bOldValue ? std::u16string_view(u"1") : std::u16string_view(u"0");
                    sNewValue
                        = !bOldValue ? std::u16string_view(u"1") : std::u16string_view(u"0");
                }
                if((m_bOrderByUnRelated || pEntry->GetOrderDir() == ORDER_NONE) &&
                   (m_bGroupByUnRelated || !pEntry->IsGroupBy()))
                {
                    pEntry->SetVisible(m_pVisibleCell->GetBox().get_active());
                }
                else
                {
                    pEntry->SetVisible();
                    m_pVisibleCell->GetBox().set_active(true);
                }
                break;

            case BROW_FIELD_ROW:
            {
                weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
                OUString aFieldName(rComboBox.get_active_text());
                try
                {
                    if (aFieldName.isEmpty())
                    {
                        OTableFieldDescRef pNewEntry = new OTableFieldDesc();
                        pNewEntry->SetColumnId( pEntry->GetColumnId() );
                        std::replace(getFields().begin(),getFields().end(),pEntry,pNewEntry);
                        sal_uInt16 nCol = GetCurColumnId();
                        for (int i = 0; i < m_nVisibleCount; i++)   // redraw column
                            RowModified(i,nCol);
                    }
                    else
                    {
                        strOldCellContents = pEntry->GetField();
                        bListAction = true;
                        if ( !m_bInUndoMode )
                            rController.GetUndoManager().EnterListAction(OUString(),OUString(),0,ViewShellId(-1));

                        sal_Int32 nPos = rComboBox.find_text(aFieldName);
                        OUString aAliasName = pEntry->GetAlias();
                        if ( nPos != -1 && aAliasName.isEmpty() && aFieldName.indexOf('.') >= 0 )
                        { // special case, we have a table field so we must cut the table name
                            OUString sTableAlias = aFieldName.getToken(0,'.');
                            pEntry->SetAlias(sTableAlias);
                            OUString sColumnName = aFieldName.copy(sTableAlias.getLength()+1);
                            const Reference<XConnection>& xConnection = rController.getConnection();
                            if ( !xConnection.is() )
                                return false;
                            bError = fillColumnRef( sColumnName, sTableAlias, xConnection->getMetaData(), pEntry, bListAction );
                        }
                        else
                            bError = true;

                        if ( bError )
                            bError = saveField(aFieldName,pEntry,bListAction);
                    }
                }
                catch(Exception&)
                {
                    bError = true;
                }
                if ( bError )
                {
                    sNewValue = aFieldName;
                    if ( !m_bInUndoMode )
                        static_cast<OQueryController&>(getDesignView()->getController()).GetUndoManager().LeaveListAction();
                    bListAction = false;
                }
                else
                    sNewValue = pEntry->GetField();
                rController.InvalidateFeature( ID_BROWSER_QUERY_EXECUTE );
            }
            break;

            case BROW_TABLE_ROW:
            {
                weld::ComboBox& rComboBox = m_pTableCell->get_widget();
                OUString aAliasName = rComboBox.get_active_text();
                strOldCellContents = pEntry->GetAlias();
                if (rComboBox.get_active() != 0)
                {
                    pEntry->SetAlias(aAliasName);
                    // we have to set the table name as well as the table window
                    OJoinTableView::OTableWindowMap& rTabWinList = getDesignView()->getTableView()->GetTabWinMap();
                    OJoinTableView::OTableWindowMap::const_iterator aIter = rTabWinList.find(aAliasName);
                    if(aIter != rTabWinList.end())
                    {
                        OQueryTableWindow* pEntryTab = static_cast<OQueryTableWindow*>(aIter->second.get());
                        if (pEntryTab)
                        {
                            pEntry->SetTable(pEntryTab->GetTableName());
                            pEntry->SetTabWindow(pEntryTab);
                        }
                    }
                }
                else
                {
                    pEntry->SetAlias(OUString());
                    pEntry->SetTable(OUString());
                    pEntry->SetTabWindow(nullptr);
                }
                sNewValue = pEntry->GetAlias();

            }   break;

            case BROW_ORDER_ROW:
            {
                strOldCellContents = OUString::number(static_cast<sal_uInt16>(pEntry->GetOrderDir()));
                weld::ComboBox& rComboBox = m_pOrderCell->get_widget();
                sal_Int32 nIdx = rComboBox.get_active();
                if (nIdx == -1)
                    nIdx = 0;
                pEntry->SetOrderDir(EOrderDir(nIdx));
                if(!m_bOrderByUnRelated)
                {
                    pEntry->SetVisible();
                    m_pVisibleCell->GetBox().set_active(true);
                    RowModified(GetBrowseRow(BROW_VIS_ROW), GetCurColumnId());
                }
                sNewValue = OUString::number(static_cast<sal_uInt16>(pEntry->GetOrderDir()));
            }   break;

            case BROW_COLUMNALIAS_ROW:
                strOldCellContents = pEntry->GetFieldAlias();
                pEntry->SetFieldAlias(m_pTextCell->get_widget().get_text());
                sNewValue = pEntry->GetFieldAlias();
                break;
            case BROW_FUNCTION_ROW:
                {
                    strOldCellContents = pEntry->GetFunction();
                    weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
                    sal_Int32 nPos = rComboBox.get_active();
                    // these functions are only available in CORE
                    OUString sFunctionName        = rComboBox.get_text(nPos);
                    std::u16string_view sGroupFunctionName = m_aFunctionStrings.subView(m_aFunctionStrings.lastIndexOf(';')+1);
                    bool bGroupBy = false;
                    if ( sGroupFunctionName == sFunctionName ) // check if the function name is GROUP
                    {
                        bGroupBy = true;

                        if ( !m_bGroupByUnRelated && !pEntry->IsVisible() )
                        {
                            // we have to change the visible flag, so we must append also an undo action
                            pEntry->SetVisible();
                            m_pVisibleCell->GetBox().set_active(true);
                            appendUndoAction(u"0"_ustr,u"1",BROW_VIS_ROW,bListAction);
                            RowModified(GetBrowseRow(BROW_VIS_ROW), GetCurColumnId());
                        }

                        pEntry->SetFunction(OUString());
                        pEntry->SetFunctionType(pEntry->GetFunctionType() & ~FKT_AGGREGATE );
                    }
                    else if ( nPos ) // we found an aggregate function
                    {
                        pEntry->SetFunctionType(pEntry->GetFunctionType() | FKT_AGGREGATE );
                        pEntry->SetFunction(sFunctionName);
                    }
                    else
                    {
                        sFunctionName.clear();
                        pEntry->SetFunction(OUString());
                        pEntry->SetFunctionType(pEntry->GetFunctionType() & ~FKT_AGGREGATE );
                    }

                    pEntry->SetGroupBy(bGroupBy);

                    sNewValue = sFunctionName;
                }
                break;
            default:
            {
                Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
                if(!xConnection.is())
                    break;

                sal_uInt16  nIdx = sal_uInt16(nRow - BROW_CRIT1_ROW);
                OUString aText = comphelper::string::stripStart(m_pTextCell->get_widget().get_text(), ' ');

                OUString aCrit;
                if(!aText.isEmpty())
                {
                    OUString aErrorMsg;
                    Reference<XPropertySet> xColumn;
                    std::unique_ptr<OSQLParseNode> pParseNode = getDesignView()->getPredicateTreeFromEntry(pEntry,aText,aErrorMsg,xColumn);

                    if (pParseNode)
                    {
                        pParseNode->parseNodeToPredicateStr(aCrit,
                                                            xConnection,
                                                            static_cast<OQueryController&>(getDesignView()->getController()).getNumberFormatter(),
                                                            xColumn,
                                                            pEntry->GetAlias(),
                                                            getDesignView()->getLocale(),
                                                            getDesignView()->getDecimalSeparator(),
                                                            &(static_cast<OQueryController&>(getDesignView()->getController()).getParser().getContext()));
                    }
                    else
                    {
                        if(xColumn.is())
                        {
                            sal_Int32 nType = 0;
                            xColumn->getPropertyValue(PROPERTY_TYPE) >>= nType;
                            switch(nType)
                            {
                                case DataType::CHAR:
                                case DataType::VARCHAR:
                                case DataType::LONGVARCHAR:
                                case DataType::CLOB:
                                    if(!aText.startsWith("'") || !aText.endsWith("'"))
                                    {
                                        aText = aText.replaceAll("'", "''");
                                        aText = "'" + aText + "'";
                                    }
                                    break;
                                default:
                                    ;
                            }
                            ::connectivity::OSQLParser& rParser = static_cast<OQueryController&>(getDesignView()->getController()).getParser();
                            pParseNode = rParser.predicateTree(aErrorMsg,
                                                                aText,
                                                                static_cast<OQueryController&>(getDesignView()->getController()).getNumberFormatter(),
                                                                xColumn);
                            if (pParseNode)
                            {
                                pParseNode->parseNodeToPredicateStr(aCrit,
                                                                    xConnection,
                                                                    static_cast<OQueryController&>(getDesignView()->getController()).getNumberFormatter(),
                                                                    xColumn,
                                                                    pEntry->GetAlias(),
                                                                    getDesignView()->getLocale(),
                                                                    getDesignView()->getDecimalSeparator(),
                                                                    &(static_cast<OQueryController&>(getDesignView()->getController()).getParser().getContext()));
                            }
                            else
                            {
                                if ( !m_bDisableErrorBox )
                                {
                                    OSQLWarningBox aWarning(GetFrameWeld(), aErrorMsg);
                                    aWarning.run();
                                }
                                bError = true;
                            }
                        }
                        else
                        {
                            if ( !m_bDisableErrorBox )
                            {
                                OSQLWarningBox aWarning(GetFrameWeld(), aErrorMsg);
                                aWarning.run();
                            }
                            bError = true;
                        }
                    }
                }
                strOldCellContents = pEntry->GetCriteria(nIdx);
                pEntry->SetCriteria(nIdx, aCrit);
                sNewValue = pEntry->GetCriteria(nIdx);
                if(!aCrit.isEmpty() && nRow >= (GetRowCount()-1))
                    bAppendRow = true;
            }
        }
        if( !bError && Controller().is() )
            Controller()->SaveValue();

        RowModified(GetCurRow(), GetCurColumnId());

        if ( bAppendRow )
        {
            RowInserted( GetRowCount()-1 );
            m_bVisibleRow.push_back(true);
            ++m_nVisibleCount;
        }

        if(!bError)
        {
            // and now the undo-action for the total
            appendUndoAction(strOldCellContents,sNewValue,nRow);

        }
    }

    // did I store data in a FieldDescription which was empty before and which is not empty anymore after the changes?
    if ( pEntry.is() && bWasEmpty && !pEntry->IsEmpty() && !bError )
    {
        // Default to visible
        pEntry->SetVisible();
        appendUndoAction(u"0"_ustr,u"1",BROW_VIS_ROW,bListAction);
        RowModified(BROW_VIS_ROW, GetCurColumnId());

        // if required add empty columns
        sal_uInt16 nDummy;
        CheckFreeColumns(nDummy);
    }

    if ( bListAction && !m_bInUndoMode )
        static_cast<OQueryController&>(getDesignView()->getController()).GetUndoManager().LeaveListAction();

    return pEntry != nullptr && !bError;
}

bool OSelectionBrowseBox::SeekRow(sal_Int32 nRow)
{
    m_nSeekRow = nRow;
    return nRow < m_nVisibleCount;
}

void OSelectionBrowseBox::PaintCell(OutputDevice& rDev, const tools::Rectangle& rRect, sal_uInt16 nColumnId) const
{
    rDev.SetClipRegion(vcl::Region(rRect));

    OTableFieldDescRef pEntry;
    sal_uInt16 nPos = GetColumnPos(nColumnId);
    if(getFields().size() > o3tl::make_unsigned(nPos - 1))
        pEntry = getFields()[nPos - 1];

    if (!pEntry.is())
        return;

    sal_Int32 nRow = GetRealRow(m_nSeekRow);
    if (nRow == BROW_VIS_ROW)
        PaintTristate(rRect, pEntry->IsVisible() ? TRISTATE_TRUE : TRISTATE_FALSE);
    else
        rDev.DrawText(rRect, GetCellText(nRow, nColumnId),DrawTextFlags::VCenter);

    rDev.SetClipRegion( );
}

void OSelectionBrowseBox::PaintStatusCell(OutputDevice& rDev, const tools::Rectangle& rRect) const
{
    tools::Rectangle aRect(rRect);
    aRect.TopLeft().AdjustY( -2 );
    OUString  aLabel(DBA_RES(STR_QUERY_HANDLETEXT));

   // from BROW_CRIT2_ROW onwards all rows are shown "or"
    sal_Int32 nToken = (m_nSeekRow >= GetBrowseRow(BROW_CRIT2_ROW))
        ?  BROW_CRIT2_ROW : GetRealRow(m_nSeekRow);
    rDev.DrawText(aRect, aLabel.getToken(nToken, ';'),DrawTextFlags::VCenter);
}

void OSelectionBrowseBox::RemoveColumn(sal_uInt16 _nColumnId)
{
    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());

    sal_uInt16 nPos = GetColumnPos(_nColumnId);
    // the control should always have exactly one more column: the HandleColumn
    OSL_ENSURE((nPos == 0) || (nPos <= getFields().size()), "OSelectionBrowseBox::RemoveColumn : invalid parameter nColId");
    // ColId is synonymous to Position, and the condition should be valid

    sal_uInt16 nCurCol = GetCurColumnId();
    sal_Int32 nCurrentRow = GetCurRow();

    DeactivateCell();

    getFields().erase( getFields().begin() + (nPos - 1) );
    OTableFieldDescRef pEntry = new OTableFieldDesc();
    pEntry->SetColumnId(_nColumnId);
    getFields().push_back(pEntry);

    EditBrowseBox::RemoveColumn( _nColumnId );
    InsertDataColumn( _nColumnId , OUString(), DEFAULT_SIZE );

    // redraw
    tools::Rectangle aInvalidRect = GetInvalidRect( _nColumnId );
    Invalidate( aInvalidRect );

    ActivateCell( nCurrentRow, nCurCol );

    rController.setModified( true );

    invalidateUndoRedo();
}

void OSelectionBrowseBox::RemoveField(sal_uInt16 nColumnId )
{
    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());

    sal_uInt16 nPos = GetColumnPos(nColumnId);
    OSL_ENSURE(getFields().size() > o3tl::make_unsigned(nPos-1),"ID is to great!");

    OTableFieldDescRef pDesc = getEntry(static_cast<sal_uInt32>(nPos - 1)) ;
    pDesc->SetColWidth( static_cast<sal_uInt16>(GetColumnWidth(nColumnId)) );    // was not stored this before

    // trigger UndoAction
    if ( !m_bInUndoMode )
    {
        std::unique_ptr<OTabFieldDelUndoAct> pUndoAction(new OTabFieldDelUndoAct( this ));
        pUndoAction->SetTabFieldDescr(pDesc);
        pUndoAction->SetColumnPosition(nPos);
        rController.addUndoActionAndInvalidate( std::move(pUndoAction) );
    }

    RemoveColumn(nColumnId);

    invalidateUndoRedo();
}

void OSelectionBrowseBox::adjustSelectionMode( bool _bClickedOntoHeader, bool _bClickedOntoHandleCol )
{
    // if a Header has been selected it should be shown otherwise not
    if ( _bClickedOntoHeader )
    {
        if (0 == GetSelectColumnCount() )
            // I am in the correct mode if a selected column exists
            if ( BrowserMode::HIDESELECT == ( m_nMode & BrowserMode::HIDESELECT ) )
            {
                m_nMode &= ~BrowserMode::HIDESELECT;
                m_nMode |= BrowserMode::MULTISELECTION;
                SetMode( m_nMode );
            }
    }
    else if ( BrowserMode::HIDESELECT != ( m_nMode & BrowserMode::HIDESELECT ) )
    {
        if ( GetSelectColumnCount() != 0 )
            SetNoSelection();

        if ( _bClickedOntoHandleCol )
        {
            m_nMode |= BrowserMode::HIDESELECT;
            m_nMode &= ~BrowserMode::MULTISELECTION;
            SetMode( m_nMode );
        }
    }
}

void OSelectionBrowseBox::MouseButtonDown(const BrowserMouseEvent& rEvt)
{
    if( rEvt.IsLeft() )
    {
        bool bOnHandle = HANDLE_ID == rEvt.GetColumnId();
        bool bOnHeader = ( rEvt.GetRow() < 0 ) && !bOnHandle;
        adjustSelectionMode( bOnHeader, bOnHandle );
    }
    EditBrowseBox::MouseButtonDown(rEvt);
}

void OSelectionBrowseBox::MouseButtonUp(const BrowserMouseEvent& rEvt)
{
    EditBrowseBox::MouseButtonUp( rEvt );
    static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature( ID_BROWSER_QUERY_EXECUTE );
}

void OSelectionBrowseBox::KeyInput( const KeyEvent& rEvt )
{
    if (IsColumnSelected(GetCurColumnId()))
    {
        if (rEvt.GetKeyCode().GetCode() == KEY_DELETE &&    // Delete rows
            !rEvt.GetKeyCode().IsShift() &&
            !rEvt.GetKeyCode().IsMod1())
        {
            RemoveField(GetCurColumnId());
            return;
        }
    }
    EditBrowseBox::KeyInput(rEvt);
}

sal_Int8 OSelectionBrowseBox::AcceptDrop( const BrowserAcceptDropEvent& rEvt )
{
    sal_Int8 nDropAction = DND_ACTION_NONE;
    if  ( rEvt.GetRow() >= -1 )
    {
        if ( IsEditing() )
        {
            // allow the asterisk again
            m_bDisableErrorBox = true;
            SaveModified();
            m_bDisableErrorBox = false;
            DeactivateCell();
        }
        // check if the format is already supported, if not deactivate the current cell and try again
        if ( OJoinExchObj::isFormatAvailable(GetDataFlavors()) )
            nDropAction = DND_ACTION_LINK;
    }

    return nDropAction;
}

sal_Int8 OSelectionBrowseBox::ExecuteDrop( const BrowserExecuteDropEvent& _rEvt )
{

    TransferableDataHelper aDropped(_rEvt.maDropEvent.Transferable);
    if (!OJoinExchObj::isFormatAvailable(aDropped.GetDataFlavorExVector()))
    {
        OSL_FAIL("OSelectionBrowseBox::ExecuteDrop: this should never have passed AcceptDrop!");
        return DND_ACTION_NONE;
    }

    // insert the field at the selected position
    OJoinExchangeData jxdSource = OJoinExchObj::GetSourceDescription(_rEvt.maDropEvent.Transferable);
    InsertField(jxdSource);

    return DND_ACTION_LINK;
}

OTableFieldDescRef const & OSelectionBrowseBox::AppendNewCol( sal_uInt16 nCnt)
{
    // one or more can be created, but the first one will is not returned
    sal_uInt32 nCount = getFields().size();
    for (sal_uInt16 i=0 ; i<nCnt ; i++)
    {
        OTableFieldDescRef pEmptyEntry = new OTableFieldDesc();
        getFields().push_back(pEmptyEntry);
        sal_uInt16 nColumnId = sal::static_int_cast< sal_uInt16 >(getFields().size());
        pEmptyEntry->SetColumnId( nColumnId );

        InsertDataColumn( nColumnId , OUString(), DEFAULT_SIZE );
    }

    return getFields()[nCount];
}

void OSelectionBrowseBox::DeleteFields(const OUString& rAliasName)
{
    if (getFields().empty())
        return;

    sal_uInt16 nColId = GetCurColumnId();
    sal_uInt32 nRow = GetCurRow();

    bool bWasEditing = IsEditing();
    if (bWasEditing)
        DeactivateCell();

    auto aIter = std::find_if(getFields().rbegin(), getFields().rend(),
        [&rAliasName](const OTableFieldDescRef pEntry) { return pEntry->GetAlias() == rAliasName; });
    if (aIter != getFields().rend())
    {
        sal_uInt16 nPos = sal::static_int_cast<sal_uInt16>(std::distance(aIter, getFields().rend()));
        RemoveField( GetColumnId( nPos ) );
    }

    if (bWasEditing)
        ActivateCell(nRow , nColId);
}

void OSelectionBrowseBox::SetColWidth(sal_uInt16 nColId, tools::Long nNewWidth)
{
    bool bWasEditing = IsEditing();
    if (bWasEditing)
        DeactivateCell();

    // create the BaseClass
    SetColumnWidth(nColId, nNewWidth);

    // tell it the FieldDescription
    OTableFieldDescRef pEntry = getEntry(GetColumnPos(nColId) - 1);
    if (pEntry.is())
        pEntry->SetColWidth(sal_uInt16(GetColumnWidth(nColId)));

    if (bWasEditing)
        ActivateCell(GetCurRow(), GetCurColumnId());
}

tools::Rectangle OSelectionBrowseBox::GetInvalidRect( sal_uInt16 nColId )
{
    // The rectangle is the full output area of the window
    tools::Rectangle aInvalidRect( Point(0,0), GetOutputSizePixel() );

    // now update the left side
    tools::Rectangle aFieldRect(GetCellRect( 0, nColId )); // used instead of GetFieldRectPixel
    aInvalidRect.SetLeft( aFieldRect.Left() );

    return aInvalidRect;
}

void OSelectionBrowseBox::InsertColumn(const OTableFieldDescRef& pEntry, sal_uInt16& _nColumnPosition)
{
    // the control should have exactly one more column: the HandleColumn
    OSL_ENSURE(_nColumnPosition == BROWSER_INVALIDID || (_nColumnPosition <= static_cast<tools::Long>(getFields().size())), "OSelectionBrowseBox::InsertColumn : invalid parameter nColId.");
     // -1 means at the end. Count means at the end, others denotes a correct position

    sal_uInt16 nCurCol = GetCurColumnId();
    sal_Int32 nCurrentRow = GetCurRow();

    DeactivateCell();

    // remember the column id of the current position
    sal_uInt16 nColumnId = GetColumnId(_nColumnPosition);
    // put at the end of the list if too small or too big,
    if ((_nColumnPosition == BROWSER_INVALIDID) || (_nColumnPosition >= getFields().size()))   // append the field
    {
        if (FindFirstFreeCol(_nColumnPosition) == nullptr)  // no more free columns
        {
            AppendNewCol();
            _nColumnPosition = sal::static_int_cast< sal_uInt16 >(
                getFields().size());
        }
        else
        {
            _nColumnPosition = static_cast<sal_uInt16>(_nColumnPosition + 1); // within the list
        }
        nColumnId = GetColumnId(_nColumnPosition);
        pEntry->SetColumnId( nColumnId );
        getFields()[ _nColumnPosition - 1] = pEntry;
    }

    // check if the column ids are identical, if not we have to move
    if ( pEntry->GetColumnId() != nColumnId )
    {
        sal_uInt16 nOldPosition = GetColumnPos(pEntry->GetColumnId());
        OSL_ENSURE( nOldPosition != 0,"Old position was 0. Not possible!");
        SetColumnPos(pEntry->GetColumnId(),_nColumnPosition);
        // we have to delete an empty field for the fields list, because the columns must have equal length
        if ( nOldPosition > 0 && nOldPosition <= getFields().size() )
            getFields()[nOldPosition - 1] = pEntry;

        ColumnMoved(pEntry->GetColumnId(),false);
    }

    if ( pEntry->GetFunctionType() & FKT_AGGREGATE )
    {
        OUString sFunctionName = pEntry->GetFunction();
        if ( GetFunctionName(sal_uInt32(-1),sFunctionName) )
            pEntry->SetFunction(sFunctionName);
    }

    nColumnId = pEntry->GetColumnId();

    SetColWidth(nColumnId,getDesignView()->getColWidth(GetColumnPos(nColumnId)-1));
    // redraw
    tools::Rectangle aInvalidRect = GetInvalidRect( nColumnId );
    Invalidate( aInvalidRect );

    ActivateCell( nCurrentRow, nCurCol );
    static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );

    invalidateUndoRedo();
}

OTableFieldDescRef OSelectionBrowseBox::InsertField(const OJoinExchangeData& jxdSource)
{
    OQueryTableWindow* pSourceWin = static_cast<OQueryTableWindow*>(jxdSource.pListBox->GetTabWin());
    if (!pSourceWin)
        return nullptr;

    // name and position of the selected field
    weld::TreeView& rTreeView = jxdSource.pListBox->get_widget();
    OUString aFieldName = rTreeView.get_text(jxdSource.nEntry);
    sal_uInt32 nFieldIndex = jxdSource.nEntry;
    OTableFieldInfo* pInf = weld::fromId<OTableFieldInfo*>(rTreeView.get_id(jxdSource.nEntry));

    // construct DragInfo, such that I use the other InsertField
    OTableFieldDescRef aInfo = new OTableFieldDesc(pSourceWin->GetTableName(),aFieldName);
    aInfo->SetTabWindow(pSourceWin);
    aInfo->SetFieldIndex(nFieldIndex);
    aInfo->SetFieldType(pInf->GetKeyType());
    aInfo->SetAlias(pSourceWin->GetAliasName());

    aInfo->SetDataType(pInf->GetDataType());
    aInfo->SetVisible();

    return InsertField(aInfo);
}

OTableFieldDescRef OSelectionBrowseBox::InsertField(const OTableFieldDescRef& _rInfo, sal_uInt16 _nColumnPosition, bool bVis, bool bActivate)
{

    if(m_nMaxColumns && m_nMaxColumns <= FieldsCount())
        return nullptr;
    if (bActivate)
        SaveModified();

    // new column description
    OTableFieldDescRef pEntry = _rInfo;
    pEntry->SetVisible(bVis);

    // insert column
    InsertColumn( pEntry, _nColumnPosition );

    if ( !m_bInUndoMode )
    {
        // trigger UndoAction
        std::unique_ptr<OTabFieldCreateUndoAct> pUndoAction(new OTabFieldCreateUndoAct( this ));
        pUndoAction->SetTabFieldDescr( pEntry );
        pUndoAction->SetColumnPosition(_nColumnPosition);
        getDesignView()->getController().addUndoActionAndInvalidate( std::move(pUndoAction) );
    }

    return pEntry;
}

sal_uInt16 OSelectionBrowseBox::FieldsCount()
{
    sal_uInt16 nCount = 0;
    for (auto const& field : getFields())
    {
        if (field.is() && !field->IsEmpty())
            ++nCount;
    }

    return nCount;
}

OTableFieldDescRef OSelectionBrowseBox::FindFirstFreeCol(sal_uInt16& _rColumnPosition )
{

    _rColumnPosition = BROWSER_INVALIDID;

    for (auto const& field : getFields())
    {
        _rColumnPosition = static_cast<sal_uInt16>(_rColumnPosition + 1);
        OTableFieldDescRef pEntry = field;
        if ( pEntry.is() && pEntry->IsEmpty() )
            return pEntry;
    }

    return nullptr;
}

void OSelectionBrowseBox::CheckFreeColumns(sal_uInt16& _rColumnPosition)
{
    if (FindFirstFreeCol(_rColumnPosition) == nullptr)
    {
        // it is full, so append a pack of columns
        AppendNewCol(DEFAULT_QUERY_COLS);
        OSL_VERIFY(FindFirstFreeCol(_rColumnPosition).is());
    }
}

void OSelectionBrowseBox::AddGroupBy( const OTableFieldDescRef& rInfo )
{
    Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
    if(!xConnection.is())
        return;
    OSL_ENSURE(!rInfo->IsEmpty(),"AddGroupBy:: OTableFieldDescRef should not be empty!");
    OTableFieldDescRef pEntry;
    const Reference<XDatabaseMetaData> xMeta = xConnection->getMetaData();
    const ::comphelper::UStringMixEqual bCase(xMeta.is() && xMeta->supportsMixedCaseQuotedIdentifiers());
    //sal_Bool bAppend = sal_False;

    bool bAllFieldsSearched = true;
    for (auto const& field : getFields())
    {
        pEntry = field;
        OSL_ENSURE(pEntry.is(),"OTableFieldDescRef was null!");

        const OUString   aField = pEntry->GetField();
        const OUString   aAlias = pEntry->GetAlias();

        if (bCase(aField,rInfo->GetField()) &&
            bCase(aAlias,rInfo->GetAlias()) &&
            pEntry->GetFunctionType() == rInfo->GetFunctionType() &&
            pEntry->GetFunction() == rInfo->GetFunction())
        {
            if ( pEntry->isNumericOrAggregateFunction() && rInfo->IsGroupBy() )
            {
                pEntry->SetGroupBy(false);
                // we do want to consider that bAllFieldsSearched still true here
                // bAllFieldsSearched = false;
                break;
            }
            else
            {
                if ( !pEntry->IsGroupBy() && !pEntry->HasCriteria() ) // here we have a where condition which is no having clause
                {
                    pEntry->SetGroupBy(rInfo->IsGroupBy());
                    if(!m_bGroupByUnRelated && pEntry->IsGroupBy())
                        pEntry->SetVisible();
                    bAllFieldsSearched = false;
                    break;
                }
            }

        }
    }

    if (bAllFieldsSearched)
    {
        OTableFieldDescRef pTmp = InsertField(rInfo, BROWSER_INVALIDID, false, false );
        if ( pTmp->isNumericOrAggregateFunction() && rInfo->IsGroupBy() ) // the GroupBy is inherited from rInfo
            pTmp->SetGroupBy(false);
    }
}

void OSelectionBrowseBox::DuplicateConditionLevel( const sal_uInt16 nLevel)
{
    const sal_uInt16 nNewLevel = nLevel +1;
    for (auto const& field : getFields())
    {
        const OTableFieldDescRef& pEntry = field;
        OUString sValue = pEntry->GetCriteria(nLevel);
        if ( !sValue.isEmpty() )
        {
            pEntry->SetCriteria( nNewLevel, sValue);
            if ( nNewLevel == (m_nVisibleCount-BROW_CRIT1_ROW-1) )
            {
                RowInserted( GetRowCount()-1 );
                m_bVisibleRow.push_back(true);
                ++m_nVisibleCount;
            }
            m_bVisibleRow[BROW_CRIT1_ROW + nNewLevel] = true;
        }
    }
}

void OSelectionBrowseBox::AddCondition( const OTableFieldDescRef& rInfo, const OUString& rValue, const sal_uInt16 nLevel,bool _bAddOrOnOneLine )
{
    Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
    if(!xConnection.is())
        return;
    OSL_ENSURE(rInfo.is() && !rInfo->IsEmpty(),"AddCondition:: OTableFieldDescRef should not be Empty!");

    OTableFieldDescRef pLastEntry;
    Reference<XDatabaseMetaData> xMeta = xConnection->getMetaData();
    ::comphelper::UStringMixEqual bCase(xMeta.is() && xMeta->supportsMixedCaseQuotedIdentifiers());

    bool bAllFieldsSearched = true;
    for (auto const& field : getFields())
    {
        const OTableFieldDescRef& pEntry = field;
        const OUString   aField = pEntry->GetField();
        const OUString   aAlias = pEntry->GetAlias();

        if (bCase(aField,rInfo->GetField()) &&
            bCase(aAlias,rInfo->GetAlias()) &&
            pEntry->GetFunctionType() == rInfo->GetFunctionType() &&
            pEntry->GetFunction() == rInfo->GetFunction() &&
            pEntry->IsGroupBy() == rInfo->IsGroupBy() )
        {
            if ( pEntry->isNumericOrAggregateFunction() && rInfo->IsGroupBy() )
                pEntry->SetGroupBy(false);
            else
            {
                if(!m_bGroupByUnRelated && pEntry->IsGroupBy())
                    pEntry->SetVisible();
            }
            if (pEntry->GetCriteria(nLevel).isEmpty() )
            {
                pEntry->SetCriteria( nLevel, rValue);
                if(nLevel == (m_nVisibleCount-BROW_CRIT1_ROW-1))
                {
                    RowInserted( GetRowCount()-1 );
                    m_bVisibleRow.push_back(true);
                    ++m_nVisibleCount;
                }
                m_bVisibleRow[BROW_CRIT1_ROW + nLevel] = true;
                bAllFieldsSearched = false;
                break;
            }
            if ( _bAddOrOnOneLine )
            {
                pLastEntry = pEntry;
            }
        }
    }
    if ( pLastEntry.is() )
    {
        OUString sCriteria = rValue;
        OUString sOldCriteria = pLastEntry->GetCriteria( nLevel );
        if ( !sOldCriteria.isEmpty() )
        {
            sCriteria = "( " + sOldCriteria + " OR " + rValue + " )";
        }
        pLastEntry->SetCriteria( nLevel, sCriteria);
        if(nLevel == (m_nVisibleCount-BROW_CRIT1_ROW-1))
        {
            RowInserted( GetRowCount()-1 );
            m_bVisibleRow.push_back(true);
            ++m_nVisibleCount;
        }
        m_bVisibleRow[BROW_CRIT1_ROW + nLevel] = true;
    }
    else if (bAllFieldsSearched)
    {
        OTableFieldDescRef pTmp = InsertField(rInfo, BROWSER_INVALIDID, false, false );
        if ( pTmp->isNumericOrAggregateFunction() && rInfo->IsGroupBy() ) // the GroupBy was inherited from rInfo
            pTmp->SetGroupBy(false);
        if ( pTmp.is() )
        {
            pTmp->SetCriteria( nLevel, rValue);
            if(nLevel == (m_nVisibleCount-BROW_CRIT1_ROW-1))
            {
                RowInserted( GetRowCount()-1 );
                m_bVisibleRow.push_back(true);
                ++m_nVisibleCount;
            }
        }
    }
}

void OSelectionBrowseBox::AddOrder( const OTableFieldDescRef& rInfo, const EOrderDir eDir, sal_uInt32 _nCurrentPos)
{
    if (_nCurrentPos == 0)
        m_nLastSortColumn = SORT_COLUMN_NONE;

    Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
    if(!xConnection.is())
        return;
    OSL_ENSURE(!rInfo->IsEmpty(),"AddOrder:: OTableFieldDescRef should not be Empty!");
    OTableFieldDescRef pEntry;
    Reference<XDatabaseMetaData> xMeta = xConnection->getMetaData();
    ::comphelper::UStringMixEqual bCase(xMeta.is() && xMeta->supportsMixedCaseQuotedIdentifiers());

    bool bAppend = false;
    sal_uInt32 nPos = 0;
    bool bAllFieldsSearched = true;
    for (auto const& field : getFields())
    {
        pEntry = field;
        OUString aField = pEntry->GetField();
        OUString aAlias = pEntry->GetAlias();

        if (bCase(aField,rInfo->GetField()) &&
            bCase(aAlias,rInfo->GetAlias()))
        {
            bAppend = (m_nLastSortColumn != SORT_COLUMN_NONE) && (nPos <= m_nLastSortColumn);
            if ( bAppend )
            {
                // we do want to consider that bAllFieldsSearched still true here
                // bAllFieldsSearched = false;
                break;
            }
            else
            {
                if ( !m_bOrderByUnRelated )
                    pEntry->SetVisible();
                pEntry->SetOrderDir( eDir );
                m_nLastSortColumn = nPos;
            }
            bAllFieldsSearched = false;
            break;
        }
        ++nPos;
    }

    if (bAllFieldsSearched)
    {
        OTableFieldDescRef pTmp = InsertField(rInfo, BROWSER_INVALIDID, false, false );
        if(pTmp.is())
        {
            m_nLastSortColumn = pTmp->GetColumnId() - 1;
            if ( !m_bOrderByUnRelated && !bAppend )
                pTmp->SetVisible();
            pTmp->SetOrderDir( eDir );
        }
    }
}

bool OSelectionBrowseBox::Save()
{
    bool bRet = true;
    if (IsModified())
        bRet = SaveModified();
    return bRet;
}

void OSelectionBrowseBox::CellModified()
{
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_VIS_ROW:
            {
                OTableFieldDescRef  pEntry = getEntry(GetColumnPos(GetCurColumnId()) - 1);

                weld::ComboBox& rComboBox = m_pOrderCell->get_widget();
                sal_Int32 nIdx = rComboBox.get_active();
                if(!m_bOrderByUnRelated && nIdx > 0 &&
                    nIdx != -1 &&
                    !pEntry->IsEmpty()              &&
                    pEntry->GetOrderDir() != ORDER_NONE)
                {
                    m_pVisibleCell->GetBox().set_active(true);
                    pEntry->SetVisible();
                }
                else
                    pEntry->SetVisible(m_pVisibleCell->GetBox().get_active());
            }
            break;
    }
    static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );
}

void OSelectionBrowseBox::Fill()
{
    OSL_ENSURE(ColCount() >= 1, "OSelectionBrowseBox::Fill : please call only after inserting the handle column !");

    sal_uInt16 nColCount = ColCount() - 1;
    if (nColCount < DEFAULT_QUERY_COLS)
        AppendNewCol(DEFAULT_QUERY_COLS - nColCount);
}

Size OSelectionBrowseBox::CalcOptimalSize( const Size& _rAvailable )
{
    Size aReturn( _rAvailable.Width(), GetTitleHeight() );

    aReturn.AdjustHeight(( m_nVisibleCount ? m_nVisibleCount : 15 ) * GetDataRowHeight() );
    aReturn.AdjustHeight(40 ); // just some space

    return aReturn;
}

void OSelectionBrowseBox::Command(const CommandEvent& rEvt)
{
    switch (rEvt.GetCommand())
    {
        case CommandEventId::ContextMenu:
        {
            Point aMenuPos( rEvt.GetMousePosPixel() );

            if (!rEvt.IsMouseEvent())
            {
                if  ( 1 == GetSelectColumnCount() )
                {
                    sal_uInt16 nSelId = GetColumnId(
                        sal::static_int_cast< sal_uInt16 >(
                            FirstSelectedColumn() ) );
                    ::tools::Rectangle aColRect( GetFieldRectPixel( 0, nSelId, false ) );

                    aMenuPos = aColRect.TopCenter();
                }
                else
                {
                    EditBrowseBox::Command(rEvt);
                    return;
                }
            }

            sal_uInt16 nColId = GetColumnId(GetColumnAtXPosPixel( aMenuPos.X() ));
            sal_Int32   nRow = GetRowAtYPosPixel( aMenuPos.Y() );

            if (nRow < 0 && nColId > HANDLE_ID )
            {
                if ( !IsColumnSelected( nColId ) )
                {
                    adjustSelectionMode( true /* clicked onto a header */ , false /* not onto the handle col */ );
                    SelectColumnId( nColId );
                }

                if (!static_cast<OQueryController&>(getDesignView()->getController()).isReadOnly())
                {
                    ::tools::Rectangle aRect(aMenuPos, Size(1, 1));
                    weld::Window* pPopupParent = weld::GetPopupParent(*this, aRect);
                    std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(pPopupParent, u"dbaccess/ui/querycolmenu.ui"_ustr));
                    std::unique_ptr<weld::Menu> xContextMenu(xBuilder->weld_menu(u"menu"_ustr));
                    OUString sIdent = xContextMenu->popup_at_rect(pPopupParent, aRect);
                    if (sIdent == "delete")
                       RemoveField(nColId);
                    else if (sIdent == "width")
                        adjustBrowseBoxColumnWidth( this, nColId );
                }
            }
            else if(nRow >= 0 && nColId <= HANDLE_ID)
            {
                if (!static_cast<OQueryController&>(getDesignView()->getController()).isReadOnly())
                {
                    ::tools::Rectangle aRect(aMenuPos, Size(1, 1));
                    weld::Window* pPopupParent = weld::GetPopupParent(*this, aRect);
                    std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(pPopupParent, u"dbaccess/ui/queryfuncmenu.ui"_ustr));
                    std::unique_ptr<weld::Menu> xContextMenu(xBuilder->weld_menu(u"menu"_ustr));
                    xContextMenu->set_active(u"functions"_ustr, m_bVisibleRow[BROW_FUNCTION_ROW]);
                    xContextMenu->set_active(u"tablename"_ustr, m_bVisibleRow[BROW_TABLE_ROW]);
                    xContextMenu->set_active(u"alias"_ustr, m_bVisibleRow[BROW_COLUMNALIAS_ROW]);
                    xContextMenu->set_active(u"distinct"_ustr, static_cast<OQueryController&>(getDesignView()->getController()).isDistinct());

                    OUString sIdent = xContextMenu->popup_at_rect(pPopupParent, aRect);
                    if (sIdent == "functions")
                    {
                        SetRowVisible(BROW_FUNCTION_ROW, !IsRowVisible(BROW_FUNCTION_ROW));
                        static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature( SID_QUERY_VIEW_FUNCTIONS );
                    }
                    else if (sIdent == "tablename")
                    {
                        SetRowVisible(BROW_TABLE_ROW, !IsRowVisible(BROW_TABLE_ROW));
                        static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature( SID_QUERY_VIEW_TABLES );
                    }
                    else if (sIdent == "alias")
                    {
                        SetRowVisible(BROW_COLUMNALIAS_ROW, !IsRowVisible(BROW_COLUMNALIAS_ROW));
                        static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature( SID_QUERY_VIEW_ALIASES );
                    }
                    else if (sIdent == "distinct")
                    {
                        static_cast<OQueryController&>(getDesignView()->getController()).setDistinct(!static_cast<OQueryController&>(getDesignView()->getController()).isDistinct());
                        static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );
                        static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature( SID_QUERY_DISTINCT_VALUES );
                    }

                    static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );
                }
            }
            else
            {
                EditBrowseBox::Command(rEvt);
                return;
            }

            [[fallthrough]];
        }
        default:
            EditBrowseBox::Command(rEvt);
    }
}

bool OSelectionBrowseBox::IsRowVisible(sal_uInt16 _nWhich) const
{
    OSL_ENSURE(_nWhich<(m_bVisibleRow.size()), "OSelectionBrowseBox::IsRowVisible : invalid parameter !");
    return m_bVisibleRow[_nWhich];
}

void OSelectionBrowseBox::SetRowVisible(sal_uInt16 _nWhich, bool _bVis)
{
    OSL_ENSURE(_nWhich<m_bVisibleRow.size(), "OSelectionBrowseBox::SetRowVisible : invalid parameter !");

    bool bWasEditing = IsEditing();
    if (bWasEditing)
        DeactivateCell();

    // do this before removing or inserting rows, as this triggers ActivateCell-calls, which rely on m_bVisibleRow
    m_bVisibleRow[_nWhich] = !m_bVisibleRow[_nWhich];

    tools::Long nId = GetBrowseRow(_nWhich);
    if (_bVis)
    {
        RowInserted(nId);
        ++m_nVisibleCount;
    }
    else
    {
        RowRemoved(nId);
        --m_nVisibleCount;
    }

    if (bWasEditing)
        ActivateCell();
}

sal_Int32 OSelectionBrowseBox::GetBrowseRow(sal_Int32 nRowId) const
{
    sal_Int32 nCount(0);
    for(sal_Int32 i = 0 ; i < nRowId ; ++i)
    {
        if ( m_bVisibleRow[i] )
            ++nCount;
    }
    return nCount;
}

sal_Int32 OSelectionBrowseBox::GetRealRow(sal_Int32 nRowId) const
{
    sal_Int32 nErg=0,i;
    const sal_Int32 nCount = m_bVisibleRow.size();
    for(i=0;i < nCount; ++i)
    {
        if(m_bVisibleRow[i] && nErg++ == nRowId)
            break;
    }
    OSL_ENSURE(nErg <= tools::Long(m_bVisibleRow.size()),"nErg cannot be greater than BROW_ROW_CNT!");
    return i;
}

const tools::Long nVisibleRowMask[] =
                    {
                            0x0001,
                            0x0002,
                            0x0004,
                            0x0008,
                            0x0010,
                            0x0020,
                            0x0040,
                            0x0080,
                            0x0100,
                            0x0200,
                            0x0400,
                            0x0800
                    };
sal_Int32 OSelectionBrowseBox::GetNoneVisibleRows() const
{
    sal_Int32 nErg(0);
    // only the first 11 rows are interesting
    std::size_t const nSize = std::size(nVisibleRowMask);
    for(std::size_t i=0;i<nSize;i++)
    {
        if(!m_bVisibleRow[i])
            nErg |= nVisibleRowMask[i];
    }
    return nErg;
}

void OSelectionBrowseBox::SetNoneVisibleRow(sal_Int32 nRows)
{
    // only the first 11 rows are interesting
    std::size_t const nSize = std::size(nVisibleRowMask);
    for(std::size_t i=0;i< nSize;i++)
        m_bVisibleRow[i] = !(nRows & nVisibleRowMask[i]);
}

OUString OSelectionBrowseBox::GetCellText(sal_Int32 nRow, sal_uInt16 nColId) const
{

    sal_uInt16 nPos = GetColumnPos(nColId);
    if ( nPos == 0 || nPos == BROWSER_INVALIDID || nPos > getFields().size() )
        return OUString();

    OTableFieldDescRef pEntry = getFields()[nPos-1];
    OSL_ENSURE(pEntry != nullptr, "OSelectionBrowseBox::GetCellText : invalid column id, prepare for GPF ... ");
    if ( pEntry->IsEmpty() )
        return OUString();

    OUString aText;
    switch (nRow)
    {
        case BROW_TABLE_ROW:
            aText = pEntry->GetAlias();
            break;
        case BROW_FIELD_ROW:
        {
            OUString aField = pEntry->GetField();
            if (!aField.isEmpty() && aField[0] == '*')                   // * replace with alias.*
            {
                aField = pEntry->GetAlias();
                if(!aField.isEmpty())
                    aField += ".";
                aField += "*";
            }
            aText = aField;
        }   break;
        case BROW_ORDER_ROW:
            if (pEntry->GetOrderDir() != ORDER_NONE)
                aText = DBA_RES(STR_QUERY_SORTTEXT).getToken(sal::static_int_cast< sal_uInt16 >(pEntry->GetOrderDir()), ';');
            break;
        case BROW_VIS_ROW:
            break;
        case BROW_COLUMNALIAS_ROW:
            aText = pEntry->GetFieldAlias();
            break;
        case BROW_FUNCTION_ROW:
            // we always show the group function at first
            if ( pEntry->IsGroupBy() )
                aText = m_aFunctionStrings.copy(m_aFunctionStrings.lastIndexOf(';')+1);
            else if ( pEntry->isNumericOrAggregateFunction() )
                aText = pEntry->GetFunction();
            break;
        default:
            aText = pEntry->GetCriteria(sal_uInt16(nRow - BROW_CRIT1_ROW));
    }
    return aText;
}

bool OSelectionBrowseBox::GetFunctionName(sal_uInt32 _nFunctionTokenId, OUString& rFkt)
{
    weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
    switch(_nFunctionTokenId)
    {
        case SQL_TOKEN_COUNT:
            rFkt = (rComboBox.get_count() < 3) ? rComboBox.get_text(1) : rComboBox.get_text(2);
            break;
        case SQL_TOKEN_AVG:
            rFkt = rComboBox.get_text(1);
            break;
        case SQL_TOKEN_MAX:
            rFkt = rComboBox.get_text(3);
            break;
        case SQL_TOKEN_MIN:
            rFkt = rComboBox.get_text(4);
            break;
        case SQL_TOKEN_SUM:
            rFkt = rComboBox.get_text(5);
            break;
        case SQL_TOKEN_EVERY:
            rFkt = rComboBox.get_text(6);
            break;
        case SQL_TOKEN_ANY:
            rFkt = rComboBox.get_text(7);
            break;
        case SQL_TOKEN_SOME:
            rFkt = rComboBox.get_text(8);
            break;
        case SQL_TOKEN_STDDEV_POP:
            rFkt = rComboBox.get_text(9);
            break;
        case SQL_TOKEN_STDDEV_SAMP:
            rFkt = rComboBox.get_text(10);
            break;
        case SQL_TOKEN_VAR_SAMP:
            rFkt = rComboBox.get_text(11);
            break;
        case SQL_TOKEN_VAR_POP:
            rFkt = rComboBox.get_text(12);
            break;
        case SQL_TOKEN_COLLECT:
            rFkt = rComboBox.get_text(13);
            break;
        case SQL_TOKEN_FUSION:
            rFkt = rComboBox.get_text(14);
            break;
        case SQL_TOKEN_INTERSECTION:
            rFkt = rComboBox.get_text(15);
            break;
        default:
            {
                const sal_Int32 nStopIdx = m_aFunctionStrings.lastIndexOf(';'); // grouping is not counted
                for (sal_Int32 nIdx {0}; nIdx<nStopIdx;)
                {
                    const OUString sFunc {m_aFunctionStrings.getToken(0, ';', nIdx)};
                    if (rFkt.equalsIgnoreAsciiCase(sFunc))
                    {
                        rFkt = sFunc;
                        return true;
                    }
                }
                return false;
            }
    }

    return true;
}

OUString OSelectionBrowseBox::GetCellContents(sal_Int32 nCellIndex, sal_uInt16 nColId)
{
    if ( GetCurColumnId() == nColId && !m_bInUndoMode )
        SaveModified();

    sal_uInt16 nPos = GetColumnPos(nColId);
    OTableFieldDescRef pEntry = getFields()[nPos - 1];
    OSL_ENSURE(pEntry != nullptr, "OSelectionBrowseBox::GetCellContents : invalid column id, prepare for GPF ... ");

    switch (nCellIndex)
    {
        case BROW_VIS_ROW :
            return OUString(pEntry->IsVisible() ? std::u16string_view(u"1") : std::u16string_view(u"0"));
        case BROW_ORDER_ROW:
        {
            sal_Int32 nIdx = m_pOrderCell->get_widget().get_active();
            if (nIdx == -1)
                nIdx = 0;
            return OUString::number(nIdx);
        }
        default:
            return GetCellText(nCellIndex, nColId);
    }
}

void OSelectionBrowseBox::SetCellContents(sal_Int32 nRow, sal_uInt16 nColId, const OUString& strNewText)
{
    bool bWasEditing = IsEditing() && (GetCurColumnId() == nColId) && IsRowVisible(static_cast<sal_uInt16>(nRow)) && (GetCurRow() == static_cast<sal_uInt16>(GetBrowseRow(nRow)));
    if (bWasEditing)
        DeactivateCell();

    sal_uInt16 nPos = GetColumnPos(nColId);
    OTableFieldDescRef pEntry = getEntry(nPos - 1);
    OSL_ENSURE(pEntry != nullptr, "OSelectionBrowseBox::SetCellContents : invalid column id, prepare for GPF ... ");

    switch (nRow)
    {
        case BROW_VIS_ROW:
            pEntry->SetVisible(strNewText == "1");
            break;
        case BROW_FIELD_ROW:
            pEntry->SetField(strNewText);
            break;
        case BROW_TABLE_ROW:
            pEntry->SetAlias(strNewText);
            break;
        case BROW_ORDER_ROW:
        {
            sal_uInt16 nIdx = static_cast<sal_uInt16>(strNewText.toInt32());
            pEntry->SetOrderDir(EOrderDir(nIdx));
        }   break;
        case BROW_COLUMNALIAS_ROW:
            pEntry->SetFieldAlias(strNewText);
            break;
        case BROW_FUNCTION_ROW:
        {
            std::u16string_view sGroupFunctionName = m_aFunctionStrings.subView(m_aFunctionStrings.lastIndexOf(';')+1);
            pEntry->SetFunction(strNewText);
            // first reset this two member
            sal_Int32 nFunctionType = pEntry->GetFunctionType();
            nFunctionType &= ~FKT_AGGREGATE;
            pEntry->SetFunctionType(nFunctionType);
            if ( pEntry->IsGroupBy() && !o3tl::equalsIgnoreAsciiCase(sGroupFunctionName, strNewText) )
                pEntry->SetGroupBy(false);

            if ( o3tl::equalsIgnoreAsciiCase(sGroupFunctionName, strNewText) )
                pEntry->SetGroupBy(true);
            else if ( !strNewText.isEmpty() )
            {
                nFunctionType |= FKT_AGGREGATE;
                pEntry->SetFunctionType(nFunctionType);
            }
        }   break;
        default:
            pEntry->SetCriteria(sal_uInt16(nRow - BROW_CRIT1_ROW), strNewText);
    }

    tools::Long nCellIndex = GetRealRow(nRow);
    if(IsRowVisible(static_cast<sal_uInt16>(nRow)))
        RowModified(nCellIndex, nColId);

    // the appropriate field-description is now empty -> set Visible to sal_False (now it is consistent to normal empty rows)
    if (pEntry->IsEmpty())
        pEntry->SetVisible(false);

    if (bWasEditing)
        ActivateCell(nCellIndex, nColId);

    static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );
}

void OSelectionBrowseBox::ColumnResized(sal_uInt16 nColId)
{
    if (static_cast<OQueryController&>(getDesignView()->getController()).isReadOnly())
        return;
    // The resizing of columns can't be suppressed (BrowseBox doesn't support that) so we have to do this
    // fake. It's not _that_ bad : the user may change column widths while in read-only mode to see all details
    // but the changes aren't permanent ...

    sal_uInt16 nPos = GetColumnPos(nColId);
    OSL_ENSURE(nPos <= getFields().size(),"ColumnResized:: nColId should not be greater than List::count!");
    OTableFieldDescRef pEntry = getEntry(nPos-1);
    OSL_ENSURE(pEntry.is(), "OSelectionBrowseBox::ColumnResized : invalid FieldDescription !");
    static_cast<OQueryController&>(getDesignView()->getController()).setModified( true );
    EditBrowseBox::ColumnResized(nColId);

    if ( pEntry.is())
    {
        if ( !m_bInUndoMode )
        {
            // create the undo action
            std::unique_ptr<OTabFieldSizedUndoAct> pUndo(new OTabFieldSizedUndoAct(this));
            pUndo->SetColumnPosition( nPos );
            pUndo->SetOriginalWidth(pEntry->GetColWidth());
            getDesignView()->getController().addUndoActionAndInvalidate(std::move(pUndo));
        }
        pEntry->SetColWidth(sal_uInt16(GetColumnWidth(nColId)));
    }
}

sal_uInt32 OSelectionBrowseBox::GetTotalCellWidth(sal_Int32 nRowId, sal_uInt16 nColId)
{
    sal_uInt16 nPos = GetColumnPos(nColId);
    OSL_ENSURE((nPos == 0) || (nPos <= getFields().size()), "OSelectionBrowseBox::GetTotalCellWidth : invalid parameter nColId");

    OTableFieldDescRef pEntry = getFields()[nPos-1];
    OSL_ENSURE(pEntry.is(), "OSelectionBrowseBox::GetTotalCellWidth : invalid FieldDescription !");

    sal_Int32 nRow = GetRealRow(nRowId);
    OUString strText(GetCellText(nRow, nColId));
    return GetDataWindow().LogicToPixel(Size(GetDataWindow().GetTextWidth(strText),0)).Width();
}

bool OSelectionBrowseBox::isCutAllowed() const
{
    bool bCutAllowed = false;
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_VIS_ROW:
        case BROW_ORDER_ROW:
        case BROW_TABLE_ROW:
        case BROW_FUNCTION_ROW:
            break;
        case BROW_FIELD_ROW:
        {
            weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
            int nStartPos, nEndPos;
            bCutAllowed = rComboBox.get_entry_selection_bounds(nStartPos, nEndPos);
            break;
        }
        default:
        {
            weld::Entry& rEntry = m_pTextCell->get_widget();
            int nStartPos, nEndPos;
            bCutAllowed = rEntry.get_selection_bounds(nStartPos, nEndPos);
            break;
        }
    }
    return bCutAllowed;
}

void OSelectionBrowseBox::cut()
{
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_FIELD_ROW:
        {
            weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
            rComboBox.cut_entry_clipboard();
            break;
        }
        default:
        {
            weld::Entry& rEntry = m_pTextCell->get_widget();
            rEntry.cut_clipboard();
        }
    }
    SaveModified();
    RowModified(GetBrowseRow(nRow), GetCurColumnId());

    invalidateUndoRedo();
}

void OSelectionBrowseBox::paste()
{
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_FIELD_ROW:
        {
            weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
            rComboBox.paste_entry_clipboard();
            break;
        }
        default:
        {
            weld::Entry& rEntry = m_pTextCell->get_widget();
            rEntry.paste_clipboard();
            break;
        }
    }
    RowModified(GetBrowseRow(nRow), GetCurColumnId());
    invalidateUndoRedo();
}

bool OSelectionBrowseBox::isPasteAllowed() const
{
    bool bPasteAllowed = true;
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_VIS_ROW:
        case BROW_ORDER_ROW:
        case BROW_TABLE_ROW:
        case BROW_FUNCTION_ROW:
            bPasteAllowed = false;
            break;
    }
    return bPasteAllowed;
}

bool OSelectionBrowseBox::isCopyAllowed() const
{
    return isCutAllowed();
}

void OSelectionBrowseBox::copy()
{
    sal_Int32 nRow = GetRealRow(GetCurRow());
    switch (nRow)
    {
        case BROW_FIELD_ROW:
        {
            weld::ComboBox& rComboBox = m_pFieldCell->get_widget();
            rComboBox.copy_entry_clipboard();
            break;
        }
        default:
        {
            weld::Entry& rEntry = m_pTextCell->get_widget();
            rEntry.copy_clipboard();
            break;
        }
    }
}

void OSelectionBrowseBox::appendUndoAction(const OUString& _rOldValue, std::u16string_view _rNewValue, sal_Int32 _nRow, bool& _bListAction)
{
    if ( !m_bInUndoMode && _rNewValue != _rOldValue )
    {
        if ( !_bListAction )
        {
            _bListAction = true;
            static_cast<OQueryController&>(getDesignView()->getController()).GetUndoManager().EnterListAction(OUString(),OUString(),0,ViewShellId(-1));
        }
        appendUndoAction(_rOldValue,_rNewValue,_nRow);
    }
}

void OSelectionBrowseBox::appendUndoAction(const OUString& _rOldValue,std::u16string_view _rNewValue,sal_Int32 _nRow)
{
    if ( !m_bInUndoMode && _rNewValue != _rOldValue )
    {
        std::unique_ptr<OTabFieldCellModifiedUndoAct> pUndoAct(new OTabFieldCellModifiedUndoAct(this));
        pUndoAct->SetCellIndex(_nRow);
        OSL_ENSURE(GetColumnPos(GetCurColumnId()) != BROWSER_INVALIDID,"Current position isn't valid!");
        pUndoAct->SetColumnPosition( GetColumnPos(GetCurColumnId()) );
        pUndoAct->SetCellContents(_rOldValue);
        getDesignView()->getController().addUndoActionAndInvalidate(std::move(pUndoAct));
    }
}

IMPL_LINK_NOARG(OSelectionBrowseBox, OnInvalidateTimer, Timer *, void)
{
    static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature(SID_CUT);
    static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature(SID_COPY);
    static_cast<OQueryController&>(getDesignView()->getController()).InvalidateFeature(SID_PASTE);
    if(!m_bStopTimer)
        m_timerInvalidate.Start();
}

void OSelectionBrowseBox::stopTimer()
{
    m_bStopTimer = true;
    if (m_timerInvalidate.IsActive())
        m_timerInvalidate.Stop();
}

void OSelectionBrowseBox::startTimer()
{
    m_bStopTimer = false;
    if (!m_timerInvalidate.IsActive())
        m_timerInvalidate.Start();
}

OTableFields& OSelectionBrowseBox::getFields() const
{
    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());
    return rController.getTableFieldDesc();
}

void OSelectionBrowseBox::enableControl(const OTableFieldDescRef& _rEntry,Window* _pControl)
{
    bool bEnable = !_rEntry->isCondition();
    _pControl->Enable(bEnable);
    _pControl->EnableInput(bEnable);
}

void OSelectionBrowseBox::setTextCellContext(const OTableFieldDescRef& _rEntry,const OUString& _sText,const OUString& _sHelpId)
{
    weld::Entry& rEntry = m_pTextCell->get_widget();
    rEntry.set_text(_sText);
    rEntry.save_value();
    if (!m_pTextCell->HasFocus())
        m_pTextCell->GrabFocus();

    enableControl(_rEntry,m_pTextCell);

    if (m_pTextCell->GetHelpId() != _sHelpId)
        // as TextCell is used in various contexts I will delete the cached HelpText
        m_pTextCell->SetHelpText(OUString());
    m_pTextCell->SetHelpId(_sHelpId);
}

void OSelectionBrowseBox::invalidateUndoRedo()
{
    OQueryController& rController = static_cast<OQueryController&>(getDesignView()->getController());
    rController.InvalidateFeature( ID_BROWSER_UNDO );
    rController.InvalidateFeature( ID_BROWSER_REDO );
    rController.InvalidateFeature( ID_BROWSER_QUERY_EXECUTE );
}

OTableFieldDescRef OSelectionBrowseBox::getEntry(OTableFields::size_type _nPos)
{
    // we have to check if we need a new entry at this position
    OTableFields& aFields = getFields();
    OSL_ENSURE(aFields.size() > _nPos,"ColID is to great!");

    OTableFieldDescRef pEntry = aFields[_nPos];
    OSL_ENSURE(pEntry.is(),"Invalid entry!");
    if ( !pEntry.is() )
    {
        pEntry = new OTableFieldDesc();
        pEntry->SetColumnId(
            GetColumnId(sal::static_int_cast< sal_uInt16 >(_nPos+1)));
        aFields[_nPos] = pEntry;
    }
    return pEntry;
}

void OSelectionBrowseBox::GetFocus()
{
    if(!IsEditing() && !m_bWasEditing)
        ActivateCell();
    EditBrowseBox::GetFocus();
}

void OSelectionBrowseBox::DeactivateCell(bool _bUpdate)
{
    m_bWasEditing = true;
    EditBrowseBox::DeactivateCell(_bUpdate);
    m_bWasEditing = false;
}

OUString OSelectionBrowseBox::GetRowDescription( sal_Int32 _nRow ) const
{
    OUString  aLabel(DBA_RES(STR_QUERY_HANDLETEXT));

    // from BROW_CRIT2_ROW onwards all rows are shown as "or"
    sal_Int32 nToken = (_nRow >= GetBrowseRow(BROW_CRIT2_ROW))
        ?  BROW_CRIT2_ROW : GetRealRow(_nRow);
    return aLabel.getToken(nToken, ';');
}

OUString OSelectionBrowseBox::GetAccessibleObjectName( AccessibleBrowseBoxObjType _eObjType,sal_Int32 _nPosition) const
{
    OUString sRetText;
    switch( _eObjType )
    {
        case AccessibleBrowseBoxObjType::RowHeaderCell:
            sRetText = GetRowDescription(_nPosition);
            break;
        default:
            sRetText = EditBrowseBox::GetAccessibleObjectDescription(_eObjType,_nPosition);
    }
    return sRetText;
}

bool OSelectionBrowseBox::fillEntryTable(OTableFieldDescRef const & _pEntry,const OUString& _sTableName)
{
    bool bRet = false;
    OJoinTableView::OTableWindowMap& rTabWinList = getDesignView()->getTableView()->GetTabWinMap();
    OJoinTableView::OTableWindowMap::const_iterator aIter = rTabWinList.find(_sTableName);
    if(aIter != rTabWinList.end())
    {
        OQueryTableWindow* pEntryTab = static_cast<OQueryTableWindow*>(aIter->second.get());
        if (pEntryTab)
        {
            _pEntry->SetTable(pEntryTab->GetTableName());
            _pEntry->SetTabWindow(pEntryTab);
            bRet = true;
        }
    }
    return bRet;
}

void OSelectionBrowseBox::setFunctionCell(OTableFieldDescRef const & _pEntry)
{
    Reference< XConnection> xConnection = static_cast<OQueryController&>(getDesignView()->getController()).getConnection();
    if ( !xConnection.is() )
        return;

    // Aggregate functions in general only available with Core SQL
    if ( lcl_SupportsCoreSQLGrammar(xConnection) )
    {
        sal_Int32 nIdx {0};
        // if we have an asterisk, no other function than count is allowed
        weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
        rComboBox.clear();
        rComboBox.append_text(m_aFunctionStrings.getToken(0, ';', nIdx));
        if ( isFieldNameAsterisk(_pEntry->GetField()) )
            rComboBox.append_text(m_aFunctionStrings.getToken(1, ';', nIdx)); // 2nd token: COUNT
        else
        {
            const bool bSkipLastToken {_pEntry->isNumeric()};
            while (nIdx>0)
            {
                const OUString sTok {m_aFunctionStrings.getToken(0, ';', nIdx)};
                if (bSkipLastToken && nIdx<0)
                    break;
                rComboBox.append_text(sTok);
            }
        }

        if ( _pEntry->IsGroupBy() )
        {
            OSL_ENSURE(!_pEntry->isNumeric(),"Not allowed to combine group by and numeric values!");
            rComboBox.set_active_text(rComboBox.get_text(rComboBox.get_count() - 1));
        }
        else if (rComboBox.find_text(_pEntry->GetFunction()) != -1)
            rComboBox.set_active_text(_pEntry->GetFunction());
        else
            rComboBox.set_active(0);

        enableControl(_pEntry, m_pFunctionCell);
    }
    else
    {
        // only COUNT(*) and COUNT("table".*) allowed
        bool bCountRemoved = !isFieldNameAsterisk(_pEntry->GetField());
        weld::ComboBox& rComboBox = m_pFunctionCell->get_widget();
        if ( bCountRemoved )
            rComboBox.remove(1);

        if ( !bCountRemoved && rComboBox.get_count() < 2)
            rComboBox.append_text(m_aFunctionStrings.getToken(2, ';')); // 2 -> COUNT

        if (rComboBox.find_text(_pEntry->GetFunction()) != -1)
            rComboBox.set_active_text(_pEntry->GetFunction());
        else
            rComboBox.set_active(0);
    }
}

Reference< XAccessible > OSelectionBrowseBox::CreateAccessibleCell( sal_Int32 _nRow, sal_uInt16 _nColumnPos )
{
    OTableFieldDescRef pEntry;
    if ( _nColumnPos != 0 && _nColumnPos != BROWSER_INVALIDID && _nColumnPos <= getFields().size() )
        pEntry = getFields()[_nColumnPos - 1];

    if ( _nRow == BROW_VIS_ROW && pEntry.is() )
        return EditBrowseBox::CreateAccessibleCheckBoxCell( _nRow, _nColumnPos,pEntry->IsVisible() ? TRISTATE_TRUE : TRISTATE_FALSE );

    return EditBrowseBox::CreateAccessibleCell( _nRow, _nColumnPos );
}

bool OSelectionBrowseBox::HasFieldByAliasName(std::u16string_view rFieldName, OTableFieldDescRef const & rInfo) const
{
    for (auto const& field : getFields())
    {
        if ( field->GetFieldAlias() == rFieldName )
        {
            *rInfo = *field;
            return true;
        }
    }
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
