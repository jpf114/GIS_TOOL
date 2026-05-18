#pragma once

#include <string>
#include <map>

class QWidget;

namespace gis::gui {

void showWelcomeDialog(QWidget* parent);

bool isFirstRun();
void markFirstRunComplete();

}
