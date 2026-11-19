#include <catch2/catch_test_macros.hpp>

#include <string>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/PhysicalPrinterDialog.hpp"
#include "slic3r/GUI/PrinterWebViewAuth.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

DynamicPrintConfig printer_connection(PrintHostType host_type, AuthorizationType authorization, const std::string& host,
                                      const std::string& user, const std::string& password, const std::string& apikey = {})
{
    DynamicPrintConfig config;
    config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(host_type));
    config.set_key_value("printhost_authorization_type", new ConfigOptionEnum<AuthorizationType>(authorization));
    config.set_key_value("print_host", new ConfigOptionString(host));
    config.set_key_value("printhost_user", new ConfigOptionString(user));
    config.set_key_value("printhost_password", new ConfigOptionString(password));
    config.set_key_value("printhost_apikey", new ConfigOptionString(apikey));
    return config;
}

PrinterWebViewChallenge digest_challenge(const std::string& host, int port)
{
    PrinterWebViewChallenge challenge;
    challenge.host   = host;
    challenge.port   = port;
    challenge.digest = true;
    return challenge;
}

} // namespace

TEST_CASE("A printer address gives its host and port", "[PrinterWebViewAuth]")
{
    const auto check = [](const std::string& url, const std::string& host, int port) {
        const PrinterUrlAuthority authority = printer_url_authority(url);
        CHECK(authority.host == host);
        CHECK(authority.port == port);
    };
    check("192.168.50.176", "192.168.50.176", 80);
    check("  http://192.168.50.176/  ", "192.168.50.176", 80);
    check("http://Prusa-INDX.local:8080/api/v1/status", "Prusa-INDX.local", 8080);
    check("https://printer.example", "printer.example", 443);
    check("HTTPS://[fe80::1]/", "fe80::1", 443);
    check("http://[fe80::1]:8080", "fe80::1", 8080);
    check("maker:secret@10.0.0.2:81", "10.0.0.2", 81);
    check("10.0.0.2?page=1", "10.0.0.2", 80);
    // An address that isn't one never matches a challenge.
    check("10.0.0.2:http", "10.0.0.2", 0);
    check("10.0.0.2:99999", "10.0.0.2", 0);
    check("http://[fe80::1", "", 0);
}

TEST_CASE("Only a PrusaLink printer set to user and password has web view sign-in", "[PrinterWebViewAuth]")
{
    SECTION("PrusaLink with HTTP digest")
    {
        const PrinterWebViewCredentials credentials = printer_webview_credentials(
            printer_connection(htPrusaLink, atUserPassword, "192.168.50.176", "maker", "pw"));
        CHECK(credentials.host == "192.168.50.176");
        CHECK(credentials.port == 80);
        CHECK(credentials.user == "maker");
        CHECK(credentials.password == "pw");
        CHECK_FALSE(credentials.empty());
    }
    SECTION("PrusaLink with an API key, even with a user and password saved")
    {
        CHECK(printer_webview_credentials(printer_connection(htPrusaLink, atKeyPassword, "192.168.50.176", "maker", "pw", "key"))
                  .empty());
    }
    SECTION("Other host types with a user and password")
    {
        for (const PrintHostType host_type : {htOctoPrint, htPrusaConnect, htMoonraker, htUltiMaker})
            CHECK(printer_webview_credentials(printer_connection(host_type, atUserPassword, "192.168.50.176", "maker", "pw"))
                      .empty());
    }
    SECTION("No user or no host")
    {
        CHECK(printer_webview_credentials(printer_connection(htPrusaLink, atUserPassword, "192.168.50.176", "", "pw")).empty());
        CHECK(printer_webview_credentials(printer_connection(htPrusaLink, atUserPassword, "", "maker", "pw")).empty());
    }
    SECTION("A config without connection options")
    {
        CHECK(printer_webview_credentials(DynamicPrintConfig()).empty());
    }
}

TEST_CASE("Web view sign-in answers only its printer's HTTP digest challenge", "[PrinterWebViewAuth]")
{
    const PrinterWebViewCredentials credentials = printer_webview_credentials(
        printer_connection(htPrusaLink, atUserPassword, "http://Prusa-INDX.local", "maker", "pw"));

    CHECK(printer_webview_answers_challenge(credentials, digest_challenge("Prusa-INDX.local", 80)));
    CHECK(printer_webview_answers_challenge(credentials, digest_challenge("prusa-indx.LOCAL", 80)));

    CHECK_FALSE(printer_webview_answers_challenge(credentials, digest_challenge("192.168.50.3", 80)));
    CHECK_FALSE(printer_webview_answers_challenge(credentials, digest_challenge("Prusa-INDX.local", 8080)));
    CHECK_FALSE(printer_webview_answers_challenge(PrinterWebViewCredentials(), digest_challenge("Prusa-INDX.local", 80)));

    PrinterWebViewChallenge basic = digest_challenge("Prusa-INDX.local", 80);
    basic.digest                  = false;
    CHECK_FALSE(printer_webview_answers_challenge(credentials, basic));

    PrinterWebViewChallenge rejected    = digest_challenge("Prusa-INDX.local", 80);
    rejected.rejected_these_credentials = true;
    CHECK_FALSE(printer_webview_answers_challenge(credentials, rejected));

    const PrinterWebViewCredentials ipv6 = printer_webview_credentials(
        printer_connection(htPrusaLink, atUserPassword, "http://[fe80::1]:8080/", "maker", "pw"));
    CHECK(printer_webview_answers_challenge(ipv6, digest_challenge("[fe80::1]", 8080)));
    CHECK(printer_webview_answers_challenge(ipv6, digest_challenge("fe80::1", 8080)));
}

TEST_CASE("A web view's sign-in state answers until the printer rejects it", "[PrinterWebViewAuth]")
{
    const PrinterWebViewCredentials credentials = printer_webview_credentials(
        printer_connection(htPrusaLink, atUserPassword, "127.0.0.1:8765", "maker", "pw"));
    PrinterWebViewAuthState state;
    CHECK(state.set(credentials));
    CHECK_FALSE(state.set(credentials));
    CHECK(state.is("maker", "pw"));
    CHECK_FALSE(state.is("maker", "other"));

    CHECK(state.answer(digest_challenge("127.0.0.1", 8765)) == PrinterWebViewAnswer::Credentials);
    CHECK(state.credentials().user == "maker");
    CHECK(state.credentials().password == "pw");

    PrinterWebViewChallenge rejected    = digest_challenge("127.0.0.1", 8765);
    rejected.rejected_these_credentials = true;
    CHECK(state.answer(rejected) == PrinterWebViewAnswer::Rejected);
    // Another host's challenge, rejected or not, is never the printer's.
    CHECK(state.answer(digest_challenge("127.0.0.1", 80)) == PrinterWebViewAnswer::None);
    PrinterWebViewChallenge other_rejected    = digest_challenge("192.168.50.3", 8765);
    other_rejected.rejected_these_credentials = true;
    CHECK(state.answer(other_rejected) == PrinterWebViewAnswer::None);

    CHECK(state.set(PrinterWebViewCredentials()));
    CHECK(state.answer(digest_challenge("127.0.0.1", 8765)) == PrinterWebViewAnswer::None);
    CHECK(state.answer(rejected) == PrinterWebViewAnswer::None);
}

TEST_CASE("A printer newly switched to PrusaLink defaults to user and password sign-in", "[PrinterWebViewAuth]")
{
    CHECK(prusalink_defaults_to_digest(printer_connection(htPrusaLink, atKeyPassword, "192.168.50.176", "", "")));
    CHECK(prusalink_defaults_to_digest(printer_connection(htPrusaLink, atKeyPassword, "", "", "")));

    // A printer that already has a sign-in keeps it.
    CHECK_FALSE(prusalink_defaults_to_digest(printer_connection(htPrusaLink, atKeyPassword, "192.168.50.176", "", "", "key")));
    CHECK_FALSE(prusalink_defaults_to_digest(printer_connection(htPrusaLink, atKeyPassword, "192.168.50.176", "maker", "")));
    CHECK_FALSE(prusalink_defaults_to_digest(printer_connection(htPrusaLink, atKeyPassword, "192.168.50.176", "", "pw")));

    // PrusaConnect doesn't allow HTTP digest, and other host types have no choice.
    CHECK_FALSE(prusalink_defaults_to_digest(printer_connection(htPrusaConnect, atKeyPassword, "", "", "")));
    CHECK_FALSE(prusalink_defaults_to_digest(printer_connection(htOctoPrint, atKeyPassword, "", "", "")));
}
