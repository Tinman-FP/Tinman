#ifndef slic3r_SavePresetDialog_hpp_
#define slic3r_SavePresetDialog_hpp_

//#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrinterConnectionUpdate.hpp"
#include "libslic3r/ProfileScopeSave.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/RadioGroup.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/TextInput.hpp"

class wxString;
class wxStaticText;
class wxComboBox;
class wxStaticBitmap;
class wxChoice;

#define SAVE_PRESET_DIALOG_DEF_COLOUR wxColour(255, 255, 255)
#define SAVE_PRESET_DIALOG_INPUT_SIZE wxSize(FromDIP(360), FromDIP(24))
#define SAVE_PRESET_DIALOG_BUTTON_SIZE wxSize(FromDIP(60), FromDIP(24))

namespace Slic3r {
namespace GUI {

class SavePresetDialog : public DPIDialog
{
    enum ActionType
    {
        ChangePreset,
        AddPreset,
        Switch, 
        UndefAction
    };

    class Item : public wxWindow
    {
    public:
        enum ValidationType
        {
            Valid,
            NoValid,
            Warning
        };

        Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent);

        void            update_valid_bmp();
        virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

        bool            is_valid()      const { return m_valid_type != NoValid; }
        Preset::Type    type()          const { return m_type; }
        std::string     preset_name()   const { return m_preset_name; }
        //BBS: add project embedded preset relate logic
        bool save_to_project() const { return m_save_to_project; }

        // Method to get detach state
        bool is_detached() const { return m_detach; }
        // Per-printer scope chosen for a process or filament profile; Preserve when not offered.
        ProfileScopeSaveSelection profile_scope() const;

        Preset::Type    m_type;
        ValidationType  m_valid_type;
        std::string		m_preset_name;

        SavePresetDialog*   m_parent        {nullptr};
        wxStaticBitmap*     m_valid_bmp     {nullptr};
        wxComboBox*         m_combo         {nullptr};
        TextInput*         m_input_ctrl    {nullptr};
        wxStaticText*       m_valid_label   {nullptr};

        PresetCollection*   m_presets       {nullptr};

        //BBS: add project embedded preset relate logic
        bool                m_save_to_project {false};
        RadioGroup*         m_radio_group; // ORCA
        bool                m_detach{false};
        wxCheckBox*         m_detach_checkbox{nullptr};

        // Per-printer scope, shown only when a physical printer with an identity is
        // selected or the profile already has a scope.
        wxChoice*                            m_scope_choice{nullptr};
        wxChoice*                            m_scope_nozzle{nullptr};
        std::vector<ProfileScopeSaveMode>    m_scope_modes;
        std::optional<ProfileScopeSaveSelection> m_scope_target;
        std::string                          m_initial_name;
        std::string                          m_suggested_name;

        void add_profile_scope(wxBoxSizer *sizer);
        void on_profile_scope_changed();
        void update();
    };

    std::vector<Item*>   m_items;

    wxBoxSizer*         m_presets_sizer     {nullptr};
    wxStaticText*       m_label             {nullptr};
    wxBoxSizer*         m_radio_sizer       {nullptr};  
    ActionType          m_action            {UndefAction};
    int                 m_mode;

    std::string         m_ph_printer_name;
    std::string         m_old_preset_name;

public:
    SavePresetDialog(wxWindow* parent, Preset::Type type, int mode = 0, std::string suffix = "");
    SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, int mode = 0, std::string suffix = "");
    ~SavePresetDialog();

    void AddItem(Preset::Type type, const std::string& suffix);

    std::string get_name();
    std::string get_name(Preset::Type type);
    void input_name_from_other(std::string new_preset_name);
    bool confirm_from_other();
    PendingPhysicalPrinterUpdate pending_physical_printer_update(const std::string &preset_name) const;

    bool enable_ok_btn() const;
    void add_info_for_edit_ph_printer(wxBoxSizer *sizer);
    void update_info_for_edit_ph_printer(const std::string &preset_name);
    void layout();
    //BBS: add project embedded preset relate logic
    bool get_save_to_project_selection(Preset::Type type);
    // Method to get the detach state
    bool get_detach_value(Preset::Type type);
    ProfileScopeSaveSelection get_profile_scope_selection(Preset::Type type) const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {}

private:
    void build(std::vector<Preset::Type> types, std::string suffix = "");
    void on_select_cancel(wxCommandEvent &event);
    void accept(wxCommandEvent &event);
};

} // namespace GUI
} // namespace Slic3r

#endif
