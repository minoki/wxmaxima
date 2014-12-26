//
//  Copyright (C) 2009-2014 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

/*! \file

  This file contains the definition of the class History that handles the recently 
  issued commands for the history pane.
 */
#include <wx/wx.h>

#ifndef HISTORY_H
#define HISTORY_H

enum {
  history_ctrl_id,
  history_regex_id
};

/*! This class generates a pane containing the last commands that were issued.

 */
class History : public wxPanel
{
public:
  History(wxWindow* parent, int id);
  /* The destructor

     \todo Save the history on exit and reload it on loading a file?
   */
  ~History();
  //! Add a file to the recently opened files list.
  void AddToHistory(wxString cmd);
  void OnRegExEvent(wxCommandEvent &ev);
  void UpdateDisplay();
  wxString GetCommand(bool next);
private:
  wxListBox *m_history;
  wxTextCtrl *m_regex;
  wxArrayString commands;
  int m_current;
  DECLARE_EVENT_TABLE()
};

#endif // HISTORY_H
