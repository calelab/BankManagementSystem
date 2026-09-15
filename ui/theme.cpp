#include "ui/theme.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>

// 显式初始化静态库中的资源，确保链接器保留主题资源。
static void initializeThemeResources()
{
    Q_INIT_RESOURCE(bank_theme);
}

namespace bank::ui {

void applyTheme()
{
    if (qApp->property("bankThemeApplied").toBool()) {
        return;
    }
    initializeThemeResources();
    QFile styleFile(QStringLiteral(":/theme/assets/blue-white.qss"));
    if (!styleFile.open(QIODevice::ReadOnly)) {
        qWarning("Unable to load the bank theme resource");
        return;
    }

    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#F5F8FC"));
    palette.setColor(QPalette::WindowText, QColor("#34465B"));
    palette.setColor(QPalette::Base, QColor("#FFFFFF"));
    palette.setColor(QPalette::AlternateBase, QColor("#F5F9FD"));
    palette.setColor(QPalette::Text, QColor("#34465B"));
    palette.setColor(QPalette::Button, QColor("#FFFFFF"));
    palette.setColor(QPalette::ButtonText, QColor("#34465B"));
    palette.setColor(QPalette::Highlight, QColor("#DFECFC"));
    palette.setColor(QPalette::HighlightedText, QColor("#24496B"));
    palette.setColor(QPalette::PlaceholderText, QColor("#718399"));
    palette.setColor(QPalette::ToolTipBase, QColor("#FFFFFF"));
    palette.setColor(QPalette::ToolTipText, QColor("#34465B"));
    palette.setColor(QPalette::Link, QColor("#3175C2"));
    palette.setColor(QPalette::Light, QColor("#FFFFFF"));
    palette.setColor(QPalette::Mid, QColor("#DCE5EE"));
    palette.setColor(QPalette::Dark, QColor("#A6B9CD"));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text,
                                    QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, QColor("#8593A3"));
    }
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    qApp->setProperty("bankThemeApplied", true);
}

void styleDialog(QDialog *dialog)
{
    applyTheme();
    const int minimumWidth = qMax(520, dialog->minimumWidth());
    dialog->setMinimumWidth(minimumWidth);
    if (dialog->layout()) {
        dialog->layout()->setContentsMargins(28, 26, 28, 24);
        dialog->layout()->setSpacing(18);
        dialog->layout()->setSizeConstraint(QLayout::SetMinimumSize);
    }
    for (auto *form : dialog->findChildren<QFormLayout *>()) {
        form->setVerticalSpacing(12);
        form->setHorizontalSpacing(16);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    }
    for (auto *button : dialog->findChildren<QPushButton *>()) {
        if (button->objectName().startsWith(QStringLiteral("confirm"))) {
            button->setProperty("tone", "primary");
        }
        button->setCursor(Qt::PointingHandCursor);
    }
    for (auto *label : dialog->findChildren<QLabel *>()) {
        label->setMinimumHeight(22);
        if (label->objectName().endsWith(QStringLiteral("TitleLabel"))) {
            label->setProperty("role", "pageTitle");
            // 宽度随内容约束保留，垂直方向则由实际文字和行距决定。
            label->setMinimumWidth(minimumWidth - 56);
        }
        if (label->objectName().endsWith(QStringLiteral("MessageLabel"))) {
            label->setProperty("tone", "warning");
            label->setMinimumHeight(24);
        }
    }
    for (auto *widget : dialog->findChildren<QWidget *>()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->updateGeometry();
    }
}

void setMessageTone(QLabel *label, const char *tone)
{
    label->setProperty("tone", tone);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}

QIcon symbolIcon(Symbol symbol)
{
    // 矢量绘制后生成高分辨率图标，不依赖系统字体中的图形字符。
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(4, 4);
    painter.setPen(QPen(QColor("#3175C2"), 1.7, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    switch (symbol) {
    case Symbol::Bank: {
        QPolygonF roof{QPointF(4, 11), QPointF(16, 5), QPointF(28, 11)};
        painter.drawPolyline(roof);
        line(5, 13, 27, 13);
        for (int x : {8, 16, 24}) line(x, 16, x, 24);
        line(5, 27, 27, 27);
        break;
    }
    case Symbol::Account:
        painter.drawEllipse(QPointF(13, 10), 4, 4);
        painter.drawArc(QRectF(4, 17, 18, 16), 0, 180 * 16);
        line(25, 16, 25, 26); line(20, 21, 30, 21);
        break;
    case Symbol::Login:
        painter.drawRoundedRect(QRectF(17, 5, 10, 23), 2, 2);
        line(3, 16, 21, 16); line(10, 10, 16, 16); line(10, 22, 16, 16);
        break;
    case Symbol::Search:
        painter.drawEllipse(QPointF(13, 13), 8, 8);
        line(19, 19, 27, 27);
        break;
    case Symbol::Forecast:
        painter.drawRoundedRect(QRectF(5, 7, 23, 22), 2, 2);
        line(5, 13, 28, 13); line(11, 4, 11, 10); line(22, 4, 22, 10);
        line(11, 24, 11, 21); line(17, 24, 17, 18); line(23, 24, 23, 16);
        break;
    case Symbol::Audit:
        painter.drawRoundedRect(QRectF(7, 4, 19, 25), 2, 2);
        line(12, 11, 21, 11); line(12, 17, 21, 17); line(12, 23, 18, 23);
        break;
    case Symbol::Switch:
        line(5, 11, 27, 11); line(21, 5, 27, 11); line(21, 17, 27, 11);
        line(27, 23, 5, 23); line(11, 17, 5, 23); line(11, 29, 5, 23);
        break;
    }
    painter.end();
    QIcon icon;
    icon.addPixmap(pixmap);
    return icon;
}

} // namespace bank::ui
