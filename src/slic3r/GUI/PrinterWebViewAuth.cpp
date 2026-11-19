#include "PrinterWebViewAuth.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/log/trivial.hpp>

#ifdef _WIN32
#include <WebView2.h>
#include <wrl.h>
#include <boost/nowide/convert.hpp>
#include <wx/event.h>
#include <wx/webview.h>
#include <chrono>
#include <map>
#include <memory>
#elif defined(__linux__)
#include <webkit2/webkit2.h>
#include <wx/webview.h>
#endif

namespace Slic3r {
namespace GUI {

PrinterUrlAuthority printer_url_authority(std::string url)
{
    boost::algorithm::trim(url);
    std::string scheme = "http";
    const size_t scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
        scheme = boost::algorithm::to_lower_copy(url.substr(0, scheme_end));
        url.erase(0, scheme_end + 3);
    }
    const size_t authority_end = url.find_first_of("/?#");
    if (authority_end != std::string::npos)
        url.resize(authority_end);
    const size_t at = url.rfind('@');
    if (at != std::string::npos)
        url.erase(0, at + 1);

    PrinterUrlAuthority authority;
    std::string         port;
    if (!url.empty() && url.front() == '[') {
        const size_t end = url.find(']');
        if (end == std::string::npos)
            return {};
        authority.host = url.substr(1, end - 1);
        if (end + 1 < url.size()) {
            if (url[end + 1] != ':')
                return {};
            port = url.substr(end + 2);
        }
    } else {
        const size_t colon = url.find(':');
        authority.host     = url.substr(0, colon);
        if (colon != std::string::npos)
            port = url.substr(colon + 1);
    }

    if (port.empty())
        authority.port = scheme == "https" ? 443 : 80;
    else if (port.size() <= 5 && std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        const int value = std::stoi(port);
        authority.port  = value <= 65535 ? value : 0;
    }
    return authority;
}

PrinterWebViewCredentials printer_webview_credentials(const DynamicPrintConfig& printer_config)
{
    const auto* host_type     = printer_config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    const auto* authorization = printer_config.option<ConfigOptionEnum<AuthorizationType>>("printhost_authorization_type");
    if (host_type == nullptr || host_type->value != htPrusaLink || authorization == nullptr ||
        authorization->value != atUserPassword)
        return {};

    const auto string_value = [&printer_config](const char* key) {
        const auto* option = printer_config.option<ConfigOptionString>(key);
        return option != nullptr ? option->value : std::string();
    };
    const PrinterUrlAuthority authority = printer_url_authority(string_value("print_host"));
    PrinterWebViewCredentials credentials{authority.host, authority.port, string_value("printhost_user"),
                                          string_value("printhost_password")};
    return credentials.empty() ? PrinterWebViewCredentials() : credentials;
}

bool printer_webview_answers_challenge(const PrinterWebViewCredentials& credentials, const PrinterWebViewChallenge& challenge)
{
    if (credentials.empty() || !challenge.digest || challenge.rejected_these_credentials)
        return false;
    std::string host = challenge.host;
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    return challenge.port == credentials.port && boost::algorithm::iequals(host, credentials.host);
}

bool PrinterWebViewAuthState::set(const PrinterWebViewCredentials& credentials)
{
    if (credentials == m_credentials)
        return false;
    m_credentials      = credentials;
    m_logged_answer    = false;
    m_logged_rejection = false;
    return true;
}

PrinterWebViewAnswer PrinterWebViewAuthState::answer(const PrinterWebViewChallenge& challenge)
{
    PrinterWebViewChallenge first_answer    = challenge;
    first_answer.rejected_these_credentials = false;
    if (!printer_webview_answers_challenge(m_credentials, first_answer))
        return PrinterWebViewAnswer::None;
    if (challenge.rejected_these_credentials) {
        if (!std::exchange(m_logged_rejection, true))
            BOOST_LOG_TRIVIAL(warning) << "PrinterWebView: the printer rejected the saved user and password";
        return PrinterWebViewAnswer::Rejected;
    }
    if (!std::exchange(m_logged_answer, true))
        BOOST_LOG_TRIVIAL(info) << "PrinterWebView answering the printer's HTTP digest sign-in";
    return PrinterWebViewAnswer::Credentials;
}

#ifdef _WIN32

namespace {

using Microsoft::WRL::ComPtr;

// One web view's hook. WebView2 reports no failure count, but asks again at once for the same
// address when the printer rejects an answer.
struct EdgeAuth
{
    PrinterWebViewAuthState               state;
    ComPtr<ICoreWebView2_10>              view;
    EventRegistrationToken                token{};
    std::wstring                          answered_uri;
    std::chrono::steady_clock::time_point answered_at;
};

std::map<wxWebView*, std::shared_ptr<EdgeAuth>>& edge_auths()
{
    static std::map<wxWebView*, std::shared_ptr<EdgeAuth>> auths;
    return auths;
}

std::wstring take_string(LPWSTR value)
{
    std::wstring result = value != nullptr ? value : L"";
    CoTaskMemFree(value);
    return result;
}

HRESULT on_edge_auth(const std::weak_ptr<EdgeAuth>& weak_auth, ICoreWebView2BasicAuthenticationRequestedEventArgs* args)
{
    const std::shared_ptr<EdgeAuth> auth = weak_auth.lock();
    if (!auth || args == nullptr)
        return S_OK;

    LPWSTR uri_value       = nullptr;
    LPWSTR challenge_value = nullptr;
    const bool read        = SUCCEEDED(args->get_Uri(&uri_value)) && SUCCEEDED(args->get_Challenge(&challenge_value));
    const std::wstring uri = take_string(uri_value);
    const std::string  header = boost::nowide::narrow(take_string(challenge_value));
    if (!read)
        return S_OK;

    const PrinterUrlAuthority authority = printer_url_authority(boost::nowide::narrow(uri));
    const auto                now       = std::chrono::steady_clock::now();
    PrinterWebViewChallenge   challenge;
    challenge.host   = authority.host;
    challenge.port   = authority.port;
    challenge.digest = boost::algorithm::icontains(header, "digest");
    challenge.rejected_these_credentials = uri == auth->answered_uri && now - auth->answered_at < std::chrono::seconds(3);

    // Unanswered, WebView2 shows its own sign-in prompt, as it does without the hook.
    ComPtr<ICoreWebView2BasicAuthenticationResponse> response;
    if (auth->state.answer(challenge) != PrinterWebViewAnswer::Credentials || FAILED(args->get_Response(&response)) || !response)
        return S_OK;
    const PrinterWebViewCredentials& credentials = auth->state.credentials();
    response->put_UserName(boost::nowide::widen(credentials.user).c_str());
    response->put_Password(boost::nowide::widen(credentials.password).c_str());
    auth->answered_uri = uri;
    auth->answered_at  = now;
    return S_OK;
}

} // namespace

void set_printer_webview_credentials(wxWebView* web_view, const PrinterWebViewCredentials& credentials)
{
    if (web_view == nullptr)
        return;
    auto& auths = edge_auths();
    auto  found = auths.find(web_view);
    if (found == auths.end()) {
        if (credentials.empty())
            return;
        found = auths.emplace(web_view, std::make_shared<EdgeAuth>()).first;
        web_view->Bind(wxEVT_DESTROY, [web_view](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == web_view)
                edge_auths().erase(web_view);
            event.Skip();
        });
    }

    const std::shared_ptr<EdgeAuth>& auth = found->second;
    if (auth->state.set(credentials))
        auth->answered_uri.clear();
    if (credentials.empty()) {
        if (auth->view)
            auth->view->remove_BasicAuthenticationRequested(auth->token);
        auth->view.Reset();
        return;
    }
    if (auth->view)
        return;

    // Null until WebView2 has created the view; wxEVT_WEBVIEW_CREATED calls this again.
    auto* core = static_cast<ICoreWebView2*>(web_view->GetNativeBackend());
    if (core == nullptr)
        return;
    ComPtr<ICoreWebView2_10> view;
    if (FAILED(core->QueryInterface(IID_PPV_ARGS(&view)))) {
        BOOST_LOG_TRIVIAL(warning) << "PrinterWebView: this WebView2 runtime can't answer the printer's sign-in";
        return;
    }
    const std::weak_ptr<EdgeAuth> weak_auth = auth;
    const HRESULT added = view->add_BasicAuthenticationRequested(
        Microsoft::WRL::Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
            [weak_auth](ICoreWebView2*, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) {
                return on_edge_auth(weak_auth, args);
            })
            .Get(),
        &auth->token);
    if (SUCCEEDED(added))
        auth->view = view;
    else
        BOOST_LOG_TRIVIAL(warning) << "PrinterWebView: could not add the printer sign-in handler";
}

#elif defined(__linux__)

namespace {

constexpr const char* auth_state_key   = "tinman-printer-auth";
constexpr const char* auth_handler_key = "tinman-printer-auth-handler";

std::string text_or_empty(const gchar* text) { return text != nullptr ? text : ""; }

gboolean on_authenticate(WebKitWebView* view, WebKitAuthenticationRequest* request, gpointer /*user_data*/)
{
    auto* state = static_cast<PrinterWebViewAuthState*>(g_object_get_data(G_OBJECT(view), auth_state_key));
    if (state == nullptr)
        return FALSE;

    PrinterWebViewChallenge challenge;
    challenge.host   = text_or_empty(webkit_authentication_request_get_host(request));
    challenge.port   = static_cast<int>(webkit_authentication_request_get_port(request));
    challenge.digest = webkit_authentication_request_get_scheme(request) == WEBKIT_AUTHENTICATION_SCHEME_HTTP_DIGEST;
    if (webkit_authentication_request_is_retry(request)) {
        WebKitCredential* proposed = webkit_authentication_request_get_proposed_credential(request);
        challenge.rejected_these_credentials = proposed == nullptr ||
                                               state->is(text_or_empty(webkit_credential_get_username(proposed)),
                                                         text_or_empty(webkit_credential_get_password(proposed)));
        if (proposed != nullptr)
            webkit_credential_free(proposed);
    }

    // Unanswered, WebKitGTK shows its own sign-in prompt, as it does without the hook.
    if (state->answer(challenge) != PrinterWebViewAnswer::Credentials)
        return FALSE;
    // Not stored, so a credential the printer rejected is never sent again without asking.
    const PrinterWebViewCredentials& credentials = state->credentials();
    WebKitCredential* credential = webkit_credential_new(credentials.user.c_str(), credentials.password.c_str(),
                                                         WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
    webkit_authentication_request_authenticate(request, credential);
    webkit_credential_free(credential);
    return TRUE;
}

void delete_state(gpointer state) { delete static_cast<PrinterWebViewAuthState*>(state); }

} // namespace

void set_printer_webview_credentials(wxWebView* web_view, const PrinterWebViewCredentials& credentials)
{
    auto* view = web_view != nullptr ? static_cast<WebKitWebView*>(web_view->GetNativeBackend()) : nullptr;
    if (view == nullptr)
        return;
    GObject* object = G_OBJECT(view);
    if (credentials.empty()) {
        if (const gsize handler = GPOINTER_TO_SIZE(g_object_get_data(object, auth_handler_key)); handler != 0) {
            g_signal_handler_disconnect(view, handler);
            g_object_set_data(object, auth_handler_key, nullptr);
        }
        g_object_set_data(object, auth_state_key, nullptr); // deletes the state
        return;
    }

    auto* state = static_cast<PrinterWebViewAuthState*>(g_object_get_data(object, auth_state_key));
    if (state == nullptr) {
        state = new PrinterWebViewAuthState();
        g_object_set_data_full(object, auth_state_key, state, &delete_state);
        const gulong handler = g_signal_connect(view, "authenticate", G_CALLBACK(on_authenticate), nullptr);
        g_object_set_data(object, auth_handler_key, GSIZE_TO_POINTER(handler));
    }
    state->set(credentials);
}

#elif !defined(__APPLE__)

void set_printer_webview_credentials(wxWebView* /*web_view*/, const PrinterWebViewCredentials& /*credentials*/) {}

#endif // macOS: PrinterWebViewAuthMac.mm

} // namespace GUI
} // namespace Slic3r
