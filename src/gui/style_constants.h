#pragma once

#include <QString>

namespace gis::style {

namespace Color {

constexpr const char* kWindowBg        = "#F0F2F5";
constexpr const char* kCardBg          = "#FFFFFF";
constexpr const char* kCardBorder      = "#E4E7EC";
constexpr const char* kSidebarBg       = "#FAFBFC";
constexpr const char* kSidebarSelected = "#E8F0FE";
constexpr const char* kSidebarIndicator= "#3B82F6";
constexpr const char* kPrimary         = "#3B82F6";
constexpr const char* kPrimaryHover    = "#2563EB";
constexpr const char* kPrimaryLight    = "#EFF6FF";
constexpr const char* kSuccess         = "#10B981";
constexpr const char* kSuccessBg       = "#ECFDF5";
constexpr const char* kWarning         = "#F59E0B";
constexpr const char* kWarningBg       = "#FFFBEB";
constexpr const char* kError           = "#EF4444";
constexpr const char* kErrorBg         = "#FEF2F2";
constexpr const char* kTextPrimary     = "#1F2937";
constexpr const char* kTextSecondary   = "#6B7280";
constexpr const char* kTextMuted       = "#9CA3AF";
constexpr const char* kDivider         = "#E5E7EB";
constexpr const char* kInputBorder     = "#D1D5DB";
constexpr const char* kInputBg         = "#FFFFFF";
constexpr const char* kInputFocusBorder= "#3B82F6";
constexpr const char* kBrowseBtnBg     = "#F3F4F6";
constexpr const char* kBrowseBtnHover  = "#E5E7EB";
constexpr const char* kProgressTrack   = "#E5E7EB";
constexpr const char* kProgressFill    = "#3B82F6";
constexpr const char* kDisabledBg      = "#F3F4F6";
constexpr const char* kDisabledText    = "#9CA3AF";

}

namespace Size {

constexpr int kSidebarWidth       = 240;
constexpr int kSidebarMinWidth    = 200;
constexpr int kWindowMinWidth     = 900;
constexpr int kWindowMinHeight    = 600;
constexpr int kWindowDefaultWidth = 1200;
constexpr int kWindowDefaultHeight= 800;
constexpr int kCardRadius         = 12;
constexpr int kCardPadding        = 16;
constexpr int kCardSpacing        = 12;
constexpr int kInputRadius        = 6;
constexpr int kInputMinHeight     = 36;
constexpr int kInputPaddingH      = 12;
constexpr int kButtonRadius       = 8;
constexpr int kButtonMinHeight    = 40;
constexpr int kButtonMinWidth     = 120;
constexpr int kProgressHeight     = 8;
constexpr int kProgressRadius     = 4;
constexpr int kLabelInputRatio    = 2;

}

inline QString globalStyleSheet() {
    return QString(
        "QMainWindow { background: %1; }"
        "QWidget { color: %2; font-size: 13px; }"
        "QFrame#card { background: %3; border: 1px solid %4; border-radius: %5px; }"
        "QLabel#cardTitle { font-size: 14px; font-weight: 600; color: %2; }"
        "QLabel#cardDesc { font-size: 12px; color: %6; }"
        "QLabel#paramLabel { font-size: 13px; font-weight: 500; color: %2; }"
        "QLabel#paramKey { font-size: 11px; color: %7; background: %8; border: 1px solid %4; border-radius: 999px; padding: 2px 8px; }"
        "QLabel#paramDesc { font-size: 12px; color: %6; }"
        "QLabel#requiredMark { color: %9; font-weight: 700; }"
        "QLineEdit {"
        "  min-height: %10px; border: 1px solid %11; border-radius: %12px;"
        "  background: %13; padding: 0 %14px; color: %2;"
        "}"
        "QLineEdit:focus { border-color: %15; }"
        "QComboBox {"
        "  min-height: %10px; border: 1px solid %11; border-radius: %12px;"
        "  background: %13; padding: 0 %14px; color: %2;"
        "}"
        "QComboBox:focus { border-color: %15; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView {"
        "  border: 1px solid %11; border-radius: %12px;"
        "  background: %13; selection-background-color: %16; padding: 4px;"
        "}"
        "QSpinBox, QDoubleSpinBox {"
        "  min-height: %10px; border: 1px solid %11; border-radius: %12px;"
        "  background: %13; padding: 0 %14px; color: %2;"
        "}"
        "QSpinBox:focus, QDoubleSpinBox:focus { border-color: %15; }"
        "QPushButton#primaryButton {"
        "  background: %17; color: white; border: none; border-radius: %18px;"
        "  min-height: %19px; min-width: %20px; font-weight: 600;"
        "}"
        "QPushButton#primaryButton:hover { background: %21; }"
        "QPushButton#primaryButton:disabled { background: %7; color: white; }"
        "QPushButton#secondaryButton {"
        "  background: %22; color: %17; border: 1px solid %4; border-radius: %18px;"
        "  min-height: %19px; font-weight: 500;"
        "}"
        "QPushButton#secondaryButton:hover { background: %23; }"
        "QPushButton#secondaryButton:disabled { background: %24; color: %7; }"
        "QPushButton#browseButton {"
        "  min-width: 60px; max-width: 80px; min-height: %10px;"
        "  border: 1px solid %11; border-radius: %12px;"
        "  background: %25; color: %2; font-weight: 500;"
        "}"
        "QPushButton#browseButton:hover { background: %26; }"
        "QCheckBox { color: %2; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 1px solid %11; }"
        "QCheckBox::indicator:checked { background: %17; border-color: %17; }"
        "QProgressBar {"
        "  border: none; border-radius: %27px; background: %28; height: %29px; text-align: center;"
        "}"
        "QProgressBar::chunk { background: %30; border-radius: %27px; }"
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 8px; margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %11; border-radius: 4px; min-height: 24px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal {"
        "  background: transparent; height: 8px; margin: 0;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: %11; border-radius: 4px; min-width: 24px;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QStatusBar { background: %3; border-top: 1px solid %4; color: %6; font-size: 12px; }"
        "QSplitter::handle { background: %4; }"
    )
    .arg(Color::kWindowBg)
    .arg(Color::kTextPrimary)
    .arg(Color::kCardBg)
    .arg(Color::kCardBorder)
    .arg(Size::kCardRadius)
    .arg(Color::kTextSecondary)
    .arg(Color::kTextMuted)
    .arg(Color::kPrimaryLight)
    .arg(Color::kError)
    .arg(Size::kInputMinHeight)
    .arg(Color::kInputBorder)
    .arg(Size::kInputRadius)
    .arg(Color::kInputBg)
    .arg(Size::kInputPaddingH)
    .arg(Color::kInputFocusBorder)
    .arg(Color::kSidebarSelected)
    .arg(Color::kPrimary)
    .arg(Size::kButtonRadius)
    .arg(Size::kButtonMinHeight)
    .arg(Size::kButtonMinWidth)
    .arg(Color::kPrimaryHover)
    .arg(Color::kPrimaryLight)
    .arg(Color::kPrimary)
    .arg(Color::kDisabledBg)
    .arg(Color::kBrowseBtnBg)
    .arg(Color::kBrowseBtnHover)
    .arg(Size::kProgressRadius)
    .arg(Color::kProgressTrack)
    .arg(Size::kProgressHeight)
    .arg(Color::kProgressFill);
}

inline QString sidebarStyleSheet() {
    return QString(
        "QFrame#sidebar { background: %1; border-right: 1px solid %2; }"
        "QLabel#sidebarTitle { font-size: 16px; font-weight: 700; color: %3; padding: 16px 16px 8px 16px; }"
        "QPushButton#navItem {"
        "  text-align: left; padding: 10px 16px; border: none; border-radius: 0;"
        "  background: transparent; color: %4; font-size: 13px; font-weight: 500;"
        "}"
        "QPushButton#navItem:hover { background: %5; }"
        "QPushButton#navItem:checked {"
        "  background: %6; color: %7; border-left: 3px solid %7; font-weight: 600;"
        "}"
        "QLabel#subFunctionHeader { font-size: 11px; font-weight: 600; color: %4; padding: 12px 16px 4px 16px; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QPushButton#subNavItem {"
        "  text-align: left; padding: 8px 16px 8px 28px; border: none; border-radius: 0;"
        "  background: transparent; color: %4; font-size: 12px;"
        "}"
        "QPushButton#subNavItem:hover { background: %5; }"
        "QPushButton#subNavItem:checked {"
        "  background: %6; color: %7; font-weight: 600;"
        "}"
    )
    .arg(Color::kSidebarBg)
    .arg(Color::kCardBorder)
    .arg(Color::kTextPrimary)
    .arg(Color::kTextSecondary)
    .arg(Color::kSidebarSelected)
    .arg(Color::kSidebarSelected)
    .arg(Color::kPrimary);
}

}
