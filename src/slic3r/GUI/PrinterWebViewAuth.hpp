#pragma once

// Tinman: the Device tab's web view signs in to a PrusaLink printer set up with a user and password
// (HTTP digest) by answering the printer's sign-in challenge, as PrusaSlicer does
// (setup_webview_with_credentials). An API key is still sent as a header by PrinterWebView.

#include <string>

class wxWebView;

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

// The host and port of a printer address such as "192.168.1.5", "http://printer.local:8080/" or
// "https://[fe80::1]". The port is the scheme's default when the address has none, and 0 when it
// is not a number. The host keeps its case; brackets and any user info are removed.
struct PrinterUrlAuthority
{
    std::string host;
    int         port{0};
};
PrinterUrlAuthority printer_url_authority(std::string url);

// The sign-in a printer's web view answers HTTP digest challenges with, bound to the printer's host.
struct PrinterWebViewCredentials
{
    std::string host;
    int         port{0};
    std::string user;
    std::string password;

    bool empty() const { return host.empty() || user.empty(); }
    bool operator==(const PrinterWebViewCredentials& rhs) const
    {
        return host == rhs.host && port == rhs.port && user == rhs.user && password == rhs.password;
    }
    bool operator!=(const PrinterWebViewCredentials& rhs) const { return !(*this == rhs); }
};

// Only a PrusaLink printer set to user and password sign-in (HTTP digest) has them, for the host of
// its print_host. API-key printers and every other host type get none.
PrinterWebViewCredentials printer_webview_credentials(const DynamicPrintConfig& printer_config);

// An HTTP sign-in challenge as the web view reports it.
struct PrinterWebViewChallenge
{
    std::string host;
    int         port{0};
    bool        digest{false};
    // The challenge repeats one these credentials already answered: the printer rejected them.
    bool        rejected_these_credentials{false};
};

// A challenge is answered only with credentials, only for HTTP digest, only from the printer's own
// host and port, and never again once the printer rejected the same credentials.
bool printer_webview_answers_challenge(const PrinterWebViewCredentials& credentials, const PrinterWebViewChallenge& challenge);

// What a web view does with a sign-in challenge.
enum class PrinterWebViewAnswer
{
    // Not the printer's challenge: the web view handles it as it does without the hook.
    None,
    // Answer with the credentials.
    Credentials,
    // The printer rejected the credentials. Left to its default handling, WKWebView sends them again
    // at once, so macOS ends the challenge; WebView2 and WebKitGTK show their own sign-in prompt.
    Rejected,
};

// The credentials one web view answers with. Logs its first answer and the first rejection, never
// the user or password.
class PrinterWebViewAuthState
{
public:
    // Returns true when the credentials changed.
    bool set(const PrinterWebViewCredentials& credentials);
    const PrinterWebViewCredentials& credentials() const { return m_credentials; }
    bool is(const std::string& user, const std::string& password) const
    {
        return m_credentials.user == user && m_credentials.password == password;
    }
    PrinterWebViewAnswer answer(const PrinterWebViewChallenge& challenge);

private:
    PrinterWebViewCredentials m_credentials;
    bool                      m_logged_answer{false};
    bool                      m_logged_rejection{false};
};

// Makes web_view answer its printer's HTTP digest challenges with credentials, replacing any set
// before. Empty credentials remove the hook, so the web view handles sign-in as it does without one.
// On Windows the hook needs the WebView2 view, so call this again on wxEVT_WEBVIEW_CREATED.
void set_printer_webview_credentials(wxWebView* web_view, const PrinterWebViewCredentials& credentials);

} // namespace GUI
} // namespace Slic3r
