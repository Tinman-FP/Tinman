#ifndef slic3r_PhysicalPrinterDialog_hpp_
#define slic3r_PhysicalPrinterDialog_hpp_

#include "libslic3r/PrintConfig.hpp"
#include <string>
#include <vector>
#include <memory>

#include <wx/event.h>
#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Button.hpp"

class wxTextCtrl;
class wxStaticText;
class Button;
class wxBoxSizer;

namespace Slic3r {

namespace GUI {

// Tinman: a printer switched to PrusaLink with no API key, user or password yet signs in with a user
// and password (HTTP digest), the default of current Prusa firmware. Printers already set up keep
// their sign-in.
bool prusalink_defaults_to_digest(const DynamicPrintConfig& config);

//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

class ConfigOptionsGroup;
class PhysicalPrinterDialog : public DPIDialog
{
    Preset m_original_preset;
    Preset m_draft_preset;
    DynamicPrintConfig* m_config            { nullptr };
    std::unique_ptr<ConfigOptionsGroup> m_optgroup;

    Button*     m_printhost_browse_btn              {nullptr};
    Button*     m_printhost_test_btn                {nullptr};
    Button*     m_printhost_generate_creds_btn      {nullptr};
    Button*     m_printhost_logout_btn              {nullptr};
    Button*     m_printhost_cafile_browse_btn       {nullptr};
    Button*     m_printhost_port_browse_btn         {nullptr};

    RoundedRectangle*   m_input_area                        {nullptr};
    wxStaticText*       m_valid_label                       {nullptr};
    wxTextCtrl*         m_input_ctrl                        {nullptr};
    Button*             btnOK                               {nullptr};
    Button*             m_add_unit_btn                      {nullptr};
    Button*             m_move_profiles_btn                 {nullptr};
    Button*             m_import_profiles_btn               {nullptr};

    void build_printhost_settings(ConfigOptionsGroup* optgroup);
    void OnOK(wxEvent& event);
    // Create a separate physical printer of this model with its own machine profile.
    void add_physical_unit();
    // Copy existing profiles (from the library, or from a folder of files) onto this physical printer.
    void move_profiles(bool from_folder);

public:
    PhysicalPrinterDialog(wxWindow* parent);
    ~PhysicalPrinterDialog();

    enum ValidationType
    {
        Valid,
        NoValid,
        Warning
    };
    PresetCollection* m_presets {nullptr};
    ValidationType  m_valid_type;
    std::string     m_preset_name;

    void        update(bool printer_change = false);
    void        update_host_type(bool printer_change);
    void        update_preset_input();
    void        update_printhost_buttons();
    void        update_printers();
    void        update_ports();
    void        update_webui();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {};
    void check_host_key_valid();
};


} // namespace GUI
} // namespace Slic3r

#endif
