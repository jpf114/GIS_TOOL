#include "crs_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <proj.h>

CrsDialog::CrsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("选择坐标系"));
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);

    auto* searchLayout = new QHBoxLayout;
    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("输入EPSG代号（如4326、32650）搜索..."));
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"));
    connect(searchBtn, &QPushButton::clicked, this, &CrsDialog::onSearch);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &CrsDialog::onSearch);
    searchLayout->addWidget(searchEdit_);
    searchLayout->addWidget(searchBtn);
    mainLayout->addLayout(searchLayout);

    resultList_ = new QListWidget;
    resultList_->setAlternatingRowColors(true);
    connect(resultList_, &QListWidget::itemDoubleClicked,
            this, &CrsDialog::onItemDoubleClicked);
    mainLayout->addWidget(resultList_);

    auto* btnLayout = new QHBoxLayout;
    auto* okBtn = new QPushButton(QStringLiteral("确定"));
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        auto* current = resultList_->currentItem();
        if (current) {
            selectedCrs_ = current->data(Qt::UserRole).toString();
            accept();
        }
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void CrsDialog::onSearch() {
    QString query = searchEdit_->text().trimmed();
    if (query.isEmpty()) return;

    resultList_->clear();

    bool isNumber = false;
    int epsgNum = query.toInt(&isNumber);

    if (isNumber && epsgNum > 0) {
        auto* ctx = proj_context_create();
        if (ctx) {
            auto* crs = proj_create_from_database(
                ctx, "EPSG", std::to_string(epsgNum).c_str(),
                PJ_CATEGORY_CRS, false, nullptr);
            if (crs) {
                const char* name = proj_get_name(crs);
                if (name) {
                    QString text = QString("EPSG:%1 - %2").arg(epsgNum).arg(QString::fromUtf8(name));
                    auto* item = new QListWidgetItem(text, resultList_);
                    item->setData(Qt::UserRole, QString("EPSG:%1").arg(epsgNum));
                }
                proj_destroy(crs);
            } else {
                new QListWidgetItem(QStringLiteral("EPSG:%1 不存在或无效").arg(epsgNum), resultList_);
            }
            proj_context_destroy(ctx);
        }
    } else {
        new QListWidgetItem(QStringLiteral("请输入有效的EPSG代号（数字）"), resultList_);
    }

    if (resultList_->count() == 0) {
        new QListWidgetItem(QStringLiteral("未找到匹配的坐标系"), resultList_);
    }
}

void CrsDialog::onItemDoubleClicked(QListWidgetItem* item) {
    QString crs = item->data(Qt::UserRole).toString();
    if (!crs.isEmpty()) {
        selectedCrs_ = crs;
        accept();
    }
}
