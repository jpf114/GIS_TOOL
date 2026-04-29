#pragma once

#include <QString>

namespace gis::style {

namespace Color {

constexpr const char* kWindowBg          = "#E8ECF1";
constexpr const char* kPagePanelBg       = "#F5F6F8";
constexpr const char* kCardBg            = "#FFFFFF";
constexpr const char* kCardBorder        = "#E2E5EA";
constexpr const char* kCardShadow        = "rgba(15, 23, 42, 0.08)";
constexpr const char* kSidebarBg         = "#1E2A36";
constexpr const char* kSidebarPanel      = "#1E2A36";
constexpr const char* kSidebarSelected   = "#1E3A5F";
constexpr const char* kSidebarHover      = "#263544";
constexpr const char* kSidebarIndicator  = "#2E7EC9";
constexpr const char* kSidebarText       = "#BCC4CF";
constexpr const char* kSidebarMuted      = "#8B9099";
constexpr const char* kPrimary           = "#2E7EC9";
constexpr const char* kPrimaryHover      = "#1F6AB5";
constexpr const char* kPrimaryPressed    = "#1A5B98";
constexpr const char* kPrimaryLight      = "#E8F2FB";
constexpr const char* kPrimaryDeep       = "#1F6AB5";
constexpr const char* kSuccess           = "#1F9D68";
constexpr const char* kSuccessBg         = "#E8F7F0";
constexpr const char* kWarning           = "#D97706";
constexpr const char* kWarningBg         = "#FFF4DF";
constexpr const char* kError             = "#DC4C3E";
constexpr const char* kErrorBg           = "#FFF0EE";
constexpr const char* kTextPrimary       = "#15253A";
constexpr const char* kTextSecondary     = "#4F637A";
constexpr const char* kTextMuted         = "#7E92A8";
constexpr const char* kDivider           = "#E3EAF2";
constexpr const char* kInputBorder       = "#D3DDE8";
constexpr const char* kInputBg           = "#FDFEFF";
constexpr const char* kInputFocusBorder  = "#2F7CF6";
constexpr const char* kBrowseBtnBg       = "#F4F7FB";
constexpr const char* kBrowseBtnHover    = "#E8EEF6";
constexpr const char* kProgressTrack     = "#E7EEF7";
constexpr const char* kProgressFill      = "#2F7CF6";
constexpr const char* kDisabledBg        = "#E9EEF5";
constexpr const char* kDisabledText      = "#90A0B2";

}

namespace Size {

constexpr int kSidebarWidth         = 250;
constexpr int kSidebarMinWidth      = 240;
constexpr int kWindowMinWidth       = 1080;
constexpr int kWindowMinHeight      = 720;
constexpr int kWindowDefaultWidth   = 1380;
constexpr int kWindowDefaultHeight  = 880;
constexpr int kCardRadius           = 10;
constexpr int kCardPadding          = 18;
constexpr int kCardSpacing          = 16;
constexpr int kInputRadius          = 6;
constexpr int kInputMinHeight       = 36;
constexpr int kInputPaddingH        = 14;
constexpr int kButtonRadius         = 6;
constexpr int kButtonMinHeight      = 40;
constexpr int kButtonMinWidth       = 118;
constexpr int kProgressHeight       = 8;
constexpr int kProgressRadius       = 4;
constexpr int kLabelInputRatio      = 2;

}

inline QString globalStyleSheet() {
    return QString(
        "QMainWindow { background: %1; }"
        "QDialog { background: %3; }"
        "QWidget {"
        "  color: %2; font-size: 13px; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', sans-serif;"
        "}"
        "QWidget#pagePanel { background: %3; }"
        "QFrame#card, QFrame#heroCard, QFrame#execCard {"
        "  background: %4; border: 1px solid %5; border-radius: %6px;"
        "}"
        "QFrame#heroCard { background: %4; }"
        "QLabel#heroBadge {"
        "  color: %7; background: %8; border: 1px solid #D7E6F5; border-radius: 12px;"
        "  padding: 3px 10px; font-size: 11px; font-weight: 600;"
        "}"
        "QLabel#heroIconBadge {"
        "  min-width: 38px; max-width: 38px; min-height: 38px; max-height: 38px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #E8F2FB, stop:1 #D4E6F8);"
        "  border: 1px solid #D8E7F6; border-radius: 6px;"
        "  color: %7; font-size: 16px; font-weight: 700;"
        "}"
        "QLabel#heroTitle { font-size: 17px; font-weight: 700; color: %2; }"
        "QLabel#heroDesc { font-size: 12px; color: %9; line-height: 1.4; }"
        "QLabel#heroMeta { font-size: 12px; color: %10; }"
        "QLabel#cardTitle { font-size: 11px; font-weight: 700; color: %10; letter-spacing: 1px; }"
        "QLabel#cardIcon { color: %7; font-size: 14px; font-weight: 700; min-width: 16px; }"
        "QLabel#cardDesc { font-size: 12px; color: %9; }"
        "QFrame#accentBar { background: %7; border-radius: 2px; min-width: 3px; max-width: 3px; }"
        "QLabel#paramLabel { font-size: 13px; font-weight: 600; color: %2; }"
        "QLabel#paramKey {"
        "  font-size: 11px; color: %10; background: %8; border: 1px solid #D7E6FB;"
        "  border-radius: 999px; padding: 2px 8px;"
        "}"
        "QLabel#paramDesc { font-size: 12px; color: %9; }"
        "QLabel#requiredMark { color: %11; font-weight: 700; }"
        "QLabel#statusBadgeReady, QLabel#statusBadgeRunning, QLabel#statusBadgeSuccess,"
        "QLabel#statusBadgeWarning, QLabel#statusBadgeError {"
        "  padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: 600;"
        "}"
        "QLabel#statusBadgeReady { color: %9; background: #F1F5F9; }"
        "QLabel#statusBadgeRunning { color: %7; background: %8; }"
        "QLabel#statusBadgeSuccess { color: %12; background: %13; }"
        "QLabel#statusBadgeWarning { color: %14; background: %15; }"
        "QLabel#statusBadgeError { color: %11; background: %16; }"
        "QLabel#execSummary { font-size: 12px; color: %9; line-height: 1.5; }"
        "QLabel#statusBarLabel { color: %9; font-size: 12px; padding: 0 6px; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit {"
        "  min-height: %17px; border: 1px solid %18; border-radius: %19px;"
        "  background: %20; padding: 0 %21px; color: %2;"
        "  selection-background-color: %8; selection-color: %2;"
        "}"
        "QTextEdit { padding: 10px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus {"
        "  border-color: %22; background: white;"
        "}"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView {"
        "  border: 1px solid %18; border-radius: %19px; background: white;"
        "  color: %2; selection-background-color: %8; selection-color: %2; padding: 6px;"
        "}"
        "QPushButton#primaryButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2E7EC9, stop:1 #1F6AB5);"
        "  color: white; border: none; border-radius: %23px;"
        "  min-height: %24px; min-width: %25px; font-weight: 700; padding: 0 18px;"
        "}"
        "QPushButton#primaryButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3590DA, stop:1 #2575C0); }"
        "QPushButton#primaryButton:pressed { background: %27; }"
        "QPushButton#primaryButton:disabled { background: %28; color: %29; }"
        "QPushButton#secondaryButton {"
        "  background: %8; color: %7; border: 1px solid #D0E0FA; border-radius: %23px;"
        "  min-height: %24px; font-weight: 600; padding: 0 16px;"
        "}"
        "QPushButton#secondaryButton:hover { background: #DDEBFF; }"
        "QPushButton#secondaryButton:disabled { background: %28; color: %29; border-color: %28; }"
        "QPushButton#browseButton {"
        "  min-width: 72px; max-width: 88px; min-height: %17px;"
        "  border: 1px solid %18; border-radius: %19px;"
        "  background: white; color: %9; font-weight: 600; padding: 0 12px;"
        "}"
        "QPushButton#browseButton:hover { background: #F3F5F7; border-color: #C8CDD4; color: %2; }"
        "QPushButton#navItem::menu-indicator, QPushButton#subNavItem::menu-indicator { image: none; width: 0; }"
        "QCheckBox { color: %2; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid %18; background: white; }"
        "QCheckBox::indicator:checked { background: %7; border-color: %7; }"
        "QProgressBar {"
          "  border: none; border-radius: %32px; background: #E5E9EE; height: %34px; text-align: center;"
        "  color: %9; font-weight: 600; font-size: 11px;"
        "}"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #5CB8FF, stop:1 #2E7EC9); border-radius: %32px; }"
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #CBD6E2; border-radius: 5px; min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background: #AFBFCE; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: #CBD6E2; border-radius: 5px; min-width: 28px; }"
        "QScrollBar::handle:horizontal:hover { background: #AFBFCE; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QStatusBar { background: %4; border-top: 1px solid %5; color: %9; font-size: 12px; }"
        "QSplitter::handle { background: #DCE5EF; }")
        .arg(Color::kWindowBg)
        .arg(Color::kTextPrimary)
        .arg(Color::kPagePanelBg)
        .arg(Color::kCardBg)
        .arg(Color::kCardBorder)
        .arg(Size::kCardRadius)
        .arg(Color::kPrimary)
        .arg(Color::kPrimaryLight)
        .arg(Color::kTextSecondary)
        .arg(Color::kTextMuted)
        .arg(Color::kError)
        .arg(Color::kSuccess)
        .arg(Color::kSuccessBg)
        .arg(Color::kWarning)
        .arg(Color::kWarningBg)
        .arg(Color::kErrorBg)
        .arg(Size::kInputMinHeight)
        .arg(Color::kInputBorder)
        .arg(Size::kInputRadius)
        .arg(Color::kInputBg)
        .arg(Size::kInputPaddingH)
        .arg(Color::kInputFocusBorder)
        .arg(Size::kButtonRadius)
        .arg(Size::kButtonMinHeight)
        .arg(Size::kButtonMinWidth)
        .arg(Color::kPrimaryHover)
        .arg(Color::kPrimaryPressed)
        .arg(Color::kDisabledBg)
        .arg(Color::kDisabledText)
        .arg(Color::kBrowseBtnBg)
        .arg(Color::kBrowseBtnHover)
        .arg(Size::kProgressRadius)
        .arg(Color::kProgressTrack)
        .arg(Size::kProgressHeight)
        .arg(Color::kProgressFill);
}

inline QString sidebarStyleSheet() {
    return QString(
        "QFrame#sidebar { background: %1; border-right: 1px solid #1E3048; }"
        "QFrame#sidebarTopCard, QFrame#sidebarFooterCard {"
        "  background: transparent; border: none; border-radius: 0px;"
        "}"
        "QLabel#sidebarEyebrow { font-size: 11px; font-weight: 700; color: %3; letter-spacing: 1px; }"
        "QLabel#sidebarTitle { font-size: 18px; font-weight: 700; color: white; }"
        "QLabel#sidebarDesc { font-size: 12px; color: %4; line-height: 1.5; }"
        "QLabel#sidebarSection { font-size: 10.5px; font-weight: 700; color: #5C6E80; padding: 12px 4px 8px 4px; letter-spacing: 1.8px; }"
        "QFrame#sidebarDivider { background: rgba(255, 255, 255, 0.06); min-height: 1px; max-height: 1px; }"
        "QPushButton#navItem {"
        "  text-align: left; padding: 9px 14px; border: none; border-left: 3px solid transparent;"
        "  border-radius: 0px; background: transparent; color: %5; font-size: 13px; font-weight: 500;"
        "}"
        "QPushButton#navItem:hover { background: %6; color: #DDE3EA; }"
        "QPushButton#navItem:checked {"
        "  background: #1A3A52; color: white; border-left-color: %3; font-weight: 600;"
        "}"
        "QPushButton#subNavItem {"
          "  text-align: left; padding: 7px 14px 7px 12px; border: 1px solid transparent; border-radius: 4px;"
          "  background: transparent; color: #9AA8B8; font-size: 12.5px; font-weight: 500;"
        "}"
        "QPushButton#subNavItem:hover { background: rgba(255, 255, 255, 0.03); color: #C8D4E0; }"
        "QPushButton#subNavItem:checked {"
        "  background: rgba(46, 126, 201, 0.25); color: white; border-color: rgba(46, 126, 201, 0.20);"
        "}"
        "QLabel#sidebarFooterTitle { font-size: 12px; font-weight: 700; color: white; }"
        "QLabel#sidebarFooterDesc { font-size: 11px; color: %4; line-height: 1.5; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #2D476A; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #3A5D8B; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(Color::kSidebarBg)
        .arg(Color::kSidebarPanel)
        .arg(Color::kSidebarIndicator)
        .arg(Color::kSidebarMuted)
        .arg(Color::kSidebarText)
        .arg(Color::kSidebarHover)
        .arg(Color::kSidebarSelected);
}

}
