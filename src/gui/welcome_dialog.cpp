#include "welcome_dialog.h"
#include "style_constants.h"
#include "app_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLatin1String>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>

namespace gis::gui {

// 键名与旧注册表落点（HKCU\Software\GIS\GIS Tool\app）下的值名逐字一致，
// 这样 R-B4 的"补缺不覆盖"导入才能把它原样搬到 ini 里。
static const char* kFirstRunKey = "app/first_run_complete";

// 落点统一走 appSettings()（批次 B）：默认构造的 QSettings 吃 org/app ⇒ 在生产进程里是
// NativeFormat ⇒ 写的其实是 HKCU\Software\GIS\GIS Tool，那是 N101 的第三条腿。
// 两个对象都由 prvalue 直接初始化（Qt 6 的 QSettings 是 Q_DISABLE_COPY 的）。
bool isFirstRun() {
    QSettings settings = gis::gui::appSettings();
    return !settings.value(QLatin1String(kFirstRunKey), false).toBool();
}

void markFirstRunComplete() {
    QSettings settings = gis::gui::appSettings();
    settings.setValue(QLatin1String(kFirstRunKey), true);
}

void showWelcomeDialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("欢迎使用 GIS TOOLKIT"));
    dlg.setMinimumWidth(520);
    dlg.setStyleSheet(gis::style::globalStyleSheet());

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(16);

    auto* titleLabel = new QLabel(QStringLiteral("欢迎使用 GIS TOOLKIT"));
    titleLabel->setObjectName(QStringLiteral("heroTitle"));
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(
        QStringLiteral(
            "<p>GIS TOOLKIT 是一款专业的遥感算法工作台，提供 124+ 种算法，"
            "涵盖遥感预处理、影像匹配、图像处理、分类、矢量操作等功能。</p>"
            "<p><b>快速上手：</b></p>"
            "<ul>"
            "<li>左侧导航栏选择主功能 &rarr; 展开子功能</li>"
            "<li>右侧面板填写参数 &rarr; 点击&ldquo;执行处理&rdquo;</li>"
            "<li>执行成功后点击&ldquo;查看结果&rdquo;预览输出</li>"
            "<li>切换到&ldquo;工作流&rdquo;标签页可使用预设流水线</li>"
            "</ul>"));
    descLabel->setObjectName(QStringLiteral("heroDesc"));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addStretch();

    auto* btnBox = new QDialogButtonBox;
    auto* okBtn = new QPushButton(QStringLiteral("开始使用"));
    okBtn->setObjectName(QStringLiteral("primaryButton"));
    btnBox->addButton(okBtn, QDialogButtonBox::AcceptRole);
    layout->addWidget(btnBox);

    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();

    markFirstRunComplete();
}

} // namespace gis::gui
