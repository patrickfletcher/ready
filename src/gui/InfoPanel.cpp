/*  Copyright 2011-2019 The Ready Bunch

    This file is part of Ready.

    Ready is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ready is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ready. If not, see <http://www.gnu.org/licenses/>.         */

// local:
#include "InfoPanel.hpp"
#include "app.hpp"              // for wxGetApp
#include "frame.hpp"
#include "IDs.hpp"
#include "prefs.hpp"            // for readydir, etc
#include "wxutils.hpp"          // for Warning, CopyTextToClipboard
#include "dialogs.hpp"

// readybase:
#include "ImageRD.hpp"

// wxWidgets:
#include <wx/filename.h>        // for wxFileName
#include <wx/html/htmlwin.h>    // for wxHtmlWindow
#include <wx/colordlg.h>

// VTK:
#include <vtkMath.h>

// STL:
#include <string>
using namespace std;

const wxString change_prefix = _("change: ");

// labels in 1st column
const wxString rule_name_label = _("Rule name");
const wxString rule_type_label = _("Rule type");
const wxString description_label = _("Description");
const wxString num_chemicals_label = _("Number of chemicals");
const wxString formula_label = _("Formula");
const wxString kernel_label = _("Kernel");
const wxString dimensions_label = _("Dimensions");
const wxString block_size_label = _("Block size");
const wxString number_of_cells_label = _("Number of cells");
const wxString wrap_label = _("Toroidal wrap-around");
const wxString data_type_label = _("Data type");
const wxString neighborhood_type_label = _("Neighborhood");
const wxString neighborhood_range_label = _("Neighborhood range");
const wxString neighborhood_weight_label = _("Neighborhood weight");

// -----------------------------------------------------------------------------

// define a child window for displaying HTML info
class HtmlInfo : public wxHtmlWindow
{
    public:

        HtmlInfo(wxWindow* parent, MyFrame* myframe, wxWindowID id = wxID_ANY,
            const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
            long style = wxHW_SCROLLBAR_AUTO | wxWANTS_CHARS)
            : wxHtmlWindow(parent, id, pos, size, style)
        {
            frame = myframe;
            panel = (InfoPanel*)parent;
            editlink = false;
            linkrect = wxRect(0,0,0,0);
            scroll_x = scroll_y = 0;
        }

        virtual void OnLinkClicked(const wxHtmlLinkInfo& link);
        virtual void OnCellMouseHover(wxHtmlCell* cell, wxCoord x, wxCoord y);

        void ClearStatus();  // clear pane's status line

        void SetFontSizes(int size);
        void ChangeFontSizes(int size);

        wxRect linkrect;     // rect for cell containing link

        void SaveScrollPos() { GetViewStart(&scroll_x, &scroll_y); }
        void RestoreScrollPos() { Scroll(scroll_x, scroll_y); }
        void ResetScrollPos() { Scroll(0,0); }

    private:

        void OnSize(wxSizeEvent& event);
        void OnMouseMotion(wxMouseEvent& event);
        void OnMouseLeave(wxMouseEvent& event);
        void OnMouseDown(wxMouseEvent& event);
        void OnHtmlCellClicked(wxHtmlCellEvent& event);

    private:

        MyFrame* frame;
        InfoPanel* panel;

        bool editlink;          // open clicked file in editor?
        int scroll_x,scroll_y;  // remember the scroll position

        DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(HtmlInfo, wxHtmlWindow)
    EVT_SIZE          (HtmlInfo::OnSize)
    EVT_MOTION        (HtmlInfo::OnMouseMotion)
    EVT_ENTER_WINDOW  (HtmlInfo::OnMouseMotion)
    EVT_LEAVE_WINDOW  (HtmlInfo::OnMouseLeave)
    EVT_LEFT_DOWN     (HtmlInfo::OnMouseDown)
    EVT_RIGHT_DOWN    (HtmlInfo::OnMouseDown)
    EVT_HTML_CELL_CLICKED (wxID_ANY, HtmlInfo::OnHtmlCellClicked)
END_EVENT_TABLE()

// -----------------------------------------------------------------------------

void HtmlInfo::OnLinkClicked(const wxHtmlLinkInfo& link)
{
    wxString url = link.GetHref();
    if ( url.StartsWith(wxT("http:")) || url.StartsWith(wxT("mailto:")) ) {
        // pass http/mailto URL to user's preferred browser/emailer
        if ( !wxLaunchDefaultBrowser(url) )
            Warning(_("Could not open URL in browser!"));

    } else if ( url.StartsWith(change_prefix) ) {
        panel->ChangeInfo( url.Mid(change_prefix.size()) );
        // best to reset focus after dialog closes
        SetFocus();

    } else if ( url.StartsWith(wxT("prefs:")) ) {
        // user clicked on link to Preferences dialog
        frame->ShowPrefsDialog( url.AfterFirst(':') );

    } else if ( url.StartsWith(wxT("edit:")) ) {
        // open clicked file in user's preferred text editor
        wxString path = url.AfterFirst(':');
        #ifdef __WXMSW__
            path.Replace(wxT("/"), wxT("\\"));
        #endif
        wxFileName fname(path);
        if (!fname.IsAbsolute()) path = readydir + path;
        frame->EditFile(path);


    } else if ( url.StartsWith(wxT("open:")) ) {
        // open clicked file
        wxString path = url.AfterFirst(':');
        #ifdef __WXMSW__
            path.Replace(wxT("/"), wxT("\\"));
        #endif
        wxFileName fname(path);
        if (!fname.IsAbsolute()) path = readydir + path;
        if (editlink) {
            frame->EditFile(path);
        } else {
            frame->Raise();
            frame->OpenFile(path);
        }

    } else {
        // assume it's a link to a local target
        LoadPage(url);
    }
}

// -----------------------------------------------------------------------------

void HtmlInfo::OnHtmlCellClicked(wxHtmlCellEvent& event)
{
    wxHtmlCell* cell = event.GetCell();
    int x = event.GetPoint().x;
    int y = event.GetPoint().y;

    wxHtmlLinkInfo* link = cell->GetLink(x,y);
    if (link) {
        // set linkrect to avoid bug in wxHTML if click was in outer edge of link
        // (bug is in htmlwin.cpp in wxHtmlWindowMouseHelper::HandleIdle;
        // OnCellMouseHover needs to be called if cell != m_tmpLastCell)
        wxPoint pt = ScreenToClient( wxGetMousePosition() );
        linkrect = wxRect(pt.x-x, pt.y-y, cell->GetWidth(), cell->GetHeight());
    }

    event.Skip();   // call OnLinkClicked
}

// -----------------------------------------------------------------------------

void HtmlInfo::OnCellMouseHover(wxHtmlCell* cell, wxCoord x, wxCoord y)
{
    wxHtmlLinkInfo* link = cell->GetLink(x,y);
    if (link) {
        wxString href = link->GetHref();
        href.Replace(wxT("&"), wxT("&&"));
        panel->SetStatus(href);
        wxPoint pt = ScreenToClient( wxGetMousePosition() );
        linkrect = wxRect(pt.x-x, pt.y-y, cell->GetWidth(), cell->GetHeight());
    } else {
        ClearStatus();
    }
}

// -----------------------------------------------------------------------------

void HtmlInfo::OnMouseMotion(wxMouseEvent& event)
{
    if (!linkrect.IsEmpty()) {
        int x = event.GetX();
        int y = event.GetY();
        if (!linkrect.Contains(x,y)) ClearStatus();
    }
    event.Skip();
}

// -----------------------------------------------------------------------------

void HtmlInfo::OnMouseLeave(wxMouseEvent& event)
{
    ClearStatus();
    event.Skip();
}

// -----------------------------------------------------------------------------

void HtmlInfo::ClearStatus()
{
    panel->SetStatus(wxEmptyString);
    linkrect = wxRect(0,0,0,0);
}

// -----------------------------------------------------------------------------

void HtmlInfo::OnMouseDown(wxMouseEvent& event)
{
    // set flag so ctrl/right-clicked file can be opened in editor
    editlink = event.ControlDown() || event.RightDown();
    event.Skip();
}

// -----------------------------------------------------------------------------

// avoid scroll position being reset to top when wxHtmlWindow is resized
void HtmlInfo::OnSize(wxSizeEvent& event)
{
    SaveScrollPos();

    Freeze(); // prevent flicker

    wxHtmlWindow::OnSize(event);

    wxString currpage = GetOpenedPage();
    if ( !currpage.IsEmpty() ) {
        LoadPage(currpage);         // reload page
    }

    RestoreScrollPos();
    Thaw();

    // prevent wxHtmlWindow::OnSize being called again
    event.Skip(false);
}

// -----------------------------------------------------------------------------

void HtmlInfo::SetFontSizes(int size)
{
    // set font sizes for <FONT SIZE=-2> to <FONT SIZE=+4>
    int f_sizes[7];
    f_sizes[0] = int(size * 0.6);
    f_sizes[1] = int(size * 0.8);
    f_sizes[2] = size;
    f_sizes[3] = int(size * 1.2);
    f_sizes[4] = int(size * 1.4);
    f_sizes[5] = int(size * 1.6);
    f_sizes[6] = int(size * 1.8);
    #ifdef __WXOSX_COCOA__
        SetFonts(wxT("Lucida Grande"), wxT("Monaco"), f_sizes);
    #else
        SetFonts(wxEmptyString, wxEmptyString, f_sizes);
    #endif
}

// -----------------------------------------------------------------------------

void HtmlInfo::ChangeFontSizes(int size)
{
    // changing font sizes resets pos to top, so save and restore pos
    SaveScrollPos();
    SetFontSizes(size);
    RestoreScrollPos();
    panel->UpdateButtons();
}

// -----------------------------------------------------------------------------

// define a panel that contains a HtmlInfo window

BEGIN_EVENT_TABLE(InfoPanel, wxPanel)
    EVT_BUTTON (ID::SmallerButton,  InfoPanel::OnSmallerButton)
    EVT_BUTTON (ID::BiggerButton,   InfoPanel::OnBiggerButton)
END_EVENT_TABLE()

InfoPanel::InfoPanel(MyFrame* parent, wxWindowID id)
    : wxPanel(parent,id), frame(parent)
{
    html = new HtmlInfo(this, frame, wxID_ANY);

    html->SetBorders(0);
    html->SetFontSizes(infofontsize);

    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);

    // add buttons at top of html window
    smallerbutt = new wxButton(this, ID::SmallerButton, _("-"), wxDefaultPosition, wxSize(30,wxDefaultCoord));
    biggerbutt = new wxButton(this, ID::BiggerButton, _("+"), wxDefaultPosition, wxSize(30,wxDefaultCoord));

    #ifdef __WXMAC__
        // nicer to use smaller buttons -- also do for Win/Linux???
        smallerbutt->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
        biggerbutt->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
    #endif

    hbox->Add(smallerbutt, 0, wxLEFT | wxTOP | wxBOTTOM | wxALIGN_LEFT, 10);
    hbox->Add(biggerbutt, 0, wxLEFT | wxTOP | wxBOTTOM | wxALIGN_LEFT, 10);

    status = new wxStaticText(this, wxID_STATIC, wxEmptyString);
    #ifdef __WXMAC__
        status->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
    #endif
    wxBoxSizer* statbox = new wxBoxSizer(wxHORIZONTAL);
    statbox->Add(status);
    hbox->Add(statbox, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 10);

    hbox->AddStretchSpacer(1);
    vbox->Add(hbox, 0, wxALL | wxEXPAND | wxALIGN_TOP, 0);
    vbox->Add(html, 1, wxEXPAND | wxALIGN_TOP, 0);
    SetSizer(vbox);

    UpdateButtons();

    // install event handlers to detect keyboard shortcuts when html window has focus
    html->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(MyFrame::OnKeyDown), NULL, frame);
    html->Connect(wxEVT_CHAR, wxKeyEventHandler(MyFrame::OnChar), NULL, frame);
}

// -----------------------------------------------------------------------------

void InfoPanel::ResetPosition()
{
    html->ResetScrollPos();
}

// -----------------------------------------------------------------------------

void InfoPanel::Update(const AbstractRD* const system)
{
    // build HTML string to display current parameters
    wxString contents;

    contents += wxT("<html><body><table border=0 cellspacing=0 cellpadding=4 width=\"100%\">");

    rownum = 0;

    wxString s(system->GetRuleName().c_str(),wxConvUTF8);
    contents += AppendRow(rule_name_label, rule_name_label, s, true);

    s = wxString(system->GetRuleType().c_str(),wxConvUTF8);
    contents += AppendRow(rule_type_label, rule_type_label, s, false);

    s = wxString(system->GetDescription().c_str(),wxConvUTF8);
    s.Replace(wxT("\n\n"), wxT("<p>"));
    contents += AppendRow(description_label, description_label, s, true, true);

    contents += AppendRow(num_chemicals_label, num_chemicals_label, wxString::Format(wxT("%d"), system->GetNumberOfChemicals()),
                          system->HasEditableNumberOfChemicals());

    for(int iParam=0;iParam<(int)system->GetNumberOfParameters();iParam++)
    {
        contents += AppendRow(system->GetParameterName(iParam), system->GetParameterName(iParam),
                              FormatFloat(system->GetParameterValue(iParam)), true);
    }

    wxString formula = system->GetFormula();
    if(system->HasEditableFormula() || formula.size()>0)
    {
        // escape HTML reserved characters
        formula.Replace(wxT("&"), wxT("&amp;")); // (the order of these is important)
        formula.Replace(wxT("<"), wxT("&lt;"));
        formula.Replace(wxT(">"), wxT("&gt;"));
        // deal with line endings
        formula.Replace(wxT("\r\n"), wxT("<br>"));
        formula.Replace(wxT("\n\r"), wxT("<br>"));
        formula.Replace(wxT("\r"), wxT("<br>"));
        formula.Replace(wxT("\n"), wxT("<br>"));
        // convert whitespace to &nbsp; so we can use the <code> block
        formula.Replace(wxT("  "), wxT("&nbsp;&nbsp;"));
        // (This is a bit of a hack. We only want to keep the leading whitespace on each line, and since &ensp; is not supported we
        //  have to use &nbsp; but this prevents wrapping. By only replacing *double* spaces we cover most usages and it's good enough for now.)
        formula = _("<code>") + formula + _("</code>");
        // (would prefer the <pre> block here but it adds a leading newline (which we can't use CSS to get rid of) and also prevents wrapping)
        if(system->GetRuleType()=="kernel")
            contents += AppendRow(kernel_label, kernel_label, formula, system->HasEditableFormula(), true);
        else
            contents += AppendRow(formula_label, formula_label, formula, system->HasEditableFormula(), true);
    }

    if(system->HasEditableDimensions())
        contents += AppendRow(dimensions_label, dimensions_label, wxString::Format(wxT("%s x %s x %s"),
                                        FormatFloat(system->GetX(),3),FormatFloat(system->GetY(),3),FormatFloat(system->GetZ(),3)),
                                        system->HasEditableDimensions());
    else
        contents += AppendRow(number_of_cells_label, number_of_cells_label, wxString::Format(wxT("%d"),
                                        system->GetNumberOfCells()),false);

    if(!system->HasEditableDimensions() || system->GetRuleType()!="kernel")
    {
        // (hide the neighborhood for image-based kernels, which don't use it)
        contents += AppendRow(neighborhood_type_label, neighborhood_type_label, system->GetNeighborhoodType()+"-neighbors", false);
        contents += AppendRow(neighborhood_range_label, neighborhood_range_label, wxString::Format(wxT("%d"),system->GetNeighborhoodRange()), false);
        contents += AppendRow(neighborhood_weight_label, neighborhood_weight_label, system->GetNeighborhoodWeight(), false);
    }

    /* bit technical, leave as a file-only option
    contents += AppendRow(block_size_label, block_size_label, wxString::Format(wxT("%d x %d x %d"),
                                        system->GetBlockSizeX(),system->GetBlockSizeY(),system->GetBlockSizeZ()),
                                        system->HasEditableBlockSize());*/

    if(system->HasEditableWrapOption())
        contents += AppendRow(wrap_label, wrap_label, system->GetWrap()?_("on"):_("off"), true);

    contents += AppendRow(data_type_label,data_type_label,system->GetDataType()==VTK_DOUBLE?_("double"):_("float"),
        system->HasEditableDataType());

    contents += _T("</table>");

    contents += wxT("<h5><center>");
    contents += _("Render settings:");
    contents += wxT("</h5></center>");
    contents += wxT("<table border=0 cellspacing=0 cellpadding=4 width=\"100%\">");

    rownum = 1;     // nicer if 1st render setting has gray background

    const Properties& render_settings = frame->GetRenderSettings();
    for(int i=0;i<render_settings.GetNumberOfProperties();i++)
    {
        const Property& prop = render_settings.GetProperty(i);
        string name = prop.GetName();
        wxString print_label(name);
        print_label.Replace(_T("_"),_T(" "));
        string type = prop.GetType();
        if(type=="float")
            contents += AppendRow(print_label, name, FormatFloat(prop.GetFloat()), true);
        else if(type=="bool")
            contents += AppendRow(print_label, name, prop.GetBool()?_("true"):_("false"), true);
        else if(type=="int")
            contents += AppendRow(print_label, name, FormatFloat(prop.GetInt()), true);
        else if(type=="color")
        {
            float r,g,b;
            prop.GetColor(r,g,b);
            wxColor col(r*255,g*255,b*255);
            contents += AppendRow(print_label, name, _("RGB = ")+FormatFloat(r,2)+_T(", ")+FormatFloat(g,2)+_T(", ")+FormatFloat(b,2),
                                  true, false, col.GetAsString(wxC2S_HTML_SYNTAX));
        }
        else if(type=="chemical")
            contents += AppendRow(print_label, name, prop.GetChemical(), true);
        else if(type=="axis")
            contents += AppendRow(print_label, name, prop.GetAxis(), true);
        else throw runtime_error("InfoPanel::Update : unrecognised type: "+type);
    }

    contents += _T("</table></body></html>");

    html->SaveScrollPos();
    html->Freeze();             // prevent flicker
    html->SetPage(contents);
    html->RestoreScrollPos();
    html->Thaw();
}

// -----------------------------------------------------------------------------

wxString InfoPanel::AppendRow(const wxString& print_label, const wxString& label, const wxString& value,
                              bool is_editable, bool is_multiline, const wxString& color)
{
    wxString result = (rownum & 1) ? _T("<tr bgcolor=\"#F0F0F0\">") : _T("<tr>");
    rownum++;

    if (is_multiline) {
        result += _T("<td width=3></td><td valign=top width=\"45%\"><b>");
        result += print_label;
        result += _T("</b></td><td valign=top></td>");
        // see below for how value is formatted
    } else {
        result += _T("<td width=3></td><td valign=top width=\"45%\"><b>");
        result += print_label;
        result += _T("</b></td><td valign=top>");
        if (color.empty()) {
            result += value;
        } else {
            // start with a color block to illustrate the color
            // (we put a border around block in case color matches row background)
            result += _T("<table border=0 cellspacing=0 cellpadding=0><tr><td>");
            result += _T("<table border=1 cellspacing=0 cellpadding=6><tr bgcolor=\"");
            result += color;
            result += _T("\"><td width=25></td></tr></table></td><td valign=top>&nbsp;&nbsp;&nbsp;");
            result += value;
            result += _T("</td></tr></table>");
        }
        result += _T("</td>");
    }

    if (is_editable) {
        result += _T("<td valign=top align=right><a href=\"");
        result += change_prefix;
        result += label;
        result += _T("\">");
        result += _("edit");
        result += _T("</a></td>");
    } else {
        result += _T("<td></td>");
    }
    result += _T("<td width=3></td></tr>");

    if (is_multiline) {
        // put text in a new row that spans all columns
        result += (rownum & 1) ? _T("<tr>") : _T("<tr bgcolor=\"#F0F0F0\">");
        result += _T("<td width=3></td><td colspan=3>");
        result += value;
        result += _T("<br></td><td width=3></td></tr>");
    }

    return result;
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeRenderSetting(const wxString& setting)
{
    Property& prop = frame->GetRenderSettings().GetProperty(string(setting.mb_str()));
    string type = prop.GetType();
    if(type=="float")
    {
        float newval;
        float oldval = prop.GetFloat();

        // position dialog box to left of linkrect
        wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
        int dlgwd = 300;
        pos.x -= dlgwd + 20;

        ParameterDialog dialog(frame, false, setting, oldval, pos, wxSize(dlgwd,-1));

        if (dialog.ShowModal() == wxID_OK)
        {
            newval = dialog.GetValue();
            if (newval != oldval) {
                prop.SetFloat(newval);
                frame->RenderSettingsChanged();
            }
        }
    }
    else if(type=="int")
    {
        float newval;
        float oldval = prop.GetInt();

        // position dialog box to left of linkrect
        wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
        int dlgwd = 300;
        pos.x -= dlgwd + 20;

        ParameterDialog dialog(frame, false, setting, oldval, pos, wxSize(dlgwd,-1));

        if (dialog.ShowModal() == wxID_OK)
        {
            newval = dialog.GetValue();
            if (newval != oldval) {
                prop.SetInt((int)newval);
                frame->RenderSettingsChanged();
            }
        }
    }
    else if(type=="bool")
    {
        prop.SetBool(!prop.GetBool());
        frame->RenderSettingsChanged();
    }
    else if(type=="color")
    {
        float r,g,b;
        prop.GetColor(r,g,b);
        wxColour col = wxGetColourFromUser(this,wxColour(r*255,g*255,b*255));
        if(col.IsOk()) // (else user cancelled)
        {
            r = col.Red()/255.0f;
            g = col.Green()/255.0f;
            b = col.Blue()/255.0f;
            prop.SetColor(r,g,b);
            frame->RenderSettingsChanged();
        }
    }
    else if(type=="chemical")
    {
        wxArrayString choices;
        for(int i=0;i<this->frame->GetCurrentRDSystem()->GetNumberOfChemicals();i++)
            choices.Add(GetChemicalName(i));
        wxSingleChoiceDialog dlg(this,_("Chemical:"),_("Select active chemical"),
            choices);
        dlg.SetSelection(IndexFromChemicalName(prop.GetChemical()));
        if(dlg.ShowModal()!=wxID_OK) return;
        prop.SetChemical(GetChemicalName(dlg.GetSelection()));
        frame->RenderSettingsChanged();
    }
    else if(type=="axis")
    {
        wxArrayString choices;
        choices.Add(_T("x"));
        choices.Add(_T("y"));
        choices.Add(_T("z"));
        wxSingleChoiceDialog dlg(this,_("Axis:"),_("Select axis"),choices);
        int iAxis=0;
        if(prop.GetAxis()=="x") iAxis=0;
        else if(prop.GetAxis()=="y") iAxis=1;
        else if(prop.GetAxis()=="z") iAxis=2;
        dlg.SetSelection(iAxis);
        if(dlg.ShowModal()!=wxID_OK) return;
        prop.SetAxis(string(choices[dlg.GetSelection()].mb_str()));
        frame->RenderSettingsChanged();
    }
    else {
        wxMessageBox("Editing "+setting+" of type "+wxString(type.c_str(),wxConvUTF8)+" is not currently supported");
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeParameter(const wxString& parameter)
{
    AbstractRD* sys = frame->GetCurrentRDSystem();
    int iParam = 0;
    while (iParam < (int)sys->GetNumberOfParameters()) {
        if (parameter == sys->GetParameterName(iParam)) break;
        iParam++;
    }
    if (iParam == (int)sys->GetNumberOfParameters()) {
        Warning(_("Bug in ChangeParameter! Unknown parameter: ") + parameter);
        return;
    }

    wxString newname;
    float newval;
    float oldval = sys->GetParameterValue(iParam);
    bool can_edit_name = sys->HasEditableFormula();

    // position dialog box to left of linkrect
    wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
    int dlgwd = 300;
    pos.x -= dlgwd + 20;

    ParameterDialog dialog(frame, can_edit_name, parameter, oldval, pos, wxSize(dlgwd,-1));

    if (dialog.ShowModal() == wxID_OK)
    {
        newname = can_edit_name ? dialog.GetName() : parameter;
        newval = dialog.GetValue();
        if (newname != parameter) frame->SetParameterName(iParam, string(newname.mb_str()));
        if (newval != oldval) frame->SetParameter(iParam, newval);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeRuleName()
{
    wxString oldname(frame->GetCurrentRDSystem()->GetRuleName().c_str(),wxConvUTF8);
    wxString newname;

    // position dialog box to left of linkrect
    wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
    int dlgwd = 300;
    pos.x -= dlgwd + 20;

    if ( GetString(_("Change rule name"), _("Enter the new rule name:"),
                   oldname, newname, pos, wxSize(dlgwd,wxDefaultCoord)) )
    {
        if (newname != oldname) frame->SetRuleName(string(newname.mb_str()));
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeDescription()
{
    wxString oldtext(frame->GetCurrentRDSystem()->GetDescription().c_str(),wxConvUTF8);
    wxString newtext;

    MultiLineDialog dialog(frame, _("Change description"), _("Enter the new description:"), oldtext);

    // best to center potentially large dialog box
    dialog.SetSize(textdlgwd, textdlght);
    dialog.Centre();

    if (dialog.ShowModal() == wxID_OK)
    {
        newtext = dialog.GetValue();
        if (newtext != oldtext) frame->SetDescription(string(newtext.mb_str()));
    }

    textdlgwd = dialog.GetRect().width;
    textdlght = dialog.GetRect().height;
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeFormula()
{
    wxString oldcode(frame->GetCurrentRDSystem()->GetFormula().c_str(),wxConvUTF8);
    wxString newcode;

    wxString code_type(frame->GetCurrentRDSystem()->GetRuleType().c_str(),wxConvUTF8);
    MultiLineDialog dialog(frame, _("Change ")+code_type, _("Enter the new ")+code_type+_T(":"), oldcode);

    // best to center potentially large dialog box
    dialog.SetSize(textdlgwd, textdlght);
    dialog.Centre();

    if (dialog.ShowModal() == wxID_OK)
    {
        newcode = dialog.GetValue();
        if (newcode != oldcode) frame->SetFormula(string(newcode.mb_str()));
    }

    textdlgwd = dialog.GetRect().width;
    textdlght = dialog.GetRect().height;
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeNumChemicals()
{
    const int MAX_CHEMICALS = 1000;

    int oldnum = frame->GetCurrentRDSystem()->GetNumberOfChemicals();
    int newnum;

    // position dialog box to left of linkrect
    wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
    int dlgwd = 300;
    pos.x -= dlgwd + 20;

    if ( GetInteger(_("Change number of chemicals"), _("Enter the new number of chemicals:"),
                    oldnum, 1, MAX_CHEMICALS, &newnum,
                    pos, wxSize(dlgwd,wxDefaultCoord)) )
    {
        if (newnum != oldnum)
            frame->SetNumberOfChemicals(newnum);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeDimensions()
{
    AbstractRD* sys = frame->GetCurrentRDSystem();
    int oldx = sys->GetX();
    int oldy = sys->GetY();
    int oldz = sys->GetZ();
    int newx, newy, newz;

    // position dialog box to left of linkrect
    wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
    int dlgwd = 300;
    pos.x -= dlgwd + 20;

    do // allow the user multiple tries until the change is accepted, they cancel, or they don't change the values but hit OK
    {
        XYZIntDialog dialog(frame, _("Change the dimensions"), oldx, oldy, oldz, pos, wxSize(dlgwd,-1));
        if (dialog.ShowModal() == wxID_CANCEL) break;
        newx = dialog.GetX();
        newy = dialog.GetY();
        newz = dialog.GetZ();
        if (newx == oldx && newy == oldy && newz == oldz) break;
        const bool not_all_powers_of_two = newx&(newx - 1) || newy&(newy - 1) || newz&(newz - 1);
        if (sys->GetRuleType() == "formula" && not_all_powers_of_two)
        {
            wxMessageBox(_("For efficient wrap-around in OpenCL we require all the dimensions to be powers of 2"));
            continue;
        }
        else if (sys->GetRuleType() == "kernel" && not_all_powers_of_two)
        {
            int answer = wxMessageBox(
                _("Check the kernel to see if it supports dimensions that are not powers of 2"),
                _("WARNING: Dimensions are not all powers of 2"),
                wxOK | wxCANCEL);
            if (answer == wxCANCEL)
                continue;
        }
        if(frame->SetDimensions(newx, newy, newz)) break;
    } while(true);
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeBlockSize()
{
    AbstractRD* sys = frame->GetCurrentRDSystem();
    int oldx = sys->GetBlockSizeX();
    int oldy = sys->GetBlockSizeY();
    int oldz = sys->GetBlockSizeZ();
    int newx, newy, newz;

    // position dialog box to left of linkrect
    wxPoint pos = ClientToScreen( wxPoint(html->linkrect.x, html->linkrect.y) );
    int dlgwd = 300;
    pos.x -= dlgwd + 20;

    XYZIntDialog dialog(frame, _("Change the block size"), oldx, oldy, oldz, pos, wxSize(dlgwd,-1));

    if (dialog.ShowModal() == wxID_OK)
    {
        newx = dialog.GetX();
        newy = dialog.GetY();
        newz = dialog.GetZ();
        if (newx != oldx || newy != oldy || newz != oldz)
            frame->SetBlockSize(newx, newy, newz);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeWrapOption()
{
    AbstractRD* sys = frame->GetCurrentRDSystem();
    sys->SetWrap(!sys->GetWrap());
    this->Update(sys);
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeDataType()
{
    AbstractRD* sys = frame->GetCurrentRDSystem();
    switch( sys->GetDataType() ) {
        default:
        case VTK_FLOAT: sys->SetDataType( VTK_DOUBLE ); break;
        case VTK_DOUBLE: sys->SetDataType( VTK_FLOAT ); break;
    }
    this->Update(sys);
    frame->RenderSettingsChanged();
}

// -----------------------------------------------------------------------------

void InfoPanel::ChangeInfo(const wxString& label)
{
    if ( label == rule_name_label ) {
        ChangeRuleName();

    } else if ( label == description_label ) {
        ChangeDescription();

    } else if ( label == num_chemicals_label ) {
        ChangeNumChemicals();

    } else if ( label == formula_label || label == kernel_label ) {
        ChangeFormula();

    } else if ( label == dimensions_label ) {
        ChangeDimensions();

    } else if ( label == block_size_label ) {
        ChangeBlockSize();

    } else if ( label == wrap_label ) {
        ChangeWrapOption();

    } else if ( label == data_type_label ) {
        ChangeDataType();

    } else if ( frame->GetRenderSettings().IsProperty(string(label.mb_str())) ) {
        ChangeRenderSetting(label);

    } else if ( frame->GetCurrentRDSystem()->IsParameter(string(label.mb_str())) ) {
        ChangeParameter(label);
    } else {
        Warning(_("Bug in ChangeInfo! Unexpected label: ") + label);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::OnSmallerButton(wxCommandEvent& WXUNUSED(event))
{
    if ( infofontsize > minfontsize ) {
        infofontsize--;
        html->ChangeFontSizes(infofontsize);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::OnBiggerButton(wxCommandEvent& WXUNUSED(event))
{
    if ( infofontsize < maxfontsize ) {
        infofontsize++;
        html->ChangeFontSizes(infofontsize);
    }
}

// -----------------------------------------------------------------------------

void InfoPanel::UpdateButtons()
{
    smallerbutt->Enable( infofontsize > minfontsize );
    biggerbutt->Enable( infofontsize < maxfontsize );

    html->ClearStatus();
    html->SetFocus();       // for keyboard shortcuts
}

// -----------------------------------------------------------------------------

bool InfoPanel::HtmlHasFocus()
{
    return html->HasFocus();
}

// -----------------------------------------------------------------------------

void InfoPanel::SelectAllText()
{
    html->SelectAll();
}

// -----------------------------------------------------------------------------

void InfoPanel::CopySelection()
{
    wxString text = html->SelectionToText();
    if (text.length() > 0) {
        CopyTextToClipboard(text);
    }
}

// -----------------------------------------------------------------------------

bool InfoPanel::DoKey(int key, int mods)
{
    // first look for keys that should be passed to the default handler
    if ( mods == wxMOD_NONE ) {
        if ( key == WXK_UP || key == WXK_DOWN || key == WXK_LEFT || key == WXK_RIGHT ||
             key == WXK_PAGEUP || key == WXK_PAGEDOWN ) {
            // let default handler see arrow keys and page up/down keys (for scroll bars)
            return false;
        }
    }

    // now do keyboard shortcuts specific to this pane
    if ( mods == wxMOD_NONE ) {
        // there are none at the moment, and probably safer to keep things that way
        // otherwise it can get confusing if the user assigns a keyboard shortcut
        // that conflicts with one hard-wired here
    }

    // finally do other keyboard shortcuts
    frame->ProcessKey(key, mods);
    return true;
}

// -----------------------------------------------------------------------------
