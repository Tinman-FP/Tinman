#ifndef slic3r_ProfileMigrationDialog_hpp_
#define slic3r_ProfileMigrationDialog_hpp_

#include <string>
#include <vector>

#include "GUI_Utils.hpp"
#include "libslic3r/ProfileScopeMigration.hpp"

class wxCheckBox;
class wxChoice;
class wxStaticText;
class Button;

namespace Slic3r { namespace GUI {

// Tinman: copy existing process and filament profiles onto the selected physical printer
// (ProfileScopeMigration.hpp). With an empty folder it lists the library profiles that today
// can be used only on that printer's model and nozzle; otherwise the profile files in the
// folder. Nothing is copied until the user ticks profiles and confirms; the originals are
// never changed, and library originals can afterwards be hidden (and shown again).
class ProfileMigrationDialog : public DPIDialog
{
public:
    ProfileMigrationDialog(wxWindow *parent, const ProfileScopeSaveSelection &target, const std::string &folder = {});

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct Row {
        wxCheckBox *check  = nullptr;
        wxChoice   *nozzle = nullptr; // filaments on a multi-nozzle printer
    };

    ProfileScopeSaveSelection         m_target;
    bool                              m_import;
    std::vector<ProfileMoveCandidate> m_candidates;
    std::vector<Row>                  m_rows;
    wxCheckBox                       *m_select_all  = nullptr;
    wxStaticText                     *m_status      = nullptr;
    wxStaticText                     *m_hidden_text = nullptr;
    Button                           *m_show_hidden = nullptr;
    Button                           *m_copy        = nullptr;

    void update_controls();
    void copy_selected();
    void show_hidden_originals();
    void update_hidden_line();
};

}} // namespace Slic3r::GUI

#endif
