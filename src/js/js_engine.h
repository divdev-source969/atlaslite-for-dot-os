// AtlasLight scripting: runs user scripts (~/.config/atlaslight/scripts/*.js)
// inside QuickJS to customise the BROWSER (block rules, extra CSS, logging).
//
// IMPORTANT: this is NOT a web-page JavaScript engine. litehtml has no DOM
// bindings, so <script> tags inside web pages are not executed. Scripts here
// are browser extensions, not page scripts.
//
// Script API:
//   atlas.block("ads.example.com")   add an ad-block rule
//   atlas.css("body{font-size:16px}") append CSS applied to every page
//   atlas.log("text")                print to stderr
//
// Built only if QuickJS is found at configure time (ATLAS_HAVE_QUICKJS).
#ifndef ATLAS_JS_ENGINE_H
#define ATLAS_JS_ENGINE_H

#include <functional>
#include <string>

namespace atlas
{
    struct script_host
    {
        std::function<void(const std::string&)> add_block_rule;
        std::function<void(const std::string&)> add_css;
    };

    // Returns true if scripting support was compiled in.
    bool js_available();

    // Runs every *.js file in `dir`. Returns number of scripts that ran without error.
    int run_user_scripts(const std::string& dir, const script_host& host);
} // namespace atlas

#endif
