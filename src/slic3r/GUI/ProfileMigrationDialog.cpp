#include "ProfileMigrationDialog.hpp"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <boost/filesystem/path.hpp>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r { namespace GUI {

namespace {
const wxColour grey(107, 107, 107);
const wxColour warning(255, 111, 0);

// What the app shows after any change to the process and filament lists, as a Tab save does.
void refresh_profile_lists()
{
    for (const Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT}) {
        if (Tab *tab = wxGetApp().get_tab(type))
            tab->update_tab_ui();
        wxGetApp().plater()->sidebar().update_presets(type);
    }
}
} // namespace

ProfileMigrationDialog::ProfileMigrationDialog(wxWindow *parent, const ProfileScopeSaveSelection &target, const std::string &folder)
    : DPIDialog(parent, wxID_ANY, folder.empty() ? _L("Move profiles to this printer") : _L("Import profiles to this printer"),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_target(target)
    , m_import(!folder.empty())
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    const int border = FromDIP(10);
    const int wrap   = FromDIP(560);
    const wxString printer = from_u8(m_target.physical_printer_name);

    wxString error;
    try {
        const PresetBundle &bundle = *wxGetApp().preset_bundle;
        m_candidates = m_import ? profiles_to_import(bundle, m_target, folder) : profiles_to_move(bundle, m_target);
    } catch (const std::exception &err) {
        error = from_u8(err.what());
    }

    auto *top   = new wxBoxSizer(wxVERTICAL);
    auto *title = new wxStaticText(this, wxID_ANY, format_wxstr(m_import ? _L("Import profiles to \"%1%\"") : _L("Move profiles to \"%1%\""), printer));
    title->SetFont(::Label::Head_14);
    top->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
    const wxString intro =
        m_import ?
            format_wxstr(_L("Profile files found in the folder \"%1%\". Each ticked profile is added for \"%2%\" alone, "
                            "named \"<profile> - %2%\". The files are not changed."),
                         from_path(boost::filesystem::path(folder).filename()), printer) :
            format_wxstr(_L("These process and filament profiles can today be used only on printers of this model with this "
                            "nozzle, so every such printer shares them. Tick the ones tuned on \"%1%\": each is copied for "
                            "\"%1%\" alone, named \"<profile> - %1%\". The originals are not changed."),
                         printer);
    auto *intro_text = new wxStaticText(this, wxID_ANY, intro + "\n" + _L("Profiles are copied as saved; unsaved changes are not included."));
    intro_text->Wrap(wrap);
    if (m_import)
        intro_text->SetToolTip(from_u8(folder));
    top->Add(intro_text, 0, wxEXPAND | wxALL, border);

    if (!error.IsEmpty() || m_candidates.empty()) {
        auto *empty = new wxStaticText(this, wxID_ANY, !error.IsEmpty() ? error : m_import ?
            _L("No profile files (*.json) were found in this folder.") :
            _L("No profiles are tied only to this printer's model and nozzle. Profiles shared with other printer models, "
               "and profiles that already belong to a physical printer, are not listed."));
        empty->SetForegroundColour(error.IsEmpty() ? grey : warning);
        empty->Wrap(wrap);
        top->Add(empty, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
    } else {
        m_select_all = new wxCheckBox(this, wxID_ANY, _L("Select all"));
        top->Add(m_select_all, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

        auto *list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(wrap, FromDIP(360)), wxVSCROLL);
        list->SetBackgroundColour(*wxWHITE);
        list->SetScrollRate(0, FromDIP(10));
        // One row per profile: the checkbox, then the wrapped name, details and, for a
        // filament on a multi-nozzle printer, the nozzle choice under it. Long names wrap
        // instead of pushing the nozzle choice out of view.
        const int text_wrap = wrap - FromDIP(64);
        auto     *rows      = new wxBoxSizer(wxVERTICAL);
        for (const auto &candidate : m_candidates) {
            const Preset &source = candidate.source;
            const wxString type  = source.type == Preset::TYPE_PRINT ? _L("Process") :
                                   source.type == Preset::TYPE_FILAMENT ? _L("Filament") : _L("File");
            const wxString name  = source.name.empty() ? from_path(boost::filesystem::path(candidate.source_file).filename()) :
                                                         from_u8(source.name);
            Row row;
            row.check = new wxCheckBox(list, wxID_ANY, wxEmptyString);
            row.check->SetToolTip(m_import ? from_u8(candidate.source_file) : name);
            row.check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { update_controls(); });
            auto *title = new wxStaticText(list, wxID_ANY, type + ": " + name);
            title->Wrap(text_wrap);
            title->Bind(wxEVT_LEFT_DOWN, [this, check = row.check](wxMouseEvent &) {
                if (check->IsEnabled()) {
                    check->SetValue(!check->GetValue());
                    update_controls();
                }
            });

            wxString details;
            if (!candidate.problem.empty()) {
                row.check->Disable();
                title->SetForegroundColour(grey);
                details = from_u8(candidate.problem);
            } else {
                wxString made_for;
                for (size_t i = 0; i < candidate.made_for.size() && i < 2; ++i)
                    made_for += (i ? ", " : "") + from_u8(candidate.made_for[i]);
                if (candidate.made_for.size() > 2)
                    made_for += " " + format_wxstr(_L("and %1% more"), candidate.made_for.size() - 2);
                details = format_wxstr(_L("Made for: %1%"), made_for.IsEmpty() ? _L("any printer") : made_for) + "\n" +
                          format_wxstr(_L("Copy: %1%"), from_u8(candidate.copy_name));
                if (!candidate.existing_copy.empty())
                    details += "\n" + format_wxstr(_L("Already copied for this printer as \"%1%\"."), from_u8(candidate.existing_copy));
            }
            auto *text = new wxStaticText(list, wxID_ANY, details);
            text->SetForegroundColour(candidate.problem.empty() ? grey : warning);
            text->Wrap(text_wrap);
            auto *column = new wxBoxSizer(wxVERTICAL);
            column->Add(title, 0);
            column->Add(text, 0, wxTOP, FromDIP(2));

            // A filament is tuned on one nozzle; on a multi-nozzle printer the user has to say which.
            if (candidate.problem.empty() && source.type == Preset::TYPE_FILAMENT && !m_target.nozzle_tool_index) {
                row.nozzle = new wxChoice(list, wxID_ANY);
                row.nozzle->Append(_L("Choose the nozzle it was tuned on"));
                for (size_t i = 0; i < m_target.nozzle_diameters.size(); ++i)
                    row.nozzle->Append(format_wxstr(_L("Nozzle %1% (%2% mm)"), i + 1, m_target.nozzle_diameters[i]));
                row.nozzle->SetSelection(0);
                row.nozzle->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { update_controls(); });
                column->Add(row.nozzle, 0, wxTOP, FromDIP(4));
            }
            auto *line = new wxBoxSizer(wxHORIZONTAL);
            line->Add(row.check, 0, wxRIGHT, FromDIP(6));
            line->Add(column, 1, wxEXPAND);
            rows->Add(line, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
            m_rows.push_back(row);
        }
        list->SetSizer(rows);
        list->FitInside();
        top->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, border);

        m_select_all->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
            for (auto &row : m_rows)
                if (row.check->IsEnabled())
                    row.check->SetValue(m_select_all->GetValue());
            update_controls();
        });
    }

    m_status = new wxStaticText(this, wxID_ANY, "");
    m_status->SetForegroundColour(warning);
    top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);

    if (!m_import) {
        auto *hidden = new wxBoxSizer(wxHORIZONTAL);
        m_hidden_text = new wxStaticText(this, wxID_ANY, "");
        m_hidden_text->SetForegroundColour(grey);
        m_show_hidden = new Button(this, _L("Show them again"));
        m_show_hidden->SetStyle(ButtonStyle::Regular, ButtonType::Parameter);
        m_show_hidden->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_hidden_originals(); });
        hidden->Add(m_hidden_text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
        hidden->Add(m_show_hidden, 0, wxALIGN_CENTER_VERTICAL);
        top->Add(hidden, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
    }

    auto *buttons = new DialogButtons(this, {"OK", "Cancel"});
    m_copy        = buttons->GetOK();
    m_copy->SetLabel(_L("Copy selected"));
    m_copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { copy_selected(); });
    buttons->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
    top->Add(buttons, 0, wxEXPAND | wxTOP, border);

    SetSizer(top);
    update_hidden_line();
    update_controls();
    top->SetSizeHints(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ProfileMigrationDialog::update_controls()
{
    size_t ticked = 0;
    bool   nozzle_missing = false;
    for (const auto &row : m_rows) {
        if (!row.check->GetValue())
            continue;
        ++ticked;
        nozzle_missing = nozzle_missing || (row.nozzle && row.nozzle->GetSelection() <= 0);
    }
    m_status->SetLabel(nozzle_missing ? _L("Choose the nozzle each ticked filament was tuned on.") : wxString());
    m_status->Show(nozzle_missing);
    m_copy->Enable(ticked > 0 && !nozzle_missing);
    Layout();
}

void ProfileMigrationDialog::copy_selected()
{
    std::vector<ProfileMoveRequest> requests;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].check->GetValue())
            continue;
        ProfileMoveRequest request{&m_candidates[i].source, m_candidates[i].copy_name, std::nullopt};
        if (m_rows[i].nozzle && m_rows[i].nozzle->GetSelection() > 0)
            request.nozzle_tool_index = size_t(m_rows[i].nozzle->GetSelection() - 1);
        requests.push_back(std::move(request));
    }
    if (requests.empty())
        return;

    PresetBundle            &bundle = *wxGetApp().preset_bundle;
    std::vector<std::string> saved;
    try {
        saved = copy_profiles_to_physical_printer(bundle, m_target, requests);
    } catch (const std::exception &err) {
        refresh_profile_lists(); // a failure part-way may have saved some copies
        show_error(this, _L("The profiles were not copied.") + "\n\n" + from_u8(err.what()));
        return;
    }
    // Let the sync service upload the new profiles, as a Tab save does.
    for (const auto &request : requests) {
        PresetCollection &collection = request.source->type == Preset::TYPE_PRINT ? static_cast<PresetCollection &>(bundle.prints) :
                                                                                    static_cast<PresetCollection &>(bundle.filaments);
        if (Preset *copy = collection.find_preset(request.copy_name, false, true)) {
            copy->sync_info = "create";
            if (wxGetApp().is_user_login())
                copy->user_id = wxGetApp().getAgent()->get_user_id();
            copy->save_info();
        }
    }
    refresh_profile_lists();

    wxString message = format_wxstr(_L("%1% profiles were copied for \"%2%\"."), saved.size(), from_u8(m_target.physical_printer_name));
    if (!m_import) {
        message += "\n\n" + _L("Hide the originals? They stay in your library unchanged and can be shown again from this dialog. "
                               "Other printers that still use them will no longer list them. A profile that is selected now "
                               "stays listed until Tinman restarts.");
        MessageDialog ask(this, message, _L("Profiles copied"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        if (ask.ShowModal() == wxID_YES) {
            try {
                for (const Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT}) {
                    std::vector<std::string> names;
                    for (const auto &request : requests)
                        if (request.source->type == type)
                            names.push_back(request.source->name);
                    if (!names.empty())
                        set_profiles_hidden(bundle, type, names, true);
                }
            } catch (const std::exception &err) {
                show_error(this, _L("The originals could not be hidden.") + "\n\n" + from_u8(err.what()));
            }
            refresh_profile_lists();
        }
    } else {
        MessageDialog(this, message, _L("Profiles imported"), wxOK | wxICON_INFORMATION).ShowModal();
    }
    EndModal(wxID_OK);
}

void ProfileMigrationDialog::update_hidden_line()
{
    if (!m_hidden_text)
        return;
    const PresetBundle &bundle = *wxGetApp().preset_bundle;
    const size_t count = hidden_profiles(bundle, Preset::TYPE_PRINT).size() + hidden_profiles(bundle, Preset::TYPE_FILAMENT).size();
    m_hidden_text->SetLabel(format_wxstr(_L("%1% profiles in your library are hidden from the lists."), count));
    m_hidden_text->Show(count > 0);
    m_show_hidden->Show(count > 0);
    Layout();
}

void ProfileMigrationDialog::show_hidden_originals()
{
    PresetBundle &bundle = *wxGetApp().preset_bundle;
    try {
        for (const Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT})
            set_profiles_hidden(bundle, type, hidden_profiles(bundle, type), false);
    } catch (const std::exception &err) {
        show_error(this, _L("The hidden profiles could not be shown again.") + "\n\n" + from_u8(err.what()));
    }
    refresh_profile_lists();
    update_hidden_line();
}

void ProfileMigrationDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    if (m_show_hidden)
        m_show_hidden->Rescale();
    m_copy->Rescale();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
