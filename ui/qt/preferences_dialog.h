/* preferences_dialog.h
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef PREFERENCES_DIALOG_H
#define PREFERENCES_DIALOG_H

#include "config.h"

#include <glib.h>

#include "color.h"
#include "packet-range.h"

#include <epan/prefs.h>

#include <QDialog>
#include <QTreeWidgetItem>

namespace Ui {
class PreferencesDialog;
}

class PreferencesDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit PreferencesDialog(QWidget *parent = 0);
    ~PreferencesDialog();

protected:
    void showEvent(QShowEvent *evt);

private:
    void updateItem(QTreeWidgetItem &item);

    Ui::PreferencesDialog *pd_ui_;
//    QHash<pref_t *, QTreeWidgetItem *> pref_item_hash_;

private slots:
    void on_prefsTree_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void on_advancedSearchLineEdit_textEdited(const QString &search_str);
    void on_advancedTree_itemActivated(QTreeWidgetItem *item, int column);
    void uintPrefEditingFinished();
    void enumPrefCurrentIndexChanged(int index);
    void stringPrefEditingFinished();
    void rangePrefTextChanged(const QString & text);
    void rangePrefEditingFinished();
    void on_buttonBox_helpRequested();
    void on_advancedTree_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
};

#endif // PREFERENCES_DIALOG_H