/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <basegfx/polygon/b2dpolygon.hxx>
#include <comphelper/dispatchcommand.hxx>
#include <drawinglayer/primitive2d/PolyPolygonColorPrimitive2D.hxx>
#include <drawinglayer/primitive2d/PolyPolygonStrokePrimitive2D.hxx>
#include <drawinglayer/processor2d/baseprocessor2d.hxx>
#include <drawinglayer/processor2d/processor2dtools.hxx>
#include <memory>
#include <officecfg/Office/UI/Infobar.hxx>
#include <officecfg/Office/Common.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/infobar.hxx>
#include <sfx2/objface.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/viewfrm.hxx>
#include <utility>
#include <vcl/image.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/virdev.hxx>
#include <vcl/weldutils.hxx>
#include <bitmaps.hlst>

using namespace drawinglayer::geometry;
using namespace drawinglayer::processor2d;
using namespace drawinglayer::primitive2d;
using namespace drawinglayer::attribute;
using namespace basegfx;
using namespace css::frame;

namespace
{
void GetInfoBarColors(InfobarType ibType, BColor& rBackgroundColor, BColor& rForegroundColor,
                      BColor& rMessageColor)
{
    const StyleSettings& rSettings = Application::GetSettings().GetStyleSettings();
    const bool bIsDark = rSettings.GetWindowColor().IsDark();
    switch (ibType)
    {
        case InfobarType::INFO: // blue; #004785/0,71,133; #BDE5F8/189,229,248
            rBackgroundColor = bIsDark ? basegfx::BColor(0.000, 0.278, 0.522)
                                       : basegfx::BColor(0.741, 0.898, 0.973);
            rForegroundColor = bIsDark ? basegfx::BColor(0.741, 0.898, 0.973)
                                       : basegfx::BColor(0.000, 0.278, 0.522);
            rMessageColor = rForegroundColor;
            break;
        case InfobarType::SUCCESS: // green; #32550C/50,85,12; #DFF2BF/223,242,191
            rBackgroundColor = bIsDark ? basegfx::BColor(0.196, 0.333, 0.047)
                                       : basegfx::BColor(0.874, 0.949, 0.749);
            rForegroundColor = bIsDark ? basegfx::BColor(0.874, 0.949, 0.749)
                                       : basegfx::BColor(0.196, 0.333, 0.047);
            rMessageColor = rForegroundColor;
            break;
        case InfobarType::WARNING: // orange; #704300/112,67,0; #FEEFB3/254,239,179
            rBackgroundColor = rSettings.GetWarningColor().getBColor();
            rForegroundColor = rSettings.GetWarningTextColor().getBColor();
            rMessageColor = rSettings.GetWarningTextColor().getBColor();
            break;
        case InfobarType::DANGER: // red; #7A0006/122,0,6; #FFBABA/255,186,186
            rBackgroundColor = rSettings.GetErrorColor().getBColor();
            rForegroundColor = rSettings.GetErrorTextColor().getBColor();
            rMessageColor = rSettings.GetErrorTextColor().getBColor();
            break;
    }

    if (rSettings.GetHighContrastMode())
    {
        rBackgroundColor = rSettings.GetLightColor().getBColor();
        rForegroundColor = rSettings.GetDialogTextColor().getBColor();
    }
}
OUString GetInfoBarIconName(InfobarType ibType)
{
    OUString aRet;

    switch (ibType)
    {
        case InfobarType::INFO:
            aRet = "vcl/res/infobox.png";
            break;
        case InfobarType::SUCCESS:
            aRet = "vcl/res/successbox.png";
            break;
        case InfobarType::WARNING:
            aRet = "vcl/res/warningbox.png";
            break;
        case InfobarType::DANGER:
            aRet = "vcl/res/errorbox.png";
            break;
    }

    return aRet;
}

} // anonymous namespace

void SfxInfoBarWindow::SetCloseButtonImage()
{
    Size aSize = Image(StockImage::Yes, CLOSEDOC).GetSizePixel();
    aSize = Size(aSize.Width() * 1.5, aSize.Height() * 1.5);

    ScopedVclPtr<VirtualDevice> xDevice(m_xCloseBtn->create_virtual_device());
    xDevice->SetOutputSizePixel(Size(24, 24));
    xDevice->SetBackground(Color(m_aBackgroundColor));
    xDevice->Erase();

    const int nPos = (24 - aSize.getWidth()) / 2;
    Point aBtnPos(nPos, nPos);

    const ViewInformation2D aNewViewInfos;
    const std::unique_ptr<BaseProcessor2D> pProcessor(
        createProcessor2DFromOutputDevice(*xDevice, aNewViewInfos));

    const ::tools::Rectangle aRect(aBtnPos, xDevice->PixelToLogic(aSize));

    drawinglayer::primitive2d::Primitive2DContainer aSeq(2);

    // Draw background. The right and bottom need to be extended by 1 or
    // there will be a white line on both edges when Skia is enabled.
    B2DPolygon aPolygon;
    aPolygon.append(B2DPoint(aRect.Left(), aRect.Top()));
    aPolygon.append(B2DPoint(aRect.Right() + 1, aRect.Top()));
    aPolygon.append(B2DPoint(aRect.Right() + 1, aRect.Bottom() + 1));
    aPolygon.append(B2DPoint(aRect.Left(), aRect.Bottom() + 1));
    aPolygon.setClosed(true);

    aSeq[0] = new PolyPolygonColorPrimitive2D(B2DPolyPolygon(aPolygon), m_aBackgroundColor);

    LineAttribute aLineAttribute(m_aForegroundColor, 2.0);

    // Cross
    B2DPolyPolygon aCross;

    B2DPolygon aLine1;
    aLine1.append(B2DPoint(aRect.Left(), aRect.Top()));
    aLine1.append(B2DPoint(aRect.Right(), aRect.Bottom()));
    aCross.append(aLine1);

    B2DPolygon aLine2;
    aLine2.append(B2DPoint(aRect.Right(), aRect.Top()));
    aLine2.append(B2DPoint(aRect.Left(), aRect.Bottom()));
    aCross.append(aLine2);

    aSeq[1]
        = new PolyPolygonStrokePrimitive2D(std::move(aCross), aLineAttribute, StrokeAttribute());

    pProcessor->process(aSeq);

    m_xCloseBtn->set_item_image(u"close"_ustr, xDevice);
}

class ExtraButton
{
private:
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Container> m_xContainer;
    std::unique_ptr<weld::Button> m_xButton;
    /** StatusListener. Updates the button as the slot state changes */
    rtl::Reference<weld::WidgetStatusListener> m_xStatusListener;
    OUString m_aCommand;

    DECL_LINK(CommandHdl, weld::Button&, void);

public:
    ExtraButton(weld::Container* pContainer, const OUString* pCommand)
        : m_xBuilder(Application::CreateBuilder(pContainer, u"sfx/ui/extrabutton.ui"_ustr))
        , m_xContainer(m_xBuilder->weld_container(u"ExtraButton"_ustr))
        , m_xButton(m_xBuilder->weld_button(u"button"_ustr))
    {
        if (pCommand)
        {
            m_aCommand = *pCommand;
            m_xButton->connect_clicked(LINK(this, ExtraButton, CommandHdl));
            m_xStatusListener.set(new weld::WidgetStatusListener(m_xButton.get(), m_aCommand));
            m_xStatusListener->startListening();
        }
    }

    ~ExtraButton()
    {
        if (m_xStatusListener.is())
            m_xStatusListener->dispose();
    }

    weld::Button& get_widget() { return *m_xButton; }
};

IMPL_LINK_NOARG(ExtraButton, CommandHdl, weld::Button&, void)
{
    comphelper::dispatchCommand(m_aCommand, css::uno::Sequence<css::beans::PropertyValue>());
}

SfxInfoBarWindow::SfxInfoBarWindow(vcl::Window* pParent, OUString sId,
                                   const OUString& sPrimaryMessage,
                                   const OUString& sSecondaryMessage, InfobarType ibType,
                                   bool bShowCloseButton)
    : InterimItemWindow(pParent, u"sfx/ui/infobar.ui"_ustr, u"InfoBar"_ustr)
    , m_sId(std::move(sId))
    , m_eType(ibType)
    , m_bLayingOut(false)
    , m_xImage(m_xBuilder->weld_image(u"image"_ustr))
    , m_xPrimaryMessage(m_xBuilder->weld_label(u"primary"_ustr))
    , m_xSecondaryMessage(m_xBuilder->weld_text_view(u"secondary"_ustr))
    , m_xButtonBox(m_xBuilder->weld_container(u"buttonbox"_ustr))
    , m_xCloseBtn(m_xBuilder->weld_toolbar(u"closebar"_ustr))
{
    SetStyle(GetStyle() | WB_DIALOGCONTROL);

    InitControlBase(m_xCloseBtn.get());

    m_xImage->set_from_icon_name(GetInfoBarIconName(ibType));
    m_xSecondaryMessage->set_margin_top(m_xImage->get_preferred_size().Height() / 4);

    if (!sPrimaryMessage.isEmpty())
    {
        m_xPrimaryMessage->set_label(sPrimaryMessage);
        m_xPrimaryMessage->show();
    }

    m_xSecondaryMessage->set_text(sSecondaryMessage);
    m_aOrigMessageSize = m_xSecondaryMessage->get_preferred_size();
    m_aMessageSize = m_aOrigMessageSize;
    m_xSecondaryMessage->connect_size_allocate(LINK(this, SfxInfoBarWindow, SizeAllocHdl));

    if (bShowCloseButton)
    {
        m_xCloseBtn->connect_clicked(LINK(this, SfxInfoBarWindow, CloseHandler));
        m_xCloseBtn->show();
    }

    EnableChildTransparentMode();

    SetForeAndBackgroundColors(m_eType);

    auto nWidth = pParent->GetSizePixel().getWidth();
    auto nHeight = get_preferred_size().Height();
    SetSizePixel(Size(nWidth, nHeight + 2));

    Resize();
}

IMPL_LINK(SfxInfoBarWindow, SizeAllocHdl, const Size&, rSize, void)
{
    if (m_aMessageSize != rSize)
    {
        m_aMessageSize = rSize;
        static_cast<SfxInfoBarContainerWindow*>(GetParent())->TriggerUpdateLayout();
    }
}

Size SfxInfoBarWindow::DoLayout()
{
    Size aGivenSize(GetSizePixel());

    // disconnect SizeAllocHdl because we don't care about the size change
    // during layout
    m_xSecondaryMessage->connect_size_allocate(Link<const Size&, void>());

    // blow away size cache in case m_aMessageSize.Width() is already the width request
    // and we would get the cached preferred size instead of the recalc we want to force
    m_xSecondaryMessage->set_size_request(-1, -1);
    // make the width we were detected as set to by SizeAllocHdl as our desired width
    m_xSecondaryMessage->set_size_request(m_aMessageSize.Width(), -1);
    // get our preferred size with that message width
    Size aSizeForWidth(aGivenSize.Width(), m_xContainer->get_preferred_size().Height());
    // restore the message preferred size so we can freely resize, and get a new
    // m_aMessageSize and repeat the process if we do
    m_xSecondaryMessage->set_size_request(m_aOrigMessageSize.Width(), -1);

    // connect SizeAllocHdl so changes outside of this layout will trigger a new layout
    m_xSecondaryMessage->connect_size_allocate(LINK(this, SfxInfoBarWindow, SizeAllocHdl));

    return aSizeForWidth;
}

void SfxInfoBarWindow::Layout()
{
    if (m_bLayingOut)
        return;
    m_bLayingOut = true;

    InterimItemWindow::Layout();

    m_bLayingOut = false;
}

weld::Button& SfxInfoBarWindow::addButton(const OUString* pCommand)
{
    m_aActionBtns.emplace_back(std::make_unique<ExtraButton>(m_xButtonBox.get(), pCommand));

    return m_aActionBtns.back()->get_widget();
}

SfxInfoBarWindow::~SfxInfoBarWindow() { disposeOnce(); }

void SfxInfoBarWindow::SetForeAndBackgroundColors(InfobarType eType)
{
    basegfx::BColor aMessageColor;
    GetInfoBarColors(eType, m_aBackgroundColor, m_aForegroundColor, aMessageColor);

    m_xPrimaryMessage->set_font_color(Color(aMessageColor));
    m_xSecondaryMessage->set_font_color(Color(aMessageColor));

    Color aBackgroundColor(m_aBackgroundColor);
    m_xPrimaryMessage->set_background(aBackgroundColor);
    m_xSecondaryMessage->set_background(aBackgroundColor);
    m_xContainer->set_background(aBackgroundColor);
    if (m_xCloseBtn->get_visible())
    {
        m_xCloseBtn->set_background(aBackgroundColor);
        SetCloseButtonImage();
    }
}

void SfxInfoBarWindow::dispose()
{
    for (auto& rxBtn : m_aActionBtns)
        rxBtn.reset();

    m_xImage.reset();
    m_xPrimaryMessage.reset();
    m_xSecondaryMessage.reset();
    m_xButtonBox.reset();
    m_xCloseBtn.reset();
    m_aActionBtns.clear();
    InterimItemWindow::dispose();
}

void SfxInfoBarWindow::Update(const OUString& sPrimaryMessage, const OUString& sSecondaryMessage,
                              InfobarType eType)
{
    if (m_eType != eType)
    {
        m_eType = eType;
        SetForeAndBackgroundColors(m_eType);
        m_xImage->set_from_icon_name(GetInfoBarIconName(eType));
    }

    m_xPrimaryMessage->set_label(sPrimaryMessage);
    m_xSecondaryMessage->set_text(sSecondaryMessage);
    Resize();
    Invalidate();
}

IMPL_LINK_NOARG(SfxInfoBarWindow, CloseHandler, const OUString&, void)
{
    static_cast<SfxInfoBarContainerWindow*>(GetParent())->removeInfoBar(this);
}

SfxInfoBarContainerWindow::SfxInfoBarContainerWindow(SfxInfoBarContainerChild* pChildWin)
    : Window(pChildWin->GetParent(), WB_DIALOGCONTROL)
    , m_pChildWin(pChildWin)
    , m_aLayoutIdle("SfxInfoBarContainerWindow m_aLayoutIdle")
    , m_bResizing(false)
{
    m_aLayoutIdle.SetPriority(TaskPriority::HIGHEST);
    m_aLayoutIdle.SetInvokeHandler(LINK(this, SfxInfoBarContainerWindow, DoUpdateLayout));
}

IMPL_LINK_NOARG(SfxInfoBarContainerWindow, DoUpdateLayout, Timer*, void) { m_pChildWin->Update(); }

SfxInfoBarContainerWindow::~SfxInfoBarContainerWindow() { disposeOnce(); }

void SfxInfoBarContainerWindow::dispose()
{
    for (auto& infoBar : m_pInfoBars)
        infoBar.disposeAndClear();
    m_pInfoBars.clear();
    Window::dispose();
}

VclPtr<SfxInfoBarWindow> SfxInfoBarContainerWindow::appendInfoBar(const OUString& sId,
                                                                  const OUString& sPrimaryMessage,
                                                                  const OUString& sSecondaryMessage,
                                                                  InfobarType ibType,
                                                                  bool bShowCloseButton)
{
    if (!isInfobarEnabled(sId))
        return nullptr;

    auto pInfoBar = VclPtr<SfxInfoBarWindow>::Create(this, sId, sPrimaryMessage, sSecondaryMessage,
                                                     ibType, bShowCloseButton);

    basegfx::BColor aBackgroundColor;
    basegfx::BColor aForegroundColor;
    basegfx::BColor aMessageColor;
    GetInfoBarColors(ibType, aBackgroundColor, aForegroundColor, aMessageColor);
    pInfoBar->m_aBackgroundColor = aBackgroundColor;
    pInfoBar->m_aForegroundColor = aForegroundColor;
    m_pInfoBars.push_back(pInfoBar);

    Resize();
    return pInfoBar;
}

VclPtr<SfxInfoBarWindow> SfxInfoBarContainerWindow::getInfoBar(std::u16string_view sId)
{
    for (auto const& infoBar : m_pInfoBars)
    {
        if (infoBar->getId() == sId)
            return infoBar;
    }
    return nullptr;
}

bool SfxInfoBarContainerWindow::hasInfoBarWithID(std::u16string_view sId)
{
    return (getInfoBar(sId) != nullptr);
}

void SfxInfoBarContainerWindow::removeInfoBar(VclPtr<SfxInfoBarWindow> const& pInfoBar)
{
    // Remove
    auto it = std::find(m_pInfoBars.begin(), m_pInfoBars.end(), pInfoBar);
    if (it != m_pInfoBars.end())
    {
        it->disposeAndClear();
        m_pInfoBars.erase(it);
    }

    m_pChildWin->Update();
}

bool SfxInfoBarContainerWindow::isInfobarEnabled(std::u16string_view sId)
{
    if (sId == u"readonly")
        return officecfg::Office::UI::Infobar::Enabled::Readonly::get();
    if (sId == u"signature")
        return officecfg::Office::UI::Infobar::Enabled::Signature::get();
    if (sId == u"donate")
        return officecfg::Office::UI::Infobar::Enabled::Donate::get();
    if (sId == u"getinvolved")
        return officecfg::Office::UI::Infobar::Enabled::GetInvolved::get();
    if (sId == u"hyphenationmissing")
        return officecfg::Office::UI::Infobar::Enabled::HyphenationMissing::get();
    if (sId == u"whatsnew")
        return officecfg::Office::UI::Infobar::Enabled::WhatsNew::get();
    if (sId == u"hiddentrackchanges")
        return officecfg::Office::UI::Infobar::Enabled::HiddenTrackChanges::get();
    if (sId == u"macro")
        return officecfg::Office::UI::Infobar::Enabled::MacrosDisabled::get();
    if (sId == u"securitywarn")
    {
        return officecfg::Office::Common::Security::Scripting::WarnSaveOrSendDoc::get()
               || officecfg::Office::Common::Security::Scripting::WarnSignDoc::get()
               || officecfg::Office::Common::Security::Scripting::WarnPrintDoc::get()
               || officecfg::Office::Common::Security::Scripting::WarnCreatePDF::get();
    }

    return true;
}

// This triggers the SfxFrame to re-layout its childwindows
void SfxInfoBarContainerWindow::TriggerUpdateLayout() { m_aLayoutIdle.Start(); }

void SfxInfoBarContainerWindow::Resize()
{
    if (m_bResizing)
        return;
    m_bResizing = true;
    const Size aWindowOrigSize = GetSizePixel();
    auto nOrigWidth = aWindowOrigSize.getWidth();
    auto nOrigHeight = aWindowOrigSize.getHeight();

    tools::Long nHeight = 0;

    for (auto& rxInfoBar : m_pInfoBars)
    {
        Size aOrigSize = rxInfoBar->GetSizePixel();
        Size aSize(nOrigWidth, aOrigSize.Height());

        Point aPos(0, nHeight);
        // stage 1: provisionally size the infobar,
        rxInfoBar->SetPosSizePixel(aPos, aSize);

        // stage 2: perhaps allow height to stretch to fit
        // the stage 1 width
        aSize = rxInfoBar->DoLayout();
        rxInfoBar->SetPosSizePixel(aPos, aSize);
        rxInfoBar->Show();

        // Stretch to fit the infobar(s)
        nHeight += aSize.getHeight();
    }

    if (nOrigHeight != nHeight)
    {
        SetSizePixel(Size(nOrigWidth, nHeight));
        TriggerUpdateLayout();
    }

    m_bResizing = false;
}

SFX_IMPL_POS_CHILDWINDOW_WITHID(SfxInfoBarContainerChild, SID_INFOBAR, SFX_OBJECTBAR_OBJECT);

SfxInfoBarContainerChild::SfxInfoBarContainerChild(vcl::Window* _pParent, sal_uInt16 nId,
                                                   SfxBindings* pBindings, SfxChildWinInfo*)
    : SfxChildWindow(_pParent, nId)
    , m_pBindings(pBindings)
{
    SetWindow(VclPtr<SfxInfoBarContainerWindow>::Create(this));
    GetWindow()->SetPosSizePixel(Point(0, 0), Size(_pParent->GetSizePixel().getWidth(), 0));
    GetWindow()->Show();

    SetAlignment(SfxChildAlignment::LOWESTTOP);
}

SfxInfoBarContainerChild::~SfxInfoBarContainerChild() {}

SfxChildWinInfo SfxInfoBarContainerChild::GetInfo() const
{
    SfxChildWinInfo aInfo = SfxChildWindow::GetInfo();
    return aInfo;
}

void SfxInfoBarContainerChild::Update()
{
    // Layout to current width, this may change the height
    if (vcl::Window* pChild = GetWindow())
    {
        Size aSize(pChild->GetSizePixel());
        pChild->Resize();
        if (aSize == pChild->GetSizePixel())
            return;
    }

    // Refresh the frame to take the infobars container height change into account
    const sal_uInt16 nId = GetChildWindowId();
    SfxViewFrame* pVFrame = m_pBindings->GetDispatcher()->GetFrame();
    pVFrame->ShowChildWindow(nId);

    // Give the focus to the document view
    pVFrame->GetWindow().GrabFocusToDocument();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
