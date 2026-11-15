#include "js_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef ATLAS_HAVE_QUICKJS
#if __has_include(<quickjs/quickjs.h>)
#include <quickjs/quickjs.h>
#else
#include <quickjs.h>
#endif

namespace
{
    const atlas::script_host* g_host = nullptr;

    // A script that loops forever must not freeze the browser: stop it after a short time.
    constexpr int kScriptTimeoutMs = 2000;
    std::chrono::steady_clock::time_point g_deadline;

    int interrupt_handler(JSContext*, void*)
    {
        return std::chrono::steady_clock::now() > g_deadline ? 1 : 0;
    }

    std::string arg_string(JSContext* ctx, JSValueConst v)
    {
        const char* s = JS_ToCString(ctx, v);
        std::string out = s ? s : "";
        if(s)
        {
            JS_FreeCString(ctx, s);
        }
        return out;
    }

    JSValue js_block(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        if(argc > 0 && g_host && g_host->add_block_rule)
        {
            g_host->add_block_rule(arg_string(ctx, argv[0]));
        }
        return JS_UNDEFINED;
    }

    JSValue js_css(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        if(argc > 0 && g_host && g_host->add_css)
        {
            g_host->add_css(arg_string(ctx, argv[0]));
        }
        return JS_UNDEFINED;
    }

    JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        for(int i = 0; i < argc; i++)
        {
            std::fprintf(stderr, "%s%s", i ? " " : "[script] ", arg_string(ctx, argv[i]).c_str());
        }
        std::fprintf(stderr, "\n");
        return JS_UNDEFINED;
    }

    bool run_one(JSContext* ctx, const std::string& path)
    {
        std::ifstream in(path);
        if(!in)
        {
            return false;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string code = ss.str();

        g_deadline  = std::chrono::steady_clock::now() + std::chrono::milliseconds(kScriptTimeoutMs);
        JSValue res = JS_Eval(ctx, code.c_str(), code.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
        bool    ok  = !JS_IsException(res);
        if(!ok)
        {
            JSValue exc = JS_GetException(ctx);
            std::fprintf(stderr, "[script] error in %s: %s\n", path.c_str(), arg_string(ctx, exc).c_str());
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, res);
        return ok;
    }
} // namespace

bool atlas::js_available()
{
    return true;
}

int atlas::run_user_scripts(const std::string& dir, const script_host& host)
{
    std::vector<std::string> files;
    if(DIR* d = opendir(dir.c_str()))
    {
        while(dirent* e = readdir(d))
        {
            std::string name = e->d_name;
            if(name.size() > 3 && name.compare(name.size() - 3, 3, ".js") == 0)
            {
                files.push_back(dir + "/" + name);
            }
        }
        closedir(d);
    }
    if(files.empty())
    {
        return 0;
    }
    std::sort(files.begin(), files.end());

    JSRuntime* rt = JS_NewRuntime();
    if(!rt)
    {
        return 0;
    }
    JS_SetMemoryLimit(rt, 16 * 1024 * 1024); // keep scripts small on a 2 GB machine
    JS_SetInterruptHandler(rt, interrupt_handler, nullptr);
    JSContext* ctx = JS_NewContext(rt);
    if(!ctx)
    {
        JS_FreeRuntime(rt);
        return 0;
    }

    g_host = &host;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue atlas  = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, atlas, "block", JS_NewCFunction(ctx, js_block, "block", 1));
    JS_SetPropertyStr(ctx, atlas, "css", JS_NewCFunction(ctx, js_css, "css", 1));
    JS_SetPropertyStr(ctx, atlas, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "atlas", atlas); // takes ownership of `atlas`
    JS_FreeValue(ctx, global);

    int ok = 0;
    for(const auto& f : files)
    {
        if(run_one(ctx, f))
        {
            ok++;
        }
    }

    g_host = nullptr;
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return ok;
}

#else // ---- QuickJS not available: scripting disabled ----

bool atlas::js_available()
{
    return false;
}

int atlas::run_user_scripts(const std::string&, const script_host&)
{
    return 0;
}

#endif
