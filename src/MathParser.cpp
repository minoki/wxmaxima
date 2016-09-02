// -*- mode: c++; c-file-style: "linux"; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
//  Copyright (C) 2004-2015 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//            (C) 2014-2016 Gunter Königsmann <wxMaxima@physikbuch.de>
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
  This file defines the class MathParser that reads wxmx data and math from maxma.
*/

#include <wx/config.h>
#include <wx/tokenzr.h>
#include <wx/sstream.h>
#include <wx/regex.h>
#include <wx/intl.h>

#include "MathParser.h"

#include "FracCell.h"
#include "ExptCell.h"
#include "TextCell.h"
#include "SubCell.h"
#include "SqrtCell.h"
#include "LimitCell.h"
#include "MatrCell.h"
#include "ParenCell.h"
#include "AbsCell.h"
#include "ConjugateCell.h"
#include "AtCell.h"
#include "DiffCell.h"
#include "SumCell.h"
#include "IntCell.h"
#include "FunCell.h"
#include "EditorCell.h"
#include "ImgCell.h"
#include "SubSupCell.h"
#include "SlideShowCell.h"
#include "GroupCell.h"

wxXmlNode* MathParser::SkipWhitespaceNode(wxXmlNode* node)
{
  if(node)
  {
    // If this is a text node there is a chance that this is a whitespace we want to skip
    if(node->GetType() == wxXML_TEXT_NODE)
    {
      // This is a text node => Let's see if it is whitespace-only and skip it if it is.
      wxString contents = node->GetContent();
      contents.Trim();
      if(contents.Length()<=1)
        node = node->GetNext();
    }
  }
  return node;
}

wxXmlNode* MathParser::GetNextTag(wxXmlNode* node)
{
  if(node)
    node = node->GetNext();
  return SkipWhitespaceNode(node);
}

MathParser::MathParser(wxString zipfile)
{
  m_workingDirectory = wxEmptyString;
  m_ParserStyle = MC_TYPE_DEFAULT;
  m_FracStyle = FracCell::FC_NORMAL;
  m_highlight = false;
  if (zipfile.Length() > 0) {
    m_fileSystem = new wxFileSystem();
    m_fileSystem->ChangePathTo(zipfile + wxT("#zip:/"), true);
  }
  else
    m_fileSystem = NULL;
}

MathParser::~MathParser()
{
  if (m_fileSystem)
    delete m_fileSystem;
}

// ParseCellTag
// This function is responsible for creating
// a tree of groupcells when loading XML document.
// Any changes in GroupCell structure or methods
// has to be reflected here in order to ensure proper
// loading of WXMX files.
MathCell* MathParser::ParseCellTag(wxXmlNode* node)
{
  GroupCell *group = NULL;

  // read hide status
  bool hide = (node->GetAttribute(wxT("hide"), wxT("false")) == wxT("true")) ? true : false;
  // read (group)cell type
  wxString type = node->GetAttribute(wxT("type"), wxT("text"));
  wxString sectioning_level = node->GetAttribute(wxT("sectioning_level"), wxT("0"));

  if (type == wxT("code")) {
    group = new GroupCell(GC_TYPE_CODE);
    wxXmlNode *children = node->GetChildren();
    children = SkipWhitespaceNode(children);
    while (children) {
      if (children->GetName() == wxT("input")) {
        MathCell *editor = ParseTag(children->GetChildren());
        group->SetEditableContent(editor->GetValue());
        delete editor;
      }
      if (children->GetName() == wxT("output"))
      {
        MathCell *tag = ParseTag(children->GetChildren());
        group->AppendOutput(tag);
      }
      children = GetNextTag(children);
    }
  }  else if (type == wxT("image")) {
    group = new GroupCell(GC_TYPE_IMAGE);
    wxXmlNode *children = node->GetChildren();
    children = SkipWhitespaceNode(children);
    while (children) {
      if (children->GetName() == wxT("editor")) {
        MathCell *ed = ParseEditorTag(children);
        group->SetEditableContent(ed->GetValue());
        delete ed;
      }
      else
        group->AppendOutput(ParseTag(children));
      children = GetNextTag(children);
    }
  }
  else if (type == wxT("pagebreak")) {
    group = new GroupCell(GC_TYPE_PAGEBREAK);
  }
  else if (type == wxT("text")) {
    group = new GroupCell(GC_TYPE_TEXT);
    MathCell *editor = ParseTag(node->GetChildren());
    group->SetEditableContent(editor->GetValue());
    delete editor;
  }
  else {
    // text types
    if (type == wxT("title"))
      group = new GroupCell(GC_TYPE_TITLE);
    else if (type == wxT("section"))
      group = new GroupCell(GC_TYPE_SECTION);
    else if (type == wxT("subsection"))
    {
      // We save subsubsections as subsections with a higher sectioning level:
      // This makes them backwards-compatible in the way that they are displayed
      // as subsections on old wxMaxima installations.
      // A sectioning level of the value 0 means that the file is too old to
      // provide a sectioning level.
      if(sectioning_level != wxT("4"))
        group = new GroupCell(GC_TYPE_SUBSECTION);
      else
        group = new GroupCell(GC_TYPE_SUBSUBSECTION);
    }
    else if (type == wxT("subsubsection"))
    {
      group = new GroupCell(GC_TYPE_SUBSUBSECTION);
    }
    else
      return NULL;

    wxXmlNode *children = node->GetChildren();
    children = SkipWhitespaceNode(children);
    while (children) {
      if (children->GetName() == wxT("editor")) {
        MathCell *ed = ParseEditorTag(children);
        group->SetEditableContent(ed->GetValue());
        delete ed;
      }
      else if (children->GetName() == wxT("fold")) { // we have folded groupcells
        wxXmlNode *xmlcells = children->GetChildren();
        xmlcells = SkipWhitespaceNode(xmlcells);
        MathCell *tree = NULL;
        MathCell *last = NULL;
        while (xmlcells) {
          MathCell *cell = ParseTag(xmlcells, false);

          if(tree == NULL) tree = cell;

          if(last == NULL) last = cell;
          else
          {
            last->m_next = last->m_nextToDraw = cell;
            last->m_next->m_previous = last->m_next->m_previousToDraw = last;
            
            last = last->m_next;
          }
          xmlcells = GetNextTag(xmlcells);
        }
        if (tree)
          group->HideTree((GroupCell *)tree);
      }
      children = GetNextTag(children);
    }
  }

  group->SetParent(group);
  group->Hide(hide);
  return group;
}

MathCell* MathParser::ParseEditorTag(wxXmlNode* node)
{
  EditorCell *editor = new EditorCell();
  wxString type = node->GetAttribute(wxT("type"), wxT("input"));
  if (type == wxT("input"))
    editor->SetType(MC_TYPE_INPUT);
  else if (type == wxT("text"))
    editor->SetType(MC_TYPE_TEXT);
  else if (type == wxT("title"))
    editor->SetType(MC_TYPE_TITLE);
  else if (type == wxT("section"))
    editor->SetType(MC_TYPE_SECTION);
  else if (type == wxT("subsection"))
    editor->SetType(MC_TYPE_SUBSECTION);
  else if (type == wxT("subsubsection"))
    editor->SetType(MC_TYPE_SUBSUBSECTION);

  wxString text = wxEmptyString;
  wxXmlNode *line = node->GetChildren();
  while (line) {
    if (line->GetName() == wxT("line")) {
      if (!text.IsEmpty())
        text += wxT("\n");
#if wxUSE_UNICODE
      text += line->GetNodeContent();
#else
      wxString str = line->GetNodeContent();
      wxString str1(str.wc_str(wxConvUTF8), *wxConvCurrent);
      text += str1;
#endif
    }
    line = line->GetNext();
  } // end while
  editor->SetValue(text);
  return editor;
}

MathCell* MathParser::ParseFracTag(wxXmlNode* node)
{
  FracCell *frac = new FracCell;
  frac->SetFracStyle(m_FracStyle);
  frac->SetHighlight(m_highlight);
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  if (child)
  {
    MathCell *enumerator = ParseTag(child, false);
    wxASSERT_MSG(enumerator != NULL,_("Bug: Fraction without enumerator"));
    if(enumerator==NULL)
      return NULL;
    frac->SetNum(enumerator);
    child = GetNextTag(child);
    child = SkipWhitespaceNode(child);
    wxASSERT_MSG(((child != NULL) && (enumerator != NULL)),_("Bug: Fraction without enumerator"));
    if ((child != NULL) && (enumerator != NULL))
    {
      MathCell *denominator = ParseTag(child, false);
      if(denominator != NULL)
      {
        frac->SetDenom(denominator);
        wxASSERT_MSG(enumerator != NULL,_("Bug: Fraction without denominator"));
        frac->SetStyle(TS_VARIABLE);
        
        if(node->GetAttribute(wxT("line")) == wxT("no"))
        {
          frac->SetFracStyle(FracCell::FC_CHOOSE);
        }
        if(node->GetAttribute(wxT("diffstyle")) == wxT("yes"))
          frac->SetFracStyle(FracCell::FC_DIFF);
        frac->SetType(m_ParserStyle);
        frac->SetupBreakUps();
        return frac;
      }
    }
  }

  wxASSERT_MSG(false,_("bug:Invalid frac tag"));
  delete frac;
  return NULL;
}

MathCell* MathParser::ParseDiffTag(wxXmlNode* node)
{
  DiffCell *diff = new DiffCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  if (child)
  {
    int fc = m_FracStyle;
    m_FracStyle = FracCell::FC_DIFF;
    diff->SetDiff(ParseTag(child, false));
    m_FracStyle = fc;
    child = GetNextTag(child);
    if (child)
    {
      diff->SetBase(ParseTag(child, true));
      diff->SetType(m_ParserStyle);
      diff->SetStyle(TS_VARIABLE);
      return diff;
    }
  }
  delete diff;
  return NULL;
}

MathCell* MathParser::ParseSupTag(wxXmlNode* node)
{
  ExptCell *expt = new ExptCell;
  if (node->GetAttributes() != NULL)
    expt->IsMatrix(true);
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  if (child)
  {
    expt->SetBase(ParseTag(child, false));
    if(expt != NULL)
    {
      child = GetNextTag(child);
      if (child)
      {
        MathCell* power = ParseTag(child, false);
        if(power)
        {
          power->SetExponentFlag();
          expt->SetPower(power);
          expt->SetType(m_ParserStyle);
          expt->SetStyle(TS_VARIABLE);
          return expt;
        }
      }
    }
  }
  wxASSERT_MSG(false,_("bug:Invalid sup tag"));
  delete expt;
  return NULL;
}

MathCell* MathParser::ParseSubSupTag(wxXmlNode* node)
{
  SubSupCell *subsup = new SubSupCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  if (child)
  {
    subsup->SetBase(ParseTag(child, false));
    child = GetNextTag(child);
    if (child)
    {
      MathCell* index = ParseTag(child, false);
      index->SetExponentFlag();
      subsup->SetIndex(index);
      child = GetNextTag(child);
      if (child)
      {
        MathCell* power = ParseTag(child, false);
        power->SetExponentFlag();
        subsup->SetExponent(power);
        subsup->SetType(m_ParserStyle);
        subsup->SetStyle(TS_VARIABLE);
        return subsup;
      }
    }
  }

  wxASSERT_MSG(false,_("bug:Invalid subsup tag"));
  delete subsup;
  return NULL;
}

MathCell* MathParser::ParseSubTag(wxXmlNode* node)
{
  SubCell *sub = new SubCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);  
  if (child)
  {
    sub->SetBase(ParseTag(child, false));
    child = GetNextTag(child);
    if (child)
    {
      MathCell* index = ParseTag(child, false);
      if(index != NULL)
      {
        sub->SetIndex(index);
        index->SetExponentFlag();
      }
      else
        return(NULL);
      sub->SetType(m_ParserStyle);
      sub->SetStyle(TS_VARIABLE);

      return sub;
    }
  }

  wxASSERT_MSG(false,_("bug:Invalid sub tag"));
  delete sub;
  return NULL;
}

MathCell* MathParser::ParseAtTag(wxXmlNode* node)
{
  AtCell *at = new AtCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);

  if (child)
  {
    at->SetBase(ParseTag(child, false));
    at->SetHighlight(m_highlight);
    child = GetNextTag(child);
    if (child)
    {
      at->SetIndex(ParseTag(child, false));
      at->SetType(m_ParserStyle);
      at->SetStyle(TS_VARIABLE);
      return at;
    }
  }
  wxASSERT_MSG(false,_("bug:Invalid at tag"));
  delete at;
  return NULL;
}

MathCell* MathParser::ParseFunTag(wxXmlNode* node)
{
  FunCell *fun = new FunCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);

  if (child)
  {
    fun->SetName(ParseTag(child, false));
    child = GetNextTag(child);
    if (child)
    {
      fun->SetType(m_ParserStyle);
      fun->SetStyle(TS_VARIABLE);
      fun->SetArg(ParseTag(child, false));
      return fun;
    }
  }
  wxASSERT_MSG(false,_("bug:Invalid function tag"));
  delete fun;
  return NULL;
}

MathCell* MathParser::ParseText(wxXmlNode* node, int style)
{
  wxString str;
  TextCell *retval = NULL;
  if ((node != NULL) && ((str = node->GetContent()) != wxEmptyString))
  {
#if !wxUSE_UNICODE
    wxString str1(str.wc_str(wxConvUTF8), *wxConvCurrent);
    str = str1;
#endif
#if wxUSE_UNICODE
    str.Replace(wxT("-"), wxT("\x2212")); // unicode minus sign
#endif
    if (style == TS_NUMBER)
    {
      m_displayedDigits=100;
      wxConfigBase *config = wxConfig::Get();
      config->Read(wxT("displayedDigits"),&m_displayedDigits);

      if (m_displayedDigits<10)m_displayedDigits=10;
      if ((long)str.Length() > m_displayedDigits)
	{
	  int left= m_displayedDigits/3;
	  if (left>30) left=30;
	  
	  str = str.Left(left) + wxString::Format(_("[%i digits]"), (int) str.Length() - 2 * left) + str.Right(left);
	  //	  str = str.Left(left)+wxT("...");
	}
    }
    
    wxStringTokenizer lines(str, wxT('\n'));
    while(lines.HasMoreTokens())
    {
      TextCell* cell = new TextCell;
      if(style != TS_ERROR)
        cell->SetType(m_ParserStyle);
      else
        cell->SetType(MC_TYPE_ERROR);
      cell->SetStyle(style);
      cell->SetHighlight(m_highlight);
      cell->SetValue(lines.GetNextToken());
      if(retval == NULL)
        retval = cell;
      else
      {
        cell->ForceBreakLine(true);
        retval->AppendCell(cell);
      };
    }
  }

  if (retval == NULL)
    retval = new TextCell;

  wxString breaklineattrib;
  if(node != NULL)
    breaklineattrib = node->GetAttribute(wxT("breakline"), wxT("false"));

  if(breaklineattrib == wxT("true"))
  {
    if(retval)
    {
      retval->ForceBreakLine(true);
    }
  }
  return retval;
}

MathCell* MathParser::ParseCharCode(wxXmlNode* node, int style)
{
  TextCell* cell = new TextCell;
  wxString str;
  if ((node != NULL) && ((str = node->GetContent()) != wxEmptyString))
  {
    long code;
    if (str.ToLong(&code))
      str = wxString::Format(wxT("%c"), code);
#if !wxUSE_UNICODE
    wxString str1(str.wc_str(wxConvUTF8), *wxConvCurrent);
    str = str1;
#endif
    cell->SetValue(str);
    cell->SetType(m_ParserStyle);
    cell->SetStyle(style);
    cell->SetHighlight(m_highlight);
  }
  return cell;
}

MathCell* MathParser::ParseSqrtTag(wxXmlNode* node)
{
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  
  SqrtCell* cell = new SqrtCell;
  cell->SetInner(ParseTag(child, true));
  cell->SetType(m_ParserStyle);
  cell->SetStyle(TS_VARIABLE);
  cell->SetHighlight(m_highlight);
  return cell;
}

MathCell* MathParser::ParseAbsTag(wxXmlNode* node)
{
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  AbsCell* cell = new AbsCell;
  cell->SetInner(ParseTag(child, true));
  cell->SetType(m_ParserStyle);
  cell->SetStyle(TS_VARIABLE);
  cell->SetHighlight(m_highlight);
  return cell;
}

MathCell* MathParser::ParseConjugateTag(wxXmlNode* node)
{
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  ConjugateCell* cell = new ConjugateCell;
  cell->SetInner(ParseTag(child, true));
  cell->SetType(m_ParserStyle);
  cell->SetStyle(TS_VARIABLE);
  cell->SetHighlight(m_highlight);
  return cell;
}

MathCell* MathParser::ParseParenTag(wxXmlNode* node)
{
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  ParenCell* cell = new ParenCell;
  cell->SetInner(ParseTag(child, true), m_ParserStyle);
  cell->SetHighlight(m_highlight);
  cell->SetStyle(TS_VARIABLE);
  if (node->GetAttributes() != NULL)
    cell->SetPrint(false);
  return cell;
}

MathCell* MathParser::ParseLimitTag(wxXmlNode* node)
{
  LimitCell *limit = new LimitCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  if (child)
  {
    limit->SetName(ParseTag(child, false));
    child = GetNextTag(child);
    if (child)
    {
      limit->SetUnder(ParseTag(child, false));
      child = GetNextTag(child);
      if (child)
      {
        limit->SetBase(ParseTag(child, false));
        limit->SetType(m_ParserStyle);
        limit->SetStyle(TS_VARIABLE);
        return limit;
      }
    }
  }

  wxASSERT_MSG(false,_("bug:Invalid limit tag"));
  delete limit;
  return NULL;
}

MathCell* MathParser::ParseSumTag(wxXmlNode* node)
{
  SumCell *sum = new SumCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  wxString type = node->GetAttribute(wxT("type"), wxT("sum"));

  if (type == wxT("prod"))
    sum->SetSumStyle(SM_PROD);
  sum->SetHighlight(m_highlight);
  if (child)
  {
    sum->SetUnder(ParseTag(child, false));
    child = GetNextTag(child);
    if (child)
    {
      if (type != wxT("lsum"))
        sum->SetOver(ParseTag(child, false));
      child = GetNextTag(child);
      if (child)
      {
        sum->SetBase(ParseTag(child, false));
        sum->SetType(m_ParserStyle);
        sum->SetStyle(TS_VARIABLE);
        return sum;
      }
    }
  }
  wxASSERT_MSG(false,_("bug:Invalid sum tag"));
  delete sum;
  return NULL;
}

MathCell* MathParser::ParseIntTag(wxXmlNode* node)
{
  IntCell *in = new IntCell;
  wxXmlNode* child = node->GetChildren();
  child = SkipWhitespaceNode(child);
  in->SetHighlight(m_highlight);
  wxString definiteAtt=node->GetAttribute(wxT("def"),wxT("true"));
  if (definiteAtt != wxT("true"))
  {
    // A indefinite integral
    if (child)
    {
      in->SetBase(ParseTag(child, false));
      child = GetNextTag(child);
      if (child)
      {
        in->SetVar(ParseTag(child, true));
        in->SetType(m_ParserStyle);
        in->SetStyle(TS_VARIABLE);
        return in;
      }
    }
  }
  else
  {
    // A Definite integral
    in->SetIntStyle(IntCell::INT_DEF);
    if (child)
    {
      in->SetUnder(ParseTag(child, false));
      child = GetNextTag(child);
      if (child)
      {
        in->SetOver(ParseTag(child, false));
        child = GetNextTag(child);
        if (child)
        {
          in->SetBase(ParseTag(child, false));
          child = GetNextTag(child);
          if (child)
          {
            in->SetVar(ParseTag(child, true));
            in->SetType(m_ParserStyle);
            in->SetStyle(TS_VARIABLE);
            return in;
          }
        }
      }
    }
  }
  
  wxASSERT_MSG(false,_("bug:Invalid int tag"));
  delete in;
  return NULL;
}

MathCell* MathParser::ParseTableTag(wxXmlNode* node)
{
  MatrCell *matrix = new MatrCell;
  matrix->SetHighlight(m_highlight);

  if (node->GetAttribute(wxT("special"), wxT("false")) == wxT("true"))
    matrix->SetSpecialFlag(true);
  if (node->GetAttribute(wxT("inference"), wxT("false")) == wxT("true"))
  {
    matrix->SetInferenceFlag(true);
    matrix->SetSpecialFlag(true);
  }
  if (node->GetAttribute(wxT("colnames"), wxT("false")) == wxT("true"))
    matrix->ColNames(true);
  if (node->GetAttribute(wxT("rownames"), wxT("false")) == wxT("true"))
    matrix->RowNames(true);

  wxXmlNode* rows = SkipWhitespaceNode(node->GetChildren());
  while (rows)
  {
    matrix->NewRow();
    wxXmlNode* cells = SkipWhitespaceNode(rows->GetChildren());
    while (cells)
    {
      matrix->NewColumn();
      matrix->AddNewCell(ParseTag(cells, false));
      cells = GetNextTag(cells);
    }
    rows = rows->GetNext();
  }
  matrix->SetType(m_ParserStyle);
  matrix->SetStyle(TS_VARIABLE);
  matrix->SetDimension();
  return matrix;
}

MathCell* MathParser::ParseTag(wxXmlNode* node, bool all)
{
  //  wxYield();
  MathCell* retval = NULL;
  MathCell* cell = NULL;
  bool warning = all;
  wxString altCopy;

  node = SkipWhitespaceNode(node);

  while (node)
  {
    if (node->GetType() == wxXML_ELEMENT_NODE)
    {
      // Parse XML tags. The only other type of element we recognize are text
      // nodes.
      wxString tagName(node->GetName());

      MathCell *tmp = NULL;
      if (tagName == wxT("v"))
      {               // Variables (atoms)
        tmp = ParseText(node->GetChildren(), TS_VARIABLE);
      }
      else if (tagName == wxT("t"))
      {          // Other text
        TextStyle style = TS_DEFAULT;
        if(node->GetAttribute(wxT("type")) == wxT("error"))
          style = TS_ERROR;
        tmp = ParseText(node->GetChildren(), style);
      }
      else if (tagName == wxT("n"))
      {          // Numbers
        tmp = ParseText(node->GetChildren(), TS_NUMBER);
      }
      else if (tagName == wxT("h"))
      {          // Hidden cells (*)
        tmp = ParseText(node->GetChildren());
        tmp->m_isHidden = true;
      }
      else if (tagName == wxT("p"))
      {          // Parenthesis
        tmp = ParseParenTag(node);
      }
      else if (tagName == wxT("f"))
      {               // Fractions
        tmp = ParseFracTag(node);
      }
      else if (tagName == wxT("e"))
      {          // Exponentials
        tmp = ParseSupTag(node);
      }
      else if (tagName == wxT("i"))
      {          // Subscripts
        tmp = ParseSubTag(node);
      }
      else if (tagName == wxT("fn"))
      {         // Functions
        tmp = ParseFunTag(node);
      }
      else if (tagName == wxT("g"))
      {          // Greek constants
        tmp = ParseText(node->GetChildren(), TS_GREEK_CONSTANT);
      }
      else if (tagName == wxT("s"))
      {          // Special constants %e,...
        tmp = ParseText(node->GetChildren(), TS_SPECIAL_CONSTANT);
      }
      else if (tagName == wxT("fnm"))
      {         // Function names
        tmp = ParseText(node->GetChildren(), TS_FUNCTION);
      }
      else if (tagName == wxT("q"))
      {          // Square roots
        tmp = ParseSqrtTag(node);
      }
      else if (tagName == wxT("d"))
      {          // Differentials
        tmp = ParseDiffTag(node);
      }
      else if (tagName == wxT("sm"))
      {         // Sums
        tmp = ParseSumTag(node);
      }
      else if (tagName == wxT("in"))
      {         // integrals
        tmp = ParseIntTag(node);
      }
      else if (tagName == wxT("mspace"))
      {
        tmp = new TextCell(wxT(" "));
      }
      else if (tagName == wxT("at"))
      {
        tmp = ParseAtTag(node);
      }
      else if (tagName == wxT("a"))
      {
        tmp = ParseAbsTag(node);
      }
      else if (tagName == wxT("cj"))
      {
        tmp = ParseConjugateTag(node);
      }
      else if (tagName == wxT("ie"))
      {
        tmp = ParseSubSupTag(node);
      }
      else if (tagName == wxT("lm"))
      { // A limit tag
        tmp = ParseLimitTag(node);
      }
      else if (tagName == wxT("r"))
      { // A group of tags
        tmp = ParseTag(node->GetChildren());
      }
      else if (tagName == wxT("tb"))
      {
        tmp = ParseTableTag(node);
      }
      else if ((tagName == wxT("mth")) || (tagName == wxT("line")))
      {
        tmp = ParseTag(node->GetChildren());
        if (tmp != NULL)
          tmp->ForceBreakLine(true);
        else
          tmp = new TextCell(wxT(" "));
      }
      else if (tagName == wxT("lbl"))
      {
        if (node->GetAttribute(wxT("userdefined"), wxT("no")) != wxT("yes"))
          tmp = ParseText(node->GetChildren(), TS_LABEL);
        else
          tmp = ParseText(node->GetChildren(), TS_USERLABEL);
        tmp->ForceBreakLine(true);
      }
      else if (tagName == wxT("st"))
      {
        tmp = ParseText(node->GetChildren(), TS_STRING);
      }
      else if (tagName == wxT("hl"))
      {
        bool highlight = m_highlight;
        m_highlight = true;
        tmp = ParseTag(node->GetChildren());
        m_highlight = highlight;
      }
      else if (tagName == wxT("img"))
      {
        ImgCell *imageCell;
        wxString filename(node->GetChildren()->GetContent());
#if !wxUSE_UNICODE
        wxString filename1(filename.wc_str(wxConvUTF8), *wxConvCurrent);
        filename = filename1;
#endif

        if (m_fileSystem) // loading from zip
          imageCell = new ImgCell(filename, false, m_fileSystem);
        else
        {
          if (node->GetAttribute(wxT("del"), wxT("yes")) != wxT("no"))
            imageCell = new ImgCell(filename, true, NULL);
          else
          {
            // This is the only case show_image() produces ergo this is the only
            // case we might get a local path

            if(
              (!wxFileExists(filename)) &&
              (wxFileExists(m_workingDirectory + wxT("/") + filename))
              )
              filename = m_workingDirectory + wxT("/") + filename;
            
            imageCell = new ImgCell(filename, false, NULL);
          }
        }
        
        if (node->GetAttribute(wxT("rect"), wxT("true")) == wxT("false"))
          imageCell->DrawRectangle(false);

        tmp = imageCell;
      }
      else if (tagName == wxT("slide"))
      {
        SlideShow *slideShow = new SlideShow(m_fileSystem);
        wxString str(node->GetChildren()->GetContent());
        wxArrayString images;
        wxString framerate;
        wxStringTokenizer tokens(str, wxT(";"));
        if (node->GetAttribute(wxT("fr"), &framerate))
        {
          long fr;
          if (framerate.ToLong(&fr))
            slideShow->SetFrameRate(fr);
        }
        while (tokens.HasMoreTokens()) {
          wxString token = tokens.GetNextToken();
          if (token.Length())
          {
#if !wxUSE_UNICODE
            wxString token1(token.wc_str(wxConvUTF8), *wxConvCurrent);
            token = token1;
#endif
            images.Add(token);
          }
        }
        if(slideShow)
          slideShow->LoadImages(images);
        tmp = slideShow;
      }
      else if (tagName == wxT("editor"))
      {
        tmp = ParseEditorTag(node);
      }
      else if (tagName == wxT("cell"))
      {
        tmp = ParseCellTag(node);
      }
      else if (tagName == wxT("ascii"))
      {
        tmp = ParseCharCode(node->GetChildren());
      }
      else if (node->GetChildren())
      {
        tmp = ParseTag(node->GetChildren());
      }

      // The new cell may needing being equipped with a "altCopy" tag.
      if ((tmp != NULL) && (node->GetAttribute(wxT("altCopy"), &altCopy)))
        tmp->SetAltCopyText(altCopy);

      // Append the cell we found (tmp) to the list of cells we parsed so far (cell).
      if(tmp != NULL)
      {
        if (node->GetAttribute(wxT("breakline"), wxT("false")) == wxT("true"))
          tmp->ForceBreakLine(true);
        if (cell == NULL)
          cell = tmp;
        else
          cell->AppendCell(tmp);
      }
    }
    else
    {
      // We didn't get a tag but got a text cell => Parse the text.
      if (cell == NULL)
        cell = ParseText(node);
      else
        cell->AppendCell(ParseText(node));
    }

    if (cell != NULL)
    {
      // Append the new cell to the return value
      if (retval == NULL)
        retval = cell;
      else
        cell = cell->m_next;
    }
    else if ((warning) && (!all))
    {
      // Tell the user we ran into problems.
      wxString name;
      name.Trim(true);
      name.Trim(false);
      if(cell != NULL) name = cell->ToString();
      if(name.Length()!= 0)
      {
        wxMessageBox(_("Parts of the document will not be loaded correctly:\nFound unknown XML Tag name "+ name), _("Warning"),
                     wxOK | wxICON_WARNING);
        warning = false;
      }
    }
    
    if (!all)
      break;
    
    node = GetNextTag(node);
  }
  
  return retval;
}

/***
 * Parse the string s, which is (correct) xml fragment.
 * Put the result in line.
 */
MathCell* MathParser::ParseLine(wxString s, int style)
{
  m_ParserStyle = style;
  m_FracStyle = FracCell::FC_NORMAL;
  m_highlight = false;
  MathCell* cell = NULL;

  wxConfigBase* config = wxConfig::Get();
  int showLength = 0;
  config->Read(wxT("showLength"), &showLength);

  switch(showLength)
  {
  case 0:
    showLength = 50000;
    break;
  case 1:
    showLength = 500000;
    break;
  case 2:
    showLength = 5000000;
    break;
  case 3:
    showLength = 0;
    break;
  }
  wxRegEx graph(wxT("[[:cntrl:]]"));

#if wxUSE_UNICODE
  graph.Replace(&s, wxT("\xFFFD"));
#else
  graph.Replace(&s, wxT("?"));
#endif

  if (((long)s.Length() < showLength) || (showLength==0))
  {

    wxXmlDocument xml;

#if wxUSE_UNICODE
    wxStringInputStream xmlStream(s);
#else
    wxString su(s.wc_str(*wxConvCurrent), wxConvUTF8);
    wxStringInputStream xmlStream(su);
#endif

    xml.Load(xmlStream,wxT("UTF-8"),wxXMLDOC_KEEP_WHITESPACE_NODES);

    wxXmlNode *doc = xml.GetRoot();

    if (doc != NULL)
      cell = ParseTag(doc->GetChildren());
  }
  else
  {
    cell = new TextCell(_(" << Expression longer than allowed by the configuration setting! >>"));
    cell->ForceBreakLine(true);
  }
  return cell;
}
