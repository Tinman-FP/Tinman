// Tinman: stealth mode makes no cloud requests. Every CloudPluginService operation that would reach
// the Orca Cloud plugin hub asks one check first (CloudPluginService::stealth_mode_blocks), which the
// app installs at startup. These tests install it directly. No cloud agent is set, so an operation
// that got past the check would fail on the missing agent with a different error.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <slic3r/plugin/CloudPluginService.hpp>
#include <slic3r/plugin/PluginDescriptor.hpp>

#include <string>
#include <unordered_map>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::ContainsSubstring;

namespace {

struct StealthCheckScope
{
    explicit StealthCheckScope(bool on) { CloudPluginService::set_stealth_mode_check([on]() { return on; }); }
    ~StealthCheckScope() { CloudPluginService::set_stealth_mode_check({}); }
};

} // namespace

TEST_CASE("Stealth mode stops every cloud plugin request before it reaches the cloud", "[Plugin][Stealth][TinMan]")
{
    StealthCheckScope stealth(true);
    CloudPluginService service;
    PluginDescriptor   plugin;
    std::string        error;

    CHECK(CloudPluginService::stealth_mode_blocks());
    CHECK_FALSE(service.can_fetch_cloud_plugins());

    std::vector<PluginDescriptor> descriptors;
    std::vector<std::string>      not_found, unauthorized;
    CHECK_FALSE(service.fetch_manifests_into_descriptors(descriptors, not_found, unauthorized));

    CHECK_FALSE(service.request_cloud_subscribe("00000000-0000-4000-8000-000000000001", error));
    CHECK_THAT(error, ContainsSubstring("Stealth mode"));

    error.clear();
    CHECK_FALSE(service.request_cloud_unsubscribe(plugin, error));
    CHECK_THAT(error, ContainsSubstring("Stealth mode"));

    error.clear();
    CloudPluginDownload download;
    CHECK_FALSE(service.download_cloud_plugin(plugin, "1.0.0", download, error));
    CHECK_THAT(error, ContainsSubstring("Stealth mode"));

    error.clear();
    std::vector<PluginChangelog> changelog;
    CHECK_FALSE(service.fetch_plugin_changelog(plugin, changelog, error));
    CHECK_THAT(error, ContainsSubstring("Stealth mode"));

    error.clear();
    std::unordered_map<std::string, std::vector<PluginChangelog>> changelogs;
    CHECK_FALSE(service.fetch_plugin_changelog(std::vector<PluginDescriptor>{plugin}, changelogs, error));
    CHECK_THAT(error, ContainsSubstring("Stealth mode"));
}

TEST_CASE("Without stealth mode the cloud plugin requests go on to the cloud agent", "[Plugin][Stealth][TinMan]")
{
    SECTION("stealth mode off") { StealthCheckScope stealth(false); }
    SECTION("no check installed (tests, CLI)") { CloudPluginService::set_stealth_mode_check({}); }
    CHECK_FALSE(CloudPluginService::stealth_mode_blocks());

    CloudPluginService service;
    std::string        error;
    CHECK_FALSE(service.request_cloud_subscribe("00000000-0000-4000-8000-000000000001", error));
    CHECK_THAT(error, !ContainsSubstring("Stealth mode"));
}
