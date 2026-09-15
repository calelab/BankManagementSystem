#ifndef BANK_UI_THEME_H
#define BANK_UI_THEME_H

#include <QIcon>

class QDialog;
class QLabel;

namespace bank::ui {

enum class Symbol { Bank, Account, Login, Search, Forecast, Audit, Switch };

// 全应用共用同一套调色板和控件样式，包括独立打开的业务弹窗。
void applyTheme();
void styleDialog(QDialog *dialog);
void setMessageTone(QLabel *label, const char *tone);
QIcon symbolIcon(Symbol symbol);

} // namespace bank::ui

#endif
