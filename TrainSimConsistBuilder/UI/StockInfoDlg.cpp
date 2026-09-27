#include "StockInfoDlg.h"
#include "CustomScrollBar.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace StockInfoTheme
{
    constexpr COLORREF Background         = RGB(20, 20, 20); // Main surface
    constexpr COLORREF HeaderBackground   = RGB(26, 26, 26); // Top header
    constexpr COLORREF FooterBackground   = RGB(24, 24, 24); // Bottom footer
    constexpr COLORREF CardBackground     = RGB(28, 28, 28); // Card surface
    constexpr COLORREF CardHeaderBg       = RGB(34, 34, 34); // Card header
    constexpr COLORREF CardBorder         = RGB(46, 46, 46); // Card border
    constexpr COLORREF BorderLine         = RGB(42, 42, 42); // Dividers
    constexpr COLORREF TextPrimary        = RGB(245, 245, 245);
    constexpr COLORREF TextSecondary      = RGB(170, 170, 170);
    constexpr COLORREF TextMuted          = RGB(125, 125, 125);
    constexpr COLORREF AccentBlue         = RGB(0, 120, 215);
    constexpr COLORREF AccentHover        = RGB(20, 140, 235);
    constexpr COLORREF AccentPressed      = RGB(0, 100, 185);
    constexpr COLORREF StatusValid        = RGB(80, 200, 120); // Green
    constexpr COLORREF StatusMissing      = RGB(245, 80, 80);  // Red
    constexpr COLORREF TagBg              = RGB(38, 48, 60);
    constexpr COLORREF TagBorder          = RGB(50, 75, 105);
    constexpr COLORREF TagText            = RGB(100, 185, 255);
}

struct SectionRow
{
    std::wstring label;
    std::wstring value;
    COLORREF valColor = StockInfoTheme::TextPrimary;
};

struct SectionCard
{
    std::wstring title;
    const wchar_t* iconGlyph = L"\xE946";
    std::vector<SectionRow> rows;
    std::vector<std::wstring> rawLines;
    int calculatedHeight = 0;
};

struct StockInfoDialogState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    StockSpecReader::StockSpec spec;
    std::wstring basePath;
    std::vector<SectionCard> cards;

    // Fonts
    HFONT hFontTitle = NULL;
    HFONT hFontHeader = NULL;
    HFONT hFontSubTitle = NULL;
    HFONT hFontCardTitle = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontMono = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    // Scrollbar & Layout
    CustomScrollBar scrollBar{ ScrollBarOrientation::Vertical };
    int totalContentHeight = 0;
    int scrollOffsetY = 0;

    // Buttons
    RECT rcBtnCopy = { 0 };
    RECT rcBtnClose = { 0 };
    RECT rcBtnTopClose = { 0 };
    bool isHoverCopy = false;
    bool isPressCopy = false;
    bool isHoverClose = false;
    bool isPressClose = false;
    bool isHoverTopClose = false;
    bool isPressTopClose = false;

    // Interactive Row Hover & Toast Tooltip
    std::wstring hoveredLabel;
    std::wstring hoveredValue;
    bool isHoveringTruncatedRow = false;
    bool isHoveringAnyRow = false;

    // Active Toast message
    std::wstring toastMessage;
    DWORD toastExpireTick = 0;

    bool isDraggingWindow = false;
    POINT dragStartCursor = { 0 };
    POINT dragStartWindow = { 0 };
};

static HFONT CreateDpiFont(int pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static void BuildCardsFromSpec(StockInfoDialogState* pState)
{
    pState->cards.clear();
    const auto& spec = pState->spec;

    // 0. BROKEN / MISSING UNIT DIAGNOSTIC ALERT
    if (!spec.fileExistsOnDisk)
    {
        SectionCard cardDiag;
        cardDiag.title = L"Unit Integrity & Diagnostic Warning";
        cardDiag.iconGlyph = L"\xE783"; // Warning icon
        cardDiag.rows.push_back({ L"File Status", L"MISSING ON DISK (Broken Unit)", StockInfoTheme::StatusMissing });
        cardDiag.rows.push_back({ L"Target File", spec.fileName, StockInfoTheme::TextPrimary });
        cardDiag.rows.push_back({ L"Expected Folder", spec.folderName.empty() ? L"(Unknown)" : spec.folderName, StockInfoTheme::TextPrimary });
        cardDiag.rows.push_back({ L"Expected Full Path", spec.filePath, StockInfoTheme::TextSecondary });
        cardDiag.rows.push_back({ L"Diagnosis", L"The vehicle file is missing from TRAINS\\TRAINSET\\. Check for spelling typo or uninstalled trainset folder.", RGB(245, 180, 80) });
        pState->cards.push_back(cardDiag);
    }

    // 1. IDENTIFICATION
    {
        SectionCard card;
        card.title = L"Identification & File Properties";
        card.iconGlyph = L"\xE8D7"; // Details/Asset
        card.rows.push_back({ L"Display Name", spec.displayName });
        card.rows.push_back({ L"File Name", spec.fileName });
        card.rows.push_back({ L"Trainset Folder", spec.folderName });
        card.rows.push_back({ L"File Status", spec.fileExistsOnDisk ? L"VALID (Found on disk)" : L"MISSING ON DISK (Broken Unit)", spec.fileExistsOnDisk ? StockInfoTheme::StatusValid : StockInfoTheme::StatusMissing });
        std::wstring typeDisplay;
        if (spec.extension == L".eng")
        {
            typeDisplay = L"Engine (" + (spec.category.empty() || spec.category == L"Unresolved" ? L"Unresolved" : spec.category) + L")";
        }
        else if (spec.extension == L".wag")
        {
            typeDisplay = L"Wagon (" + (spec.category.empty() || spec.category == L"Unresolved" ? L"Unresolved" : spec.category) + L")";
        }
        else
        {
            typeDisplay = L"Unknown Unit";
        }
        card.rows.push_back({ L"Type", typeDisplay });
        card.rows.push_back({ L"File Path", spec.filePath, StockInfoTheme::TextSecondary });
        pState->cards.push_back(card);
    }

    // 2. PHYSICAL DIMENSIONS & OPEN RAILS PLACEMENT
    {
        SectionCard card;
        card.title = L"Physical Dimensions & Consist Placement";
        card.iconGlyph = L"\xE7F4"; // Dimensions/Ruler

        wchar_t buf[128];
        swprintf_s(buf, L"%.2f m", spec.size.widthM);
        card.rows.push_back({ L"Width", buf });

        swprintf_s(buf, L"%.2f m", spec.size.heightM);
        card.rows.push_back({ L"Height", buf });

        swprintf_s(buf, L"%.2f m", spec.size.lengthM);
        card.rows.push_back({ L"Body Length (Size)", buf });

        float r0 = spec.GetCouplerZeroLengthM();
        swprintf_s(buf, L"%.3f m (%.1f cm)", r0, r0 * 100.0f);
        card.rows.push_back({ L"Coupler Zero Length (r0)", buf, (r0 > 0.0f) ? StockInfoTheme::TextPrimary : StockInfoTheme::TextMuted });

        float step = spec.GetEffectivePlacementStepM();
        swprintf_s(buf, L"%.3f m (Length + r0 spacing)", step);
        card.rows.push_back({ L"Consist Placement Step", buf, StockInfoTheme::TagText });

        pState->cards.push_back(card);
    }

    // 3. 3D VISUAL SHAPES & FREIGHT ANIMS
    {
        SectionCard card;
        card.title = L"3D Visual Meshes & Attachments";
        card.iconGlyph = L"\xE8B9"; // 3D Cube / Shapes
        card.rows.push_back({ L"Primary Shape (.s)", spec.mainShapeFile.empty() ? L"None" : spec.mainShapeFile });
        card.rows.push_back({ L"Shape File Status", spec.shapeExistsOnDisk ? L"VALID (Found on disk)" : L"MISSING / UNRESOLVED!", spec.shapeExistsOnDisk ? StockInfoTheme::StatusValid : StockInfoTheme::StatusMissing });
        if (!spec.fullShapePath.empty())
        {
            card.rows.push_back({ L"Full Shape Path", spec.fullShapePath, StockInfoTheme::TextSecondary });
        }
        if (!spec.freightAnims.empty())
        {
            for (size_t i = 0; i < spec.freightAnims.size(); ++i)
            {
                const auto& fa = spec.freightAnims[i];
                std::wstring lbl = L"Freight Anim #" + std::to_wstring(i + 1);
                std::wstring val = fa.shapePath;
                if (fa.existsOnDisk)
                {
                    val += L"  [FOUND]";
                }
                else if (!fa.fullPath.empty())
                {
                    val += L"  [NOT FOUND]";
                }
                COLORREF col = fa.existsOnDisk ? StockInfoTheme::StatusValid : (!fa.fullPath.empty() ? StockInfoTheme::StatusMissing : StockInfoTheme::TextPrimary);
                card.rows.push_back({ lbl, val, col });
                if (!fa.fullPath.empty())
                {
                    std::wstring pathLbl = L"  ↳ Anim #" + std::to_wstring(i + 1) + L" Path";
                    card.rows.push_back({ pathLbl, fa.fullPath, StockInfoTheme::TextSecondary });
                }
            }
        }
        else
        {
            card.rows.push_back({ L"Freight Animations", L"None" });
        }
        pState->cards.push_back(card);
    }

    // 4. COUPLING & BUFFERS
    {
        SectionCard card;
        card.title = L"Couplers & Buffer Settings";
        card.iconGlyph = L"\xE71D"; // Link/Coupler
        if (!spec.couplers.empty())
        {
            for (size_t i = 0; i < spec.couplers.size(); ++i)
            {
                const auto& c = spec.couplers[i];
                std::wstring lbl = L"Coupler #" + std::to_wstring(i + 1);

                // Primary Coupler summary row
                std::wstring r0Str = !c.rawR0.empty() ? c.rawR0 : (c.r0_max >= 1e8f ? L"0cm 1e9" : (std::to_wstring(c.r0_min).substr(0, 4) + L"m"));
                std::wstring breakStr = !c.rawBreak.empty() ? c.rawBreak : (c.break1 >= 1e8f ? L"1e9" : (std::to_wstring((int)(c.break1 / 1000.0f)) + L"kN"));

                wchar_t buf1[256];
                swprintf_s(buf1, L"Type: %s  •  r0: %s  •  Break: %s  •  Rigid: %s",
                    c.type.c_str(), r0Str.c_str(), breakStr.c_str(), c.hasRigidConnection ? L"Yes" : L"No");
                card.rows.push_back({ lbl, buf1 });

                // Spring Dynamics sub-row (Stiffness, Damping, Velocity)
                std::wstring stifStr = !c.rawStiffness.empty() ? c.rawStiffness : L"5e6N/m 0";
                std::wstring dampStr = !c.rawDamping.empty() ? c.rawDamping : L"1e6N/m/s 0";
                std::wstring velStr = !c.rawVelocity.empty() ? c.rawVelocity : L"0.15m/s";

                wchar_t buf2[256];
                swprintf_s(buf2, L"Stiffness: %s  •  Damping: %s  •  Velocity: %s",
                    stifStr.c_str(), dampStr.c_str(), velStr.c_str());
                card.rows.push_back({ L"  ↳ Spring Dynamics", buf2, StockInfoTheme::TextSecondary });
            }
        }
        else
        {
            card.rows.push_back({ L"Couplers", L"Defaulting to Open Rails standard: 0.15m" });
        }

        if (spec.buffers.exists)
        {
            std::wstring bufR0 = !spec.buffers.rawR0.empty() ? spec.buffers.rawR0 : (spec.buffers.r0_max >= 1e8f ? L"0m 1e9" : L"0m");
            wchar_t bufB1[256];
            swprintf_s(bufB1, L"r0: %s  •  Centre: %.2f  •  Radius: %.2f  •  Angle: %.2f deg",
                bufR0.c_str(), spec.buffers.centre, spec.buffers.radius, spec.buffers.angleDeg);
            card.rows.push_back({ L"Buffers", bufB1 });

            std::wstring bufStif = !spec.buffers.rawStiffness.empty() ? spec.buffers.rawStiffness : L"5e6N/m 5e6N/m";
            std::wstring bufDamp = !spec.buffers.rawDamping.empty() ? spec.buffers.rawDamping : L"1e6N/m/s 1e6N/m/s";
            wchar_t bufB2[256];
            swprintf_s(bufB2, L"Stiffness: %s  •  Damping: %s", bufStif.c_str(), bufDamp.c_str());
            card.rows.push_back({ L"  ↳ Buffer Dynamics", bufB2, StockInfoTheme::TextSecondary });
        }
        pState->cards.push_back(card);
    }

    // 5. PERFORMANCE & SUBSYSTEMS
    {
        SectionCard card;
        card.title = L"Performance & Subsystems";
        card.iconGlyph = L"\xE7E8"; // Engine/Performance
        if (spec.massKg > 0.0f)
        {
            wchar_t buf[128];
            swprintf_s(buf, L"%.1f tons (%d kg)", spec.massKg / 1000.0f, (int)spec.massKg);
            card.rows.push_back({ L"Mass", buf });
        }
        if (spec.maxPowerKw > 0.0f)
        {
            wchar_t buf[128];
            swprintf_s(buf, L"%.0f kW (%d HP)", spec.maxPowerKw, (int)(spec.maxPowerKw * 1.34102f));
            card.rows.push_back({ L"Max Power", buf });
        }
        if (spec.maxForceKn > 0.0f)
        {
            wchar_t buf[128];
            swprintf_s(buf, L"%.1f kN", spec.maxForceKn);
            card.rows.push_back({ L"Max Force", buf });
        }
        if (spec.maxVelocityKmh > 0.0f)
        {
            wchar_t buf[128];
            swprintf_s(buf, L"%.1f km/h (%d mph)", spec.maxVelocityKmh, (int)(spec.maxVelocityKmh * 0.621371f));
            card.rows.push_back({ L"Max Velocity", buf });
        }
        if (!spec.cabViewFile.empty())
        {
            card.rows.push_back({ L"CabView (.cvf)", spec.cabViewFile });
        }
        pState->cards.push_back(card);
    }

    // 6. RESOLVED INCLUDE (.INC) CHAIN
    {
        SectionCard card;
        card.title = L"Resolved Include (.inc) Files";
        card.iconGlyph = L"\xE838"; // List/Folder
        if (!spec.resolvedIncludes.empty())
        {
            for (const auto& inc : spec.resolvedIncludes)
            {
                card.rows.push_back({ L"[OK]", inc, StockInfoTheme::StatusValid });
            }
        }
        else
        {
            card.rows.push_back({ L"Include Files", L"No external .inc files included." });
        }
        if (!spec.missingIncludes.empty())
        {
            for (const auto& inc : spec.missingIncludes)
            {
                card.rows.push_back({ L"[NOT FOUND]", inc, StockInfoTheme::StatusMissing });
            }
        }
        pState->cards.push_back(card);
    }
}

static std::wstring BuildClipboardText(const StockSpecReader::StockSpec& spec)
{
    std::wstringstream ss;
    ss << L"================================================================================\r\n";
    ss << L"  ROLLING STOCK SPECIFICATION & OPEN RAILS PLACEMENT DIAGNOSTICS\r\n";
    ss << L"================================================================================\r\n\r\n";
    ss << L"[ IDENTIFICATION ]\r\n";
    ss << L"  Display Name   : " << spec.displayName << L"\r\n";
    ss << L"  File Name      : " << spec.fileName << L"\r\n";
    ss << L"  Trainset Folder: " << spec.folderName << L"\r\n";
    std::wstring typeStr = (spec.extension == L".eng") ? (L"Engine (" + (spec.category.empty() ? L"Unresolved" : spec.category) + L")") : (L"Wagon (" + (spec.category.empty() ? L"Unresolved" : spec.category) + L")");
    ss << L"  Type           : " << typeStr << L"\r\n";
    ss << L"  File Path      : " << spec.filePath << L"\r\n\r\n";
    ss << L"[ PHYSICAL DIMENSIONS & CONSIST SPACING ]\r\n";
    ss << L"  Width          : " << std::fixed << std::setprecision(2) << spec.size.widthM << L" m\r\n";
    ss << L"  Height         : " << std::fixed << std::setprecision(2) << spec.size.heightM << L" m\r\n";
    ss << L"  Length         : " << std::fixed << std::setprecision(2) << spec.size.lengthM << L" m\r\n";
    ss << L"  Coupler Zero (r0): " << std::fixed << std::setprecision(3) << spec.GetCouplerZeroLengthM() << L" m (" << (spec.GetCouplerZeroLengthM() * 100.0f) << L" cm)\r\n";
    ss << L"  Consist Placement Step (Length + r0): " << std::fixed << std::setprecision(3) << spec.GetEffectivePlacementStepM() << L" m\r\n\r\n";
    ss << L"[ 3D VISUAL SHAPES ]\r\n";
    ss << L"  Primary Shape (.s) : " << (spec.mainShapeFile.empty() ? L"None" : spec.mainShapeFile) << L"\r\n";
    ss << L"  Shape Status       : " << (spec.shapeExistsOnDisk ? L"VALID (Found on disk)" : L"MISSING / UNRESOLVED!") << L"\r\n";
    if (!spec.fullShapePath.empty()) ss << L"  Shape Full Path    : " << spec.fullShapePath << L"\r\n";
    if (!spec.freightAnims.empty())
    {
        for (size_t i = 0; i < spec.freightAnims.size(); ++i)
        {
            const auto& fa = spec.freightAnims[i];
            ss << L"  Freight Anim #" << (i + 1) << L": " << fa.shapePath << (fa.existsOnDisk ? L" [VALID]" : L" [NOT FOUND]") << L"\r\n";
            if (!fa.fullPath.empty()) ss << L"    Path: " << fa.fullPath << L"\r\n";
        }
    }
    ss << L"\r\n";
    ss << L"[ COUPLING & BUFFERS ]\r\n";
    for (size_t i = 0; i < spec.couplers.size(); ++i)
    {
        const auto& c = spec.couplers[i];
        std::wstring r0Str = !c.rawR0.empty() ? c.rawR0 : (c.r0_max >= 1e8f ? L"0cm 1e9" : (std::to_wstring(c.r0_min).substr(0, 4) + L"m"));
        std::wstring breakStr = !c.rawBreak.empty() ? c.rawBreak : (c.break1 >= 1e8f ? L"1e9" : (std::to_wstring((int)(c.break1 / 1000.0f)) + L"kN"));
        std::wstring stifStr = !c.rawStiffness.empty() ? c.rawStiffness : L"5e6N/m 0";
        std::wstring dampStr = !c.rawDamping.empty() ? c.rawDamping : L"1e6N/m/s 0";
        std::wstring velStr = !c.rawVelocity.empty() ? c.rawVelocity : L"0.15m/s";

        ss << L"  Coupler #" << (i + 1) << L": Type: " << c.type << L" • r0: " << r0Str << L" • Break: " << breakStr << L" • Rigid: " << (c.hasRigidConnection ? L"Yes" : L"No") << L"\r\n";
        ss << L"    Dynamics: Stiffness: " << stifStr << L" • Damping: " << dampStr << L" • Velocity: " << velStr << L"\r\n";
    }
    if (spec.buffers.exists)
    {
        std::wstring bufR0 = !spec.buffers.rawR0.empty() ? spec.buffers.rawR0 : (spec.buffers.r0_max >= 1e8f ? L"0m 1e9" : L"0m");
        std::wstring bufStif = !spec.buffers.rawStiffness.empty() ? spec.buffers.rawStiffness : L"5e6N/m 5e6N/m";
        std::wstring bufDamp = !spec.buffers.rawDamping.empty() ? spec.buffers.rawDamping : L"1e6N/m/s 1e6N/m/s";
        ss << L"  Buffers: r0: " << bufR0 << L" • Centre: " << spec.buffers.centre << L" • Radius: " << spec.buffers.radius << L" • Angle: " << spec.buffers.angleDeg << L" deg\r\n";
        ss << L"    Dynamics: Stiffness: " << bufStif << L" • Damping: " << bufDamp << L"\r\n";
    }
    ss << L"\r\n[ PERFORMANCE ]\r\n";
    ss << L"  Mass: " << (spec.massKg / 1000.0f) << L" tons | Power: " << spec.maxPowerKw << L" kW | Max Speed: " << spec.maxVelocityKmh << L" km/h\r\n";
    ss << L"\r\n[ RESOLVED INCLUDES ]\r\n";
    for (const auto& inc : spec.resolvedIncludes) ss << L"  [OK] " << inc << L"\r\n";
    for (const auto& inc : spec.missingIncludes) ss << L"  [MISSING] " << inc << L"\r\n";
    return ss.str();
}

static bool HitTestRow(StockInfoDialogState* pState, int clientW, int clientH, POINT pt, std::wstring& outLabel, std::wstring& outValue, RECT& outRowRc, bool& outIsTruncated)
{
    if (!pState) return false;
    int dpi = GetDpiForSystem();
    int headerH = MulDiv(58, dpi, 96);
    int footerH = MulDiv(52, dpi, 96);
    int padX = MulDiv(18, dpi, 96);

    if (pt.y <= headerH || pt.y >= clientH - footerH) return false;

    int contentY = headerH + MulDiv(14, dpi, 96) - pState->scrollOffsetY;
    int cardMarginY = MulDiv(12, dpi, 96);
    int cardWidth = clientW - padX * 2 - (pState->scrollBar.IsVisible() ? MulDiv(14, dpi, 96) : 0);
    int rowH = MulDiv(22, dpi, 96);

    for (const auto& card : pState->cards)
    {
        int cardHeaderH = MulDiv(30, dpi, 96);
        int rowsH = (int)card.rows.size() * rowH + MulDiv(14, dpi, 96);
        int cardH = cardHeaderH + rowsH;

        if (contentY + cardH >= headerH && contentY <= clientH - footerH)
        {
            int rowY = contentY + cardHeaderH + MulDiv(8, dpi, 96);
            for (const auto& row : card.rows)
            {
                RECT rcRow = { padX + 4, rowY, padX + cardWidth - 4, rowY + rowH };
                if (PtInRect(&rcRow, pt))
                {
                    outLabel = row.label;
                    outValue = row.value;
                    outRowRc = rcRow;
                    outIsTruncated = (row.value.find(L'\\') != std::wstring::npos || row.value.find(L'/') != std::wstring::npos || row.value.length() > 30);
                    return true;
                }
                rowY += rowH;
            }
        }
        contentY += cardH + cardMarginY;
    }
    return false;
}

static void DrawCard(HDC hdc, const RECT& rcCard, const SectionCard& card, StockInfoDialogState* pState)
{
    // 1. Card Body Background & Border
    HBRUSH hbrCard = CreateSolidBrush(StockInfoTheme::CardBackground);
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, StockInfoTheme::CardBorder);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hbrCard);
    HPEN hOldP = (HPEN)SelectObject(hdc, hPenBorder);

    RoundRect(hdc, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);

    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hbrCard);
    DeleteObject(hPenBorder);

    // 2. Card Header
    int headerH = MulDiv(30, GetDpiForSystem(), 96);
    RECT rcHeader = { rcCard.left + 1, rcCard.top + 1, rcCard.right - 1, rcCard.top + headerH };
    HBRUSH hbrHeader = CreateSolidBrush(StockInfoTheme::CardHeaderBg);
    FillRect(hdc, &rcHeader, hbrHeader);
    DeleteObject(hbrHeader);

    // Header Divider Line
    HPEN hPenLine = CreatePen(PS_SOLID, 1, StockInfoTheme::CardBorder);
    HPEN hOldP2 = (HPEN)SelectObject(hdc, hPenLine);
    MoveToEx(hdc, rcCard.left, rcCard.top + headerH, NULL);
    LineTo(hdc, rcCard.right, rcCard.top + headerH);
    SelectObject(hdc, hOldP2);
    DeleteObject(hPenLine);

    // Header Icon & Title
    int padX = MulDiv(12, GetDpiForSystem(), 96);
    if (pState->hFontIconSmall)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, pState->hFontIconSmall);
        SetTextColor(hdc, StockInfoTheme::AccentBlue);
        RECT rcIcon = { rcHeader.left + padX, rcHeader.top, rcHeader.left + padX + 20, rcHeader.bottom };
        DrawTextW(hdc, card.iconGlyph, -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldF);
    }
    if (pState->hFontCardTitle)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, pState->hFontCardTitle);
        SetTextColor(hdc, StockInfoTheme::TextPrimary);
        RECT rcTitle = { rcHeader.left + padX + 24, rcHeader.top, rcHeader.right - padX, rcHeader.bottom };
        DrawTextW(hdc, card.title.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hOldF);
    }

    // 3. Card Rows
    int rowY = rcCard.top + headerH + MulDiv(8, GetDpiForSystem(), 96);
    int labelW = MulDiv(190, GetDpiForSystem(), 96);
    int rowH = MulDiv(22, GetDpiForSystem(), 96);

    for (const auto& row : card.rows)
    {
        RECT rcRow = { rcCard.left + 4, rowY, rcCard.right - 4, rowY + rowH };
        bool isRowHovered = (pState->isHoveringAnyRow && pState->hoveredValue == row.value && pState->hoveredLabel == row.label);

        if (isRowHovered)
        {
            HBRUSH hbrRowHover = CreateSolidBrush(RGB(38, 38, 44));
            HPEN hPenRowHover = CreatePen(PS_SOLID, 1, RGB(55, 55, 65));
            HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hbrRowHover);
            HPEN hOldP = (HPEN)SelectObject(hdc, hPenRowHover);
            RoundRect(hdc, rcRow.left, rcRow.top, rcRow.right, rcRow.bottom, 4, 4);
            SelectObject(hdc, hOldB);
            SelectObject(hdc, hOldP);
            DeleteObject(hbrRowHover);
            DeleteObject(hPenRowHover);
        }

        RECT rcLabel = { rcCard.left + padX, rowY, rcCard.left + padX + labelW, rowY + rowH };
        RECT rcVal = { rcCard.left + padX + labelW + 8, rowY, rcCard.right - padX, rowY + rowH };

        if (pState->hFontMain)
        {
            HFONT hOldF = (HFONT)SelectObject(hdc, pState->hFontMain);
            SetTextColor(hdc, isRowHovered ? RGB(220, 220, 220) : StockInfoTheme::TextMuted);
            DrawTextW(hdc, row.label.c_str(), -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(hdc, isRowHovered ? RGB(255, 255, 255) : row.valColor);
            DrawTextW(hdc, row.value.c_str(), -1, &rcVal, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
            SelectObject(hdc, hOldF);
        }
        rowY += rowH;
    }
}

static void DrawFluentButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHover, bool isPress, bool isAccent, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* iconGlyph = NULL)
{
    COLORREF bgCol, borderCol, textCol;
    if (isAccent)
    {
        bgCol = isPress ? StockInfoTheme::AccentPressed : (isHover ? StockInfoTheme::AccentHover : StockInfoTheme::AccentBlue);
        borderCol = bgCol;
        textCol = RGB(255, 255, 255);
    }
    else
    {
        bgCol = isPress ? RGB(32, 32, 32) : (isHover ? RGB(48, 48, 48) : RGB(36, 36, 36));
        borderCol = isHover ? RGB(80, 80, 80) : RGB(58, 58, 58);
        textCol = StockInfoTheme::TextPrimary;
    }

    HBRUSH hbr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hbr);
    HPEN hOldP = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hbr);
    DeleteObject(hPen);

    RECT rcText = rc;
    if (iconGlyph && hIconFont)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, hIconFont);
        SetTextColor(hdc, textCol);
        RECT rcIcon = { rc.left + 12, rc.top, rc.left + 32, rc.bottom };
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldF);
        rcText.left += 26;
    }

    if (hFont)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, hFont);
        SetTextColor(hdc, textCol);
        DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hOldF);
    }
}

static void RestoreParentWindowFocus(HWND hParent)
{
    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        if (IsIconic(hParent))
        {
            ShowWindow(hParent, SW_RESTORE);
        }
        SetWindowPos(hParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hParent);
        SetActiveWindow(hParent);
        BringWindowToTop(hParent);
        SetFocus(hParent);
    }
}

static LRESULT CALLBACK StockInfoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    StockInfoDialogState* pState = (StockInfoDialogState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (StockInfoDialogState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }
    case WM_CREATE:
    {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        pState->hFontTitle = CreateDpiFont(13, FW_SEMIBOLD, L"Segoe UI Variable Display");
        pState->hFontSubTitle = CreateDpiFont(10, FW_NORMAL, L"Segoe UI Variable Text");
        pState->hFontCardTitle = CreateDpiFont(10, FW_SEMIBOLD, L"Segoe UI Variable Text");
        pState->hFontMain = CreateDpiFont(10, FW_NORMAL, L"Segoe UI Variable Text");
        pState->hFontBold = CreateDpiFont(10, FW_SEMIBOLD, L"Segoe UI Variable Text");
        pState->hFontMono = CreateDpiFont(9, FW_NORMAL, L"Consolas");
        pState->hFontIcon = CreateDpiFont(16, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon) pState->hFontIcon = CreateDpiFont(16, FW_NORMAL, L"Segoe MDL2 Assets");
        pState->hFontIconSmall = CreateDpiFont(12, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIconSmall) pState->hFontIconSmall = CreateDpiFont(12, FW_NORMAL, L"Segoe MDL2 Assets");

        BuildCardsFromSpec(pState);

        pState->scrollBar.SetAutoHide(true);
        pState->scrollBar.SetThumbColor(RGB(80, 80, 80), RGB(120, 120, 120), RGB(160, 160, 160));
        pState->scrollBar.SetGutterColor(StockInfoTheme::Background);

        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        if (!pState) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // 1. Fill Surface
        HBRUSH hbrBg = CreateSolidBrush(StockInfoTheme::Background);
        FillRect(hMemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hMemDC, TRANSPARENT);

        int dpi = GetDpiForSystem();
        int headerH = MulDiv(58, dpi, 96);
        int footerH = MulDiv(52, dpi, 96);
        int padX = MulDiv(18, dpi, 96);

        // 2. Top Header Bar
        RECT rcHeader = { 0, 0, w, headerH };
        HBRUSH hbrHead = CreateSolidBrush(StockInfoTheme::HeaderBackground);
        FillRect(hMemDC, &rcHeader, hbrHead);
        DeleteObject(hbrHead);

        HPEN hPenHead = CreatePen(PS_SOLID, 1, StockInfoTheme::BorderLine);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenHead);
        MoveToEx(hMemDC, 0, headerH, NULL);
        LineTo(hMemDC, w, headerH);
        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPenHead);

        // Header Icon
        if (pState->hFontIcon)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcon);
            SetTextColor(hMemDC, RGB(96, 205, 255));
            RECT rcIcon = { padX, 0, padX + 28, headerH };
            DrawTextW(hMemDC, L"\xE946", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hMemDC, hOldF);
        }

        // Header Title & Subtitle
        if (pState->hFontTitle)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontTitle);
            SetTextColor(hMemDC, RGB(255, 255, 255));
            RECT rcTitle = { padX + 32, MulDiv(10, dpi, 96), w - 80, MulDiv(30, dpi, 96) };
            DrawTextW(hMemDC, L"Stock Unit Specifications & Diagnostics", -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hMemDC, hOldF);
        }

        if (pState->hFontSubTitle)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontSubTitle);
            SetTextColor(hMemDC, StockInfoTheme::TextSecondary);
            std::wstring subStr = pState->spec.fileName + L"  •  " + pState->spec.folderName + L"  •  " + pState->spec.category;
            RECT rcSub = { padX + 32, MulDiv(32, dpi, 96), w - 80, MulDiv(50, dpi, 96) };
            DrawTextW(hMemDC, subStr.c_str(), -1, &rcSub, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hMemDC, hOldF);
        }

        // Top Close X Button (Fluent styled with margins and rounded rect)
        int closeBtnSize = MulDiv(30, dpi, 96);
        int closeMarginRight = MulDiv(14, dpi, 96);
        int closeTop = (headerH - closeBtnSize) / 2;
        pState->rcBtnTopClose = { w - closeMarginRight - closeBtnSize, closeTop, w - closeMarginRight, closeTop + closeBtnSize };

        if (pState->isHoverTopClose)
        {
            COLORREF bgClose = pState->isPressTopClose ? RGB(160, 30, 30) : RGB(196, 43, 28);
            HBRUSH hbrClose = CreateSolidBrush(bgClose);
            HPEN hPenClose = CreatePen(PS_SOLID, 1, bgClose);
            HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrClose);
            HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenClose);
            RoundRect(hMemDC, pState->rcBtnTopClose.left, pState->rcBtnTopClose.top, pState->rcBtnTopClose.right, pState->rcBtnTopClose.bottom, 6, 6);
            SelectObject(hMemDC, hOldB);
            SelectObject(hMemDC, hOldP);
            DeleteObject(hbrClose);
            DeleteObject(hPenClose);
        }

        if (pState->hFontIconSmall)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIconSmall);
            SetTextColor(hMemDC, pState->isHoverTopClose ? RGB(255, 255, 255) : StockInfoTheme::TextSecondary);
            DrawTextW(hMemDC, L"\xE711", -1, &pState->rcBtnTopClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hMemDC, hOldF);
        }

        // 3. Scrollable Cards Body
        int contentY = headerH + MulDiv(14, dpi, 96) - pState->scrollOffsetY;
        int cardMarginY = MulDiv(12, dpi, 96);
        int cardWidth = w - padX * 2 - (pState->scrollBar.IsVisible() ? MulDiv(14, dpi, 96) : 0);

        // Clip Content Area between Header and Footer
        HRGN hRgnClip = CreateRectRgn(0, headerH + 1, w, h - footerH);
        SelectClipRgn(hMemDC, hRgnClip);

        int totalCalcH = MulDiv(14, dpi, 96);

        for (auto& card : pState->cards)
        {
            int cardHeaderH = MulDiv(30, dpi, 96);
            int rowsH = (int)card.rows.size() * MulDiv(22, dpi, 96) + MulDiv(14, dpi, 96);
            int cardH = cardHeaderH + rowsH;
            card.calculatedHeight = cardH;

            RECT rcCard = { padX, contentY, padX + cardWidth, contentY + cardH };
            if (rcCard.bottom >= headerH && rcCard.top <= h - footerH)
            {
                DrawCard(hMemDC, rcCard, card, pState);
            }

            contentY += cardH + cardMarginY;
            totalCalcH += cardH + cardMarginY;
        }

        pState->totalContentHeight = totalCalcH;

        // Remove Clip
        SelectClipRgn(hMemDC, NULL);
        DeleteObject(hRgnClip);

        // 4. Custom Scrollbar
        int viewAreaH = h - headerH - footerH;
        RECT rcScrollBounds = { w - MulDiv(12, dpi, 96), headerH + 1, w - 2, h - footerH };
        pState->scrollBar.SetBounds(rcScrollBounds);
        pState->scrollBar.SetRange(0, pState->totalContentHeight, viewAreaH);
        pState->scrollBar.SetVisible(pState->totalContentHeight > viewAreaH);
        if (pState->scrollBar.IsVisible())
        {
            pState->scrollBar.Paint(hMemDC, StockInfoTheme::Background, StockInfoTheme::Background);
        }

        // 5. Bottom Footer Bar
        RECT rcFooter = { 0, h - footerH, w, h };
        HBRUSH hbrFoot = CreateSolidBrush(StockInfoTheme::FooterBackground);
        FillRect(hMemDC, &rcFooter, hbrFoot);
        DeleteObject(hbrFoot);

        HPEN hPenFoot = CreatePen(PS_SOLID, 1, StockInfoTheme::BorderLine);
        HPEN hOldP3 = (HPEN)SelectObject(hMemDC, hPenFoot);
        MoveToEx(hMemDC, 0, h - footerH, NULL);
        LineTo(hMemDC, w, h - footerH);
        SelectObject(hMemDC, hOldP3);
        DeleteObject(hPenFoot);

        int btnH = MulDiv(32, dpi, 96);
        int btnY = h - footerH + (footerH - btnH) / 2;
        int copyW = MulDiv(240, dpi, 96);
        int closeW = MulDiv(110, dpi, 96);

        pState->rcBtnCopy = { padX, btnY, padX + copyW, btnY + btnH };
        pState->rcBtnClose = { w - padX - closeW, btnY, w - padX, btnY + btnH };

        DrawFluentButton(hMemDC, pState->rcBtnCopy, L"Copy Spec to Clipboard", pState->isHoverCopy, pState->isPressCopy, true, pState->hFontBold, pState->hFontIconSmall, L"\xE8C8");
        DrawFluentButton(hMemDC, pState->rcBtnClose, L"Close", pState->isHoverClose, pState->isPressClose, false, pState->hFontMain);

        // 6. 1px Outer Window Border
        HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldP4 = (HPEN)SelectObject(hMemDC, hPenBorder);
        HBRUSH hOldB4 = (HBRUSH)SelectObject(hMemDC, hNull);
        Rectangle(hMemDC, 0, 0, w, h);
        SelectObject(hMemDC, hOldB4);
        SelectObject(hMemDC, hOldP4);
        DeleteObject(hPenBorder);

        // 7. Toast Notification / Truncated Path Overlay
        DWORD now = GetTickCount();
        bool showActionToast = (!pState->toastMessage.empty() && now < pState->toastExpireTick);
        bool showHoverToast = (!showActionToast && pState->isHoveringTruncatedRow && !pState->hoveredValue.empty());

        if (showActionToast || showHoverToast)
        {
            std::wstring toastTitle = showActionToast ? L"Notification" : (pState->hoveredLabel + L"  •  Click to copy full text");
            std::wstring toastBody = showActionToast ? pState->toastMessage : pState->hoveredValue;
            const wchar_t* toastIcon = showActionToast ? L"\xE73E" : L"\xE8C8";
            COLORREF accentToast = showActionToast ? RGB(80, 200, 120) : StockInfoTheme::AccentBlue;
            int maxW = MulDiv(800, dpi, 96);
            int curW = w - MulDiv(36, dpi, 96);
            int toastW = (curW < maxW) ? curW : maxW;
            int toastH = MulDiv(46, dpi, 96);
            int toastX = (w - toastW) / 2;
            int toastY = h - footerH - toastH - MulDiv(10, dpi, 96);

            RECT rcToast = { toastX, toastY, toastX + toastW, toastY + toastH };

            HBRUSH hbrToastBg = CreateSolidBrush(RGB(28, 28, 34));
            HPEN hPenToastBorder = CreatePen(PS_SOLID, 1, showActionToast ? RGB(80, 200, 120) : RGB(65, 75, 90));
            HBRUSH hOldBToast = (HBRUSH)SelectObject(hMemDC, hbrToastBg);
            HPEN hOldPToast = (HPEN)SelectObject(hMemDC, hPenToastBorder);

            RoundRect(hMemDC, rcToast.left, rcToast.top, rcToast.right, rcToast.bottom, 8, 8);

            SelectObject(hMemDC, hOldBToast);
            SelectObject(hMemDC, hOldPToast);
            DeleteObject(hbrToastBg);
            DeleteObject(hPenToastBorder);

            if (pState->hFontIconSmall)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIconSmall);
                SetTextColor(hMemDC, accentToast);
                RECT rcIcon = { rcToast.left + MulDiv(10, dpi, 96), rcToast.top, rcToast.left + MulDiv(32, dpi, 96), rcToast.bottom };
                DrawTextW(hMemDC, toastIcon, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hMemDC, hOldF);
            }

            if (pState->hFontSubTitle)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontSubTitle);
                SetTextColor(hMemDC, accentToast);
                RECT rcTTitle = { rcToast.left + MulDiv(36, dpi, 96), rcToast.top + MulDiv(5, dpi, 96), rcToast.right - MulDiv(12, dpi, 96), rcToast.top + MulDiv(20, dpi, 96) };
                DrawTextW(hMemDC, toastTitle.c_str(), -1, &rcTTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hMemDC, hOldF);
            }

            if (pState->hFontMono)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontMono);
                SetTextColor(hMemDC, RGB(245, 245, 245));
                RECT rcTBody = { rcToast.left + MulDiv(36, dpi, 96), rcToast.top + MulDiv(22, dpi, 96), rcToast.right - MulDiv(12, dpi, 96), rcToast.bottom - MulDiv(4, dpi, 96) };
                DrawTextW(hMemDC, toastBody.c_str(), -1, &rcTBody, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
                SelectObject(hMemDC, hOldF);
            }
        }

        BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int step = MulDiv(40, GetDpiForSystem(), 96);
        int maxScroll = pState->scrollBar.GetMaxScrollPos();

        if (delta > 0) pState->scrollOffsetY -= step;
        else pState->scrollOffsetY += step;

        pState->scrollOffsetY = std::clamp(pState->scrollOffsetY, 0, maxScroll);
        pState->scrollBar.SetPos(pState->scrollOffsetY);
        pState->scrollBar.TriggerActivity(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->isDraggingWindow)
        {
            POINT ptScreen;
            GetCursorPos(&ptScreen);
            int newX = pState->dragStartWindow.x + (ptScreen.x - pState->dragStartCursor.x);
            int newY = pState->dragStartWindow.y + (ptScreen.y - pState->dragStartCursor.y);
            SetWindowPos(hWnd, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        }

        bool needRedraw = false;

        // Custom Scrollbar hover & drag
        if (pState->scrollBar.IsVisible())
        {
            if (pState->scrollBar.OnMouseMove(pt, hWnd))
            {
                pState->scrollOffsetY = pState->scrollBar.GetPos();
                needRedraw = true;
            }
        }

        bool hCopy = PtInRect(&pState->rcBtnCopy, pt);
        bool hClose = PtInRect(&pState->rcBtnClose, pt);
        bool hTopClose = PtInRect(&pState->rcBtnTopClose, pt);

        if (hCopy != pState->isHoverCopy || hClose != pState->isHoverClose || hTopClose != pState->isHoverTopClose)
        {
            pState->isHoverCopy = hCopy;
            pState->isHoverClose = hClose;
            pState->isHoverTopClose = hTopClose;
            needRedraw = true;
        }

        // Row hit test
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        std::wstring hLabel, hVal;
        RECT rcRow;
        bool isTrunc = false;
        bool hit = HitTestRow(pState, rcClient.right, rcClient.bottom, pt, hLabel, hVal, rcRow, isTrunc);

        if (hit != pState->isHoveringAnyRow || (hit && hVal != pState->hoveredValue))
        {
            pState->isHoveringAnyRow = hit;
            pState->isHoveringTruncatedRow = hit && isTrunc;
            pState->hoveredLabel = hLabel;
            pState->hoveredValue = hVal;
            needRedraw = true;
        }

        if (needRedraw) InvalidateRect(hWnd, NULL, FALSE);

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            pState->isHoverCopy = false;
            pState->isHoverClose = false;
            pState->isHoverTopClose = false;
            pState->isHoveringAnyRow = false;
            pState->isHoveringTruncatedRow = false;
            pState->hoveredValue.clear();
            pState->hoveredLabel.clear();
            pState->scrollBar.OnMouseLeave(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->scrollBar.IsVisible() && pState->scrollBar.OnLButtonDown(pt, hWnd))
        {
            pState->scrollOffsetY = pState->scrollBar.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (PtInRect(&pState->rcBtnCopy, pt))
        {
            pState->isPressCopy = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBtnClose, pt))
        {
            pState->isPressClose = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBtnTopClose, pt))
        {
            pState->isPressTopClose = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Clicking a row copies its full text
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        std::wstring clickedLabel, clickedValue;
        RECT rcRow;
        bool isTrunc = false;
        if (HitTestRow(pState, rcClient.right, rcClient.bottom, pt, clickedLabel, clickedValue, rcRow, isTrunc))
        {
            if (OpenClipboard(hWnd))
            {
                EmptyClipboard();
                size_t bytes = (clickedValue.size() + 1) * sizeof(wchar_t);
                HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hGlob)
                {
                    memcpy(GlobalLock(hGlob), clickedValue.c_str(), bytes);
                    GlobalUnlock(hGlob);
                    SetClipboardData(CF_UNICODETEXT, hGlob);
                }
                CloseClipboard();
            }

            pState->toastMessage = L"Copied to clipboard: " + clickedValue;
            pState->toastExpireTick = GetTickCount() + 2500;
            SetTimer(hWnd, 2002, 100, NULL);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Click on Header area initiates window dragging (excluding close button)
        int dpi = GetDpiForSystem();
        int headerH = MulDiv(58, dpi, 96);
        if (y < headerH && !PtInRect(&pState->rcBtnTopClose, pt))
        {
            pState->isDraggingWindow = true;
            GetCursorPos(&pState->dragStartCursor);
            RECT rcWnd;
            GetWindowRect(hWnd, &rcWnd);
            pState->dragStartWindow = { rcWnd.left, rcWnd.top };
            SetCapture(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->isDraggingWindow)
        {
            pState->isDraggingWindow = false;
            ReleaseCapture();
            return 0;
        }

        if (pState->scrollBar.IsVisible() && pState->scrollBar.OnLButtonUp(pt, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->isPressCopy)
        {
            pState->isPressCopy = false;
            ReleaseCapture();
            if (PtInRect(&pState->rcBtnCopy, pt))
            {
                std::wstring clipText = BuildClipboardText(pState->spec);
                if (OpenClipboard(hWnd))
                {
                    EmptyClipboard();
                    size_t bytes = (clipText.size() + 1) * sizeof(wchar_t);
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (hGlob)
                    {
                        memcpy(GlobalLock(hGlob), clipText.c_str(), bytes);
                        GlobalUnlock(hGlob);
                        SetClipboardData(CF_UNICODETEXT, hGlob);
                    }
                    CloseClipboard();
                }

                pState->toastMessage = L"All specifications copied to clipboard!";
                pState->toastExpireTick = GetTickCount() + 2500;
                SetTimer(hWnd, 2002, 100, NULL);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->isPressClose)
        {
            pState->isPressClose = false;
            ReleaseCapture();
            if (PtInRect(&pState->rcBtnClose, pt))
            {
                RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
                DestroyWindow(hWnd);
                return 0;
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->isPressTopClose)
        {
            pState->isPressTopClose = false;
            ReleaseCapture();
            if (PtInRect(&pState->rcBtnTopClose, pt))
            {
                RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
                DestroyWindow(hWnd);
                return 0;
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        return 0;
    }
    case WM_TIMER:
    {
        if (!pState) break;
        if (wParam == 2002)
        {
            if (GetTickCount() >= pState->toastExpireTick)
            {
                KillTimer(hWnd, 2002);
                pState->toastMessage.clear();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        if (pState->scrollBar.OnTimer(hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
    {
        RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
        DestroyWindow(hWnd);
        return 0;
    }
    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
    {
        if (pState)
        {
            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontSubTitle) DeleteObject(pState->hFontSubTitle);
            if (pState->hFontCardTitle) DeleteObject(pState->hFontCardTitle);
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontMono) DeleteObject(pState->hFontMono);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
            if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);

            delete pState;
        }
        return 0;
    }
    case WM_NCDESTROY:
    {
        HWND hOwner = GetWindow(hWnd, GW_OWNER);
        if (hOwner && IsWindow(hOwner))
        {
            RestoreParentWindowFocus(hOwner);
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ShowStockInfoDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath)
{
    static bool s_ClassRegistered = false;
    HINSTANCE hInstance = GetModuleHandle(NULL);

    if (!s_ClassRegistered)
    {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = StockInfoWndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = NULL;
        wcex.lpszClassName = L"StockInfoDialogClass";
        RegisterClassExW(&wcex);
        s_ClassRegistered = true;
    }

    StockInfoDialogState* pState = new StockInfoDialogState();
    pState->hParent = hWndParent;
    pState->basePath = basePath;
    pState->spec = StockSpecReader::ReadFullSpec(filePath, basePath);

    int dpi = GetDpiForSystem();
    int width = MulDiv(880, dpi, 96);
    int height = MulDiv(640, dpi, 96);

    RECT rcParent;
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }
    int x = rcParent.left + (rcParent.right - rcParent.left - width) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - height) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"StockInfoDialogClass",
        L"Stock Specification Inspector",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, width, height,
        hWndParent, NULL, hInstance, pState);

    if (hWnd)
    {
        // DWM Rounded Corners & Dark Mode
        DWORD corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)33, &corner, sizeof(corner));
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)20, &useDark, sizeof(useDark));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
        COLORREF borderColor = RGB(55, 55, 62);
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        MARGINS margins = { 0, 0, 0, 0 };
        DwmExtendFrameIntoClientArea(hWnd, &margins);

        if (hWndParent && IsWindow(hWndParent))
        {
            EnableWindow(hWndParent, FALSE);
        }

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
        SetFocus(hWnd);
    }
    else
    {
        delete pState;
    }
}
