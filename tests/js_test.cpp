// Tests the QuickJS scripting layer against the real engine.
#include "../src/js/js_engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if(!(cond))                                                \
        {                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                            \
        }                                                          \
    } while(0)

static void write(const std::string& path, const std::string& text)
{
    std::ofstream(path) << text;
}

int main()
{
    CHECK(atlas::js_available());

    char tmpl[] = "/tmp/atlas_js_test_XXXXXX";
    char* dir   = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    if(!dir)
    {
        return 1;
    }
    std::string d = dir;

    write(d + "/a_good.js", "atlas.log('hi', 42); atlas.block('ads.test'); atlas.block('@@ok.test'); atlas.css('p{color:red}');");
    write(d + "/b_syntax.js", "this is not javascript (((");
    write(d + "/c_throws.js", "throw new Error('boom');");
    write(d + "/d_loop.js", "while(true){}");
    write(d + "/e_memory.js", "var a=[]; for(;;){ a.push(new Array(100000).fill(1)); }");
    write(d + "/f_good2.js", "atlas.css('h1{color:blue}'); atlas.block(); atlas.css();"); // missing args must not crash
    write(d + "/ignored.txt", "atlas.block('never.test')");

    std::vector<std::string> rules;
    std::string              css;
    atlas::script_host       host;
    host.add_block_rule = [&](const std::string& r) { rules.push_back(r); };
    host.add_css        = [&](const std::string& c) { css += c; };

    auto t0 = std::chrono::steady_clock::now();
    int  ok = atlas::run_user_scripts(d, host);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    CHECK(ok == 2); // a_good and f_good2; syntax error, throw, infinite loop and out-of-memory all fail safely
    CHECK(rules.size() == 2);
    CHECK(rules.size() >= 1 && rules[0] == "ads.test");
    CHECK(rules.size() >= 2 && rules[1] == "@@ok.test");
    CHECK(css == "p{color:red}h1{color:blue}");
    CHECK(ms < 8000); // the infinite loop was cut off by the timeout
    std::printf("scripts ok=%d, rules=%zu, took %lld ms\n", ok, rules.size(), (long long) ms);

    // Missing directory is fine.
    CHECK(atlas::run_user_scripts("/nonexistent/atlas/dir", host) == 0);

    std::string cmd = "rm -rf " + d;
    if(std::system(cmd.c_str()) != 0) { /* best-effort cleanup */ }
    std::printf(failures ? "FAILED (%d)\n" : "js tests passed\n", failures);
    return failures ? 1 : 0;
}
