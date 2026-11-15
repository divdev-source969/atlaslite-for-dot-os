// AtlasLight - a tiny GTK 3 browser for old netbooks (built for the Packard Bell Dot).
//
// Rendering: litehtml (HTML + CSS).  Networking: libcurl.  Window: GTK 3 + Cairo (CPU only).
// This file is the only place that touches GTK; browser logic lives in src/net, adblock.*, js/*.
//
// Threading rule: page objects (current_/next_) are touched ONLY on the GTK thread.
// Worker threads (downloads) talk to us through the atomics below and post().
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <curl/curl.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include "adblock.h"
#include "js/js_engine.h"
#include "net/web_history.h"
#include "net/web_page.h"

namespace
{
    constexpr const char* kVersion = "0.1";

    // Run fn on the GTK main thread (safe to call from any thread).
    void post(std::function<void()> fn)
    {
        auto* p = new std::function<void()>(std::move(fn));
        g_idle_add(
            [](gpointer data) -> gboolean {
                auto* f = static_cast<std::function<void()>*>(data);
                (*f)();
                delete f;
                return G_SOURCE_REMOVE;
            },
            p);
    }

    std::string trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if(b == std::string::npos)
        {
            return "";
        }
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::string url_encode(const std::string& s)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string        out;
        for(unsigned char c : s)
        {
            if(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                out += (char) c;
            } else if(c == ' ')
            {
                out += '+';
            } else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    // Address-bar text -> URL (search if it does not look like an address).
    std::string normalize_input(const std::string& raw)
    {
        std::string s = trim(raw);
        if(s.empty())
        {
            return "";
        }
        if(s.find("://") != std::string::npos || s[0] == '/' || s.rfind("file:", 0) == 0)
        {
            return s;
        }
        bool has_space = s.find(' ') != std::string::npos;
        if(!has_space && (s.find('.') != std::string::npos || s.rfind("localhost", 0) == 0))
        {
            return "https://" + s;
        }
        return "https://duckduckgo.com/html/?q=" + url_encode(s);
    }

    struct palette
    {
        const char *bg, *bar, *field, *fg, *sub, *hover, *accent;
    };
    const palette kDark  = {"#1e1e2e", "#181825", "#313244", "#cdd6f4", "#a6adc8", "#45475a", "#89b4fa"};
    const palette kLight = {"#eff1f5", "#e6e9ef", "#ccd0da", "#4c4f69", "#6c6f85", "#bcc0cc", "#1e66f5"};

    std::string theme_css(bool dark)
    {
        const palette& p = dark ? kDark : kLight;
        std::string    c;
        c += std::string("@define-color bg ") + p.bg + ";\n";
        c += std::string("@define-color bar ") + p.bar + ";\n";
        c += std::string("@define-color field ") + p.field + ";\n";
        c += std::string("@define-color fg ") + p.fg + ";\n";
        c += std::string("@define-color sub ") + p.sub + ";\n";
        c += std::string("@define-color hover ") + p.hover + ";\n";
        c += std::string("@define-color accent ") + p.accent + ";\n";
        c += R"CSS(
/* ---- rounded window (needs a compositing window manager, e.g. Sway) ---- */
window.csd, window.csd decoration { border-radius: 14px; }
window.maximized, window.maximized decoration, window.fullscreen, window.fullscreen decoration { border-radius: 0; }
window { background-color: @bg; color: @fg; }

headerbar, .titlebar {
  background-image: none; background-color: @bar; color: @fg;
  border: none; box-shadow: none; border-radius: 14px 14px 0 0; padding: 4px 6px; min-height: 0;
}
headerbar button, .titlebar button {
  background-image: none; background-color: @field; color: @fg;
  border: none; border-radius: 10px; box-shadow: none; text-shadow: none; padding: 3px 10px; min-height: 0; min-width: 0;
}
headerbar button:hover { background-color: @hover; }
headerbar button:active, headerbar button:checked { background-color: @accent; color: @bg; }
headerbar entry {
  background-image: none; background-color: @field; color: @fg; caret-color: @accent;
  border: 1px solid transparent; border-radius: 14px; box-shadow: none; padding: 3px 14px; min-height: 0;
}
headerbar entry:focus { border-color: @accent; }
headerbar .title, headerbar label { color: @fg; }

scrollbar { background-color: @bg; border: none; }
scrollbar slider { background-color: @hover; border-radius: 6px; min-width: 8px; min-height: 30px; border: none; }
scrollbar slider:hover { background-color: @accent; }

.statusbar {
  background-color: @bar; color: @sub; font-size: 11px;
  padding: 3px 16px 7px 16px; border-radius: 0 0 14px 14px;
}
window.maximized .statusbar { border-radius: 0; }
)CSS";
        return c;
    }

    std::string config_dir()
    {
        return std::string(g_get_user_config_dir()) + "/atlaslight";
    }

    const char* kStartPage = R"HTML(<html><head><title>AtlasLight</title></head>
<body style="font-family:sans-serif;max-width:560px;margin:60px auto;padding:0 20px">
<h1>AtlasLight</h1>
<p>A tiny, fast browser for small machines.</p>
<p><a href="https://duckduckgo.com/html/">Search with DuckDuckGo</a><br>
<a href="https://en.wikipedia.org/wiki/Special:Random">Random Wikipedia article</a><br>
<a href="https://lite.cnn.com/">CNN Lite</a><br>
<a href="https://text.npr.org/">NPR text</a></p>
<h3>Shortcuts</h3>
<p>Ctrl+L address bar &middot; F5 reload &middot; Alt+Left / Alt+Right back / forward<br>
Ctrl+D dark / light &middot; Ctrl+Q quit</p>
<p style="font-size:12px">AtlasLight has no JavaScript engine for web pages (scripts on pages do not run),
so sites that need JavaScript will look incomplete. Type a search or an address above.</p>
</body></html>)HTML";
} // namespace

class App;

class Notifier : public litebrowser::browser_notify_interface
{
    App* m_app;

  public:
    explicit Notifier(App* app) :
        m_app(app)
    {
    }
    void redraw() override;
    void render() override;
    void update_state() override;
    void on_set_caption(const std::string& caption_text) override;
    void on_page_loaded(uint64_t web_page_id) override;
};

class App : public litebrowser::html_host_interface
{
    friend class Notifier;

  public:
    void run(int argc, char** argv);

    // ---- html_host_interface (may be called from worker threads) ----
    void open_url(const std::string& url) override
    {
        post([this, url] { navigate_link(url); });
    }
    void update_cursor() override
    {
        post([this] { apply_cursor(); });
    }
    void scroll_to(int /*x*/, int y) override
    {
        post([this, y] { set_scroll(y); });
    }
    void get_viewport(litehtml::position& viewport) const override
    {
        viewport.x      = (litehtml::pixel_t) 0;
        viewport.y      = (litehtml::pixel_t) scroll_y_.load();
        viewport.width  = (litehtml::pixel_t) view_w_.load();
        viewport.height = (litehtml::pixel_t) view_h_.load();
    }
    void redraw_boxes(const litehtml::position::vector&) override
    {
        post([this] { gtk_widget_queue_draw(area_); });
    }
    int get_render_width() override
    {
        return view_w_.load();
    }
    double get_dpi() override
    {
        return 96.0;
    }
    int get_screen_width() override
    {
        return screen_w_;
    }
    int get_screen_height() override
    {
        return screen_h_;
    }
    cairo_surface_t* load_image(const std::string& path) override;

  private:
    // ---- state ----
    GtkWidget*     window_  = nullptr;
    GtkWidget*     entry_   = nullptr;
    GtkWidget*     area_    = nullptr;
    GtkWidget*     status_  = nullptr;
    GtkAdjustment* vadj_    = nullptr;
    GtkCssProvider* css_    = nullptr;
    GtkWidget*     dark_btn_ = nullptr;

    std::shared_ptr<Notifier>              notifier_;
    std::shared_ptr<litebrowser::web_page> current_;
    std::shared_ptr<litebrowser::web_page> next_;
    web_history                            history_;
    atlas::adblock                         blocker_;

    std::atomic<int> view_w_{800};
    std::atomic<int> view_h_{500};
    std::atomic<int> scroll_y_{0};
    int              screen_w_ = 1024;
    int              screen_h_ = 600;

    bool        dark_ = true;
    std::string extra_css_; // from user scripts
    std::string page_css() const;

    // ---- actions ----
    void build_ui();
    void load_settings();
    void save_settings() const;
    void load_blocklists();
    void apply_theme();
    void navigate_input(const std::string& raw);
    void navigate_link(const std::string& url);
    void open_page(const std::string& url, const std::string& fragment);
    void reload();
    void go_back();
    void go_forward();
    void toggle_dark();
    void retire(std::shared_ptr<litebrowser::web_page>& page);
    void on_page_loaded(uint64_t id);
    void on_render();
    void update_state();
    void update_scrollbar();
    void set_scroll(int y);
    void apply_cursor();
    void on_size(int w, int h);

    // ---- GTK callbacks ----
    static gboolean cb_draw(GtkWidget* w, cairo_t* cr, gpointer self);
    static gboolean cb_button(GtkWidget* w, GdkEventButton* ev, gpointer self);
    static gboolean cb_motion(GtkWidget* w, GdkEventMotion* ev, gpointer self);
    static gboolean cb_scroll(GtkWidget* w, GdkEventScroll* ev, gpointer self);
    static gboolean cb_key(GtkWidget* w, GdkEventKey* ev, gpointer self);
    static void     cb_size(GtkWidget* w, GdkRectangle* a, gpointer self);
    static void     cb_vadj(GtkAdjustment* a, gpointer self);
};

// ===================== Notifier =====================
void Notifier::redraw()
{
    post([this] { gtk_widget_queue_draw(m_app->area_); });
}
void Notifier::render()
{
    post([this] { m_app->on_render(); });
}
void Notifier::update_state()
{
    post([this] { m_app->update_state(); });
}
void Notifier::on_set_caption(const std::string& caption_text)
{
    post([this, caption_text] {
        std::string t = caption_text.empty() ? "AtlasLight" : caption_text + " - AtlasLight";
        gtk_window_set_title(GTK_WINDOW(m_app->window_), t.c_str());
    });
}
void Notifier::on_page_loaded(uint64_t id)
{
    post([this, id] { m_app->on_page_loaded(id); });
}

// ===================== Image loading (worker thread) =====================
cairo_surface_t* App::load_image(const std::string& path)
{
    GError*    err = nullptr;
    GdkPixbuf* pb  = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if(!pb)
    {
        g_clear_error(&err);
        return nullptr;
    }
    int w = gdk_pixbuf_get_width(pb);
    int h = gdk_pixbuf_get_height(pb);
    // Protect a 2 GB machine from huge images (> ~16 megapixels).
    if(w <= 0 || h <= 0 || (long long) w * h > 16LL * 1000 * 1000)
    {
        g_object_unref(pb);
        return nullptr;
    }
    cairo_surface_t* s =
        cairo_image_surface_create(gdk_pixbuf_get_has_alpha(pb) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    g_object_unref(pb);
    return s;
}

// ===================== settings, blocklists, theme =====================
std::string App::page_css() const
{
    std::string css;
    if(dark_)
    {
        css +=
            "html,body{background-color:#1e1e2e !important;color:#cdd6f4 !important}"
            "a{color:#89b4fa !important}a:visited{color:#b4befe !important}"
            "h1,h2,h3,h4,h5,h6{color:#f5e0dc !important}";
    }
    css += extra_css_;
    return css;
}

void App::load_settings()
{
    std::ifstream in(config_dir() + "/settings.conf");
    std::string   line;
    while(std::getline(in, line))
    {
        if(line == "dark=0")
        {
            dark_ = false;
        } else if(line == "dark=1")
        {
            dark_ = true;
        }
    }
}

void App::save_settings() const
{
    g_mkdir_with_parents(config_dir().c_str(), 0755);
    std::ofstream out(config_dir() + "/settings.conf");
    out << "dark=" << (dark_ ? 1 : 0) << "\n";
}

void App::load_blocklists()
{
    size_t n = 0;
    n += blocker_.load_file(config_dir() + "/blocklist.txt");
    n += blocker_.load_file("/usr/share/atlaslight/blocklist.txt");
    n += blocker_.load_file("data/blocklist.txt"); // running from the source folder
    g_message("AtlasLight: %zu ad-block rules loaded", n);
}

void App::apply_theme()
{
    GtkSettings* st = gtk_settings_get_default();
    g_object_set(st, "gtk-application-prefer-dark-theme", dark_ ? TRUE : FALSE, nullptr);
    std::string css = theme_css(dark_);
    gtk_css_provider_load_from_data(css_, css.c_str(), -1, nullptr);
    if(dark_btn_)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dark_btn_), dark_ ? TRUE : FALSE);
    }
    if(area_)
    {
        gtk_widget_queue_draw(area_);
    }
}

// ===================== navigation =====================
void App::open_page(const std::string& url, const std::string& fragment)
{
    if(next_)
    {
        retire(next_);
    }
    next_ = std::make_shared<litebrowser::web_page>(this, notifier_, 4);
    next_->set_user_css(page_css());
    next_->set_content_filter([this](const std::string& u) { return blocker_.blocked(u); });
    next_->open(url, fragment);
    gtk_entry_set_text(GTK_ENTRY(entry_), url.c_str());
    update_state();
}

void App::navigate_link(const std::string& url)
{
    // Same logic as litehtml's own widget: "#fragment" on the same page just scrolls.
    std::string s_url    = url;
    std::string fragment;
    size_t      hash = s_url.find('#');
    if(hash != std::string::npos)
    {
        fragment = s_url.substr(hash + 1);
        s_url.erase(hash);
    }
    std::string cur = history_.current();
    size_t      ch  = cur.find('#');
    if(ch != std::string::npos)
    {
        cur.erase(ch);
    }
    const bool is_reload = (history_.current() == url);
    if(!cur.empty() && cur == s_url && !is_reload)
    {
        if(current_)
        {
            current_->show_fragment(fragment);
        }
    } else
    {
        open_page(s_url, fragment);
    }
    if(!is_reload)
    {
        history_.url_opened(url);
    }
    update_state();
}

void App::navigate_input(const std::string& raw)
{
    std::string url = normalize_input(raw);
    if(!url.empty())
    {
        navigate_link(url);
    }
}

void App::reload()
{
    std::string url = history_.current();
    if(!url.empty())
    {
        std::string fragment;
        size_t      hash = url.find('#');
        if(hash != std::string::npos)
        {
            fragment = url.substr(hash + 1);
            url.erase(hash);
        }
        open_page(url, fragment);
    }
}

void App::go_back()
{
    std::string url;
    if(history_.back(url))
    {
        navigate_link(url); // url_opened() sees it is the previous item and steps back
    }
}

void App::go_forward()
{
    std::string url;
    if(history_.forward(url))
    {
        navigate_link(url);
    }
}

void App::toggle_dark()
{
    dark_ = !dark_;
    save_settings();
    apply_theme();
    reload(); // page styles are applied when a document is created
}

// Old pages are stopped and destroyed on a helper thread. Destroying a page joins its download
// threads, so it must never happen on the GTK thread (stalls) or on one of its own workers (deadlock).
void App::retire(std::shared_ptr<litebrowser::web_page>& page)
{
    if(!page)
    {
        return;
    }
    std::thread([p = std::move(page)]() mutable {
        p->stop_loading();
        p.reset();
    }).detach();
    page.reset();
}

void App::on_page_loaded(uint64_t id)
{
    if(!next_ || next_->id() != id)
    {
        return; // a newer navigation replaced this one
    }
    std::shared_ptr<litebrowser::web_page> old = std::move(current_);
    current_                                   = std::move(next_);
    next_.reset();
    retire(old);

    gtk_entry_set_text(GTK_ENTRY(entry_), current_->url().c_str());
    set_scroll(0);
    update_scrollbar();
    gtk_widget_queue_draw(area_);
    update_state();
}

void App::on_render()
{
    if(current_)
    {
        current_->render(view_w_.load());
        update_scrollbar();
    }
    gtk_widget_queue_draw(area_);
}

void App::update_state()
{
    char buf[160];
    bool loading = (next_ && next_->is_downloading()) || (current_ && current_->is_downloading());
    std::snprintf(buf, sizeof buf, "%s  |  blocked %zu requests  |  %zu rules  |  dark %s  |  JS scripts %s",
                  loading ? "Loading..." : "Ready", blocker_.blocked_count(), blocker_.rule_count(),
                  dark_ ? "on" : "off", atlas::js_available() ? "on" : "off");
    gtk_label_set_text(GTK_LABEL(status_), buf);
}

void App::update_scrollbar()
{
    int page_h = current_ ? current_->height() : 0;
    int view_h = view_h_.load();
    int upper  = page_h > view_h ? page_h : view_h;
    gtk_adjustment_configure(vadj_, gtk_adjustment_get_value(vadj_), 0, upper, 48, view_h * 0.9, view_h);
}

void App::set_scroll(int y)
{
    gtk_adjustment_set_value(vadj_, y);
}

void App::apply_cursor()
{
    GdkWindow* gw = gtk_widget_get_window(area_);
    if(!gw)
    {
        return;
    }
    if(current_ && current_->get_cursor() == "pointer")
    {
        GdkCursor* c = gdk_cursor_new_from_name(gdk_window_get_display(gw), "pointer");
        gdk_window_set_cursor(gw, c);
        if(c)
        {
            g_object_unref(c);
        }
    } else
    {
        gdk_window_set_cursor(gw, nullptr);
    }
}

void App::on_size(int w, int h)
{
    if(w == view_w_.load() && h == view_h_.load())
    {
        return;
    }
    view_w_ = w;
    view_h_ = h;
    if(current_)
    {
        current_->media_changed();
        current_->render(w);
    }
    update_scrollbar();
    gtk_widget_queue_draw(area_);
}

// ===================== GTK callbacks =====================
gboolean App::cb_draw(GtkWidget* w, cairo_t* cr, gpointer self)
{
    auto* app = static_cast<App*>(self);
    int   aw  = gtk_widget_get_allocated_width(w);
    int   ah  = gtk_widget_get_allocated_height(w);

    const palette& p = app->dark_ ? kDark : kLight;
    unsigned       r = 0, g = 0, b = 0;
    std::sscanf(p.bg, "#%2x%2x%2x", &r, &g, &b);
    cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
    cairo_paint(cr);

    if(app->current_)
    {
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, aw, ah);
        cairo_clip(cr);
        // clip is in viewport coordinates
        litehtml::position clip((litehtml::pixel_t) 0, (litehtml::pixel_t) 0, (litehtml::pixel_t) aw,
                                (litehtml::pixel_t) ah);
        app->current_->draw((litehtml::uint_ptr) cr, 0, -app->scroll_y_.load(), &clip);
        cairo_restore(cr);
    }
    return FALSE;
}

gboolean App::cb_button(GtkWidget* w, GdkEventButton* ev, gpointer self)
{
    auto* app = static_cast<App*>(self);
    if(ev->button != 1 || !app->current_)
    {
        return FALSE;
    }
    gtk_widget_grab_focus(w);
    int  x = (int) ev->x, y = (int) ev->y, sy = app->scroll_y_.load();
    bool dirty = false;
    auto mark  = [&dirty](const litehtml::position&) { dirty = true; };
    if(ev->type == GDK_BUTTON_PRESS)
    {
        app->current_->on_lbutton_down(x, y + sy, x, y, mark);
    } else if(ev->type == GDK_BUTTON_RELEASE)
    {
        app->current_->on_lbutton_up(x, y + sy, x, y, mark);
    }
    if(dirty)
    {
        gtk_widget_queue_draw(w);
    }
    return TRUE;
}

gboolean App::cb_motion(GtkWidget* w, GdkEventMotion* ev, gpointer self)
{
    auto* app = static_cast<App*>(self);
    if(!app->current_)
    {
        return FALSE;
    }
    int  x = (int) ev->x, y = (int) ev->y, sy = app->scroll_y_.load();
    bool dirty = false;
    app->current_->on_mouse_over(x, y + sy, x, y, [&dirty](const litehtml::position&) { dirty = true; });
    if(dirty)
    {
        gtk_widget_queue_draw(w);
    }
    return FALSE;
}

gboolean App::cb_scroll(GtkWidget*, GdkEventScroll* ev, gpointer self)
{
    auto*  app = static_cast<App*>(self);
    double dy  = 0;
    if(ev->direction == GDK_SCROLL_UP)
    {
        dy = -1;
    } else if(ev->direction == GDK_SCROLL_DOWN)
    {
        dy = 1;
    } else if(ev->direction == GDK_SCROLL_SMOOTH)
    {
        dy = ev->delta_y;
    } else
    {
        return FALSE;
    }
    GtkAdjustment* a = app->vadj_;
    double         v = gtk_adjustment_get_value(a) + dy * 60.0;
    double         max = gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a);
    if(v > max)
    {
        v = max;
    }
    if(v < 0)
    {
        v = 0;
    }
    gtk_adjustment_set_value(a, v);
    return TRUE;
}

gboolean App::cb_key(GtkWidget*, GdkEventKey* ev, gpointer self)
{
    auto* app  = static_cast<App*>(self);
    bool  ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
    bool  alt  = (ev->state & GDK_MOD1_MASK) != 0;

    if(ctrl && (ev->keyval == GDK_KEY_l || ev->keyval == GDK_KEY_L))
    {
        gtk_widget_grab_focus(app->entry_);
        gtk_editable_select_region(GTK_EDITABLE(app->entry_), 0, -1);
        return TRUE;
    }
    if(ev->keyval == GDK_KEY_F5 || (ctrl && (ev->keyval == GDK_KEY_r || ev->keyval == GDK_KEY_R)))
    {
        app->reload();
        return TRUE;
    }
    if(alt && ev->keyval == GDK_KEY_Left)
    {
        app->go_back();
        return TRUE;
    }
    if(alt && ev->keyval == GDK_KEY_Right)
    {
        app->go_forward();
        return TRUE;
    }
    if(ctrl && (ev->keyval == GDK_KEY_d || ev->keyval == GDK_KEY_D))
    {
        app->toggle_dark();
        return TRUE;
    }
    if(ctrl && (ev->keyval == GDK_KEY_q || ev->keyval == GDK_KEY_Q))
    {
        gtk_main_quit();
        return TRUE;
    }

    // Scrolling keys only when the page (not the address bar) has focus.
    if(gtk_window_get_focus(GTK_WINDOW(app->window_)) == app->area_)
    {
        GtkAdjustment* a    = app->vadj_;
        double         v    = gtk_adjustment_get_value(a);
        double         step = gtk_adjustment_get_step_increment(a);
        double         page = gtk_adjustment_get_page_increment(a);
        double         max  = gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a);
        switch(ev->keyval)
        {
        case GDK_KEY_Down:
            v += step;
            break;
        case GDK_KEY_Up:
            v -= step;
            break;
        case GDK_KEY_Page_Down:
        case GDK_KEY_space:
            v += page;
            break;
        case GDK_KEY_Page_Up:
            v -= page;
            break;
        case GDK_KEY_Home:
            v = 0;
            break;
        case GDK_KEY_End:
            v = max;
            break;
        default:
            return FALSE;
        }
        if(v > max)
        {
            v = max;
        }
        if(v < 0)
        {
            v = 0;
        }
        gtk_adjustment_set_value(a, v);
        return TRUE;
    }
    return FALSE;
}

void App::cb_size(GtkWidget*, GdkRectangle* a, gpointer self)
{
    static_cast<App*>(self)->on_size(a->width, a->height);
}

void App::cb_vadj(GtkAdjustment* a, gpointer self)
{
    auto* app = static_cast<App*>(self);
    app->scroll_y_ = (int) gtk_adjustment_get_value(a);
    gtk_widget_queue_draw(app->area_);
}

// ===================== UI construction =====================
void App::build_ui()
{
    GdkScreen* screen = gdk_screen_get_default();

    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "AtlasLight");
    // Transparent corners need an RGBA visual and a compositor; harmless without one.
    GdkVisual* vis = gdk_screen_get_rgba_visual(screen);
    if(vis && gdk_screen_is_composited(screen))
    {
        gtk_widget_set_visual(window_, vis);
    }
    int dw = screen_w_ < 1000 ? screen_w_ - 20 : 1000;
    int dh = screen_h_ < 640 ? screen_h_ - 60 : 620;
    gtk_window_set_default_size(GTK_WINDOW(window_), dw, dh);

    // Header bar = client-side decorations = rounded window.
    GtkWidget* header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);

    GtkWidget* back = gtk_button_new_with_label("\xE2\x97\x80");    // left triangle
    GtkWidget* fwd  = gtk_button_new_with_label("\xE2\x96\xB6");    // right triangle
    GtkWidget* rel  = gtk_button_new_with_label("\xE2\x9F\xB3");    // refresh arrow
    dark_btn_       = gtk_toggle_button_new_with_label("\xE2\x98\xBE"); // crescent moon
    gtk_widget_set_tooltip_text(back, "Back (Alt+Left)");
    gtk_widget_set_tooltip_text(fwd, "Forward (Alt+Right)");
    gtk_widget_set_tooltip_text(rel, "Reload (F5)");
    gtk_widget_set_tooltip_text(dark_btn_, "Dark / light (Ctrl+D)");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), back);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), fwd);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), rel);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), dark_btn_);

    entry_ = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry_), 42);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_), "Search or type an address");
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), entry_);
    gtk_window_set_titlebar(GTK_WINDOW(window_), header);

    // Page area + scrollbar + status bar.
    area_ = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area_, TRUE);
    gtk_widget_set_vexpand(area_, TRUE);
    gtk_widget_set_can_focus(area_, TRUE);
    gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                     GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    vadj_              = gtk_adjustment_new(0, 0, 1, 48, 400, 1);
    GtkWidget* vscroll = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, vadj_);

    status_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(status_), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(status_), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_), "statusbar");

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox), area_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vscroll, FALSE, FALSE, 0);
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), status_, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window_), vbox);

    // Theme.
    css_ = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    apply_theme();

    // Signals.
    g_signal_connect(area_, "draw", G_CALLBACK(cb_draw), this);
    g_signal_connect(area_, "button-press-event", G_CALLBACK(cb_button), this);
    g_signal_connect(area_, "button-release-event", G_CALLBACK(cb_button), this);
    g_signal_connect(area_, "motion-notify-event", G_CALLBACK(cb_motion), this);
    g_signal_connect(area_, "scroll-event", G_CALLBACK(cb_scroll), this);
    g_signal_connect(area_, "size-allocate", G_CALLBACK(cb_size), this);
    g_signal_connect(vadj_, "value-changed", G_CALLBACK(cb_vadj), this);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(cb_key), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    g_signal_connect_swapped(back, "clicked", G_CALLBACK(+[](App* a) { a->go_back(); }), this);
    g_signal_connect_swapped(fwd, "clicked", G_CALLBACK(+[](App* a) { a->go_forward(); }), this);
    g_signal_connect_swapped(rel, "clicked", G_CALLBACK(+[](App* a) { a->reload(); }), this);
    g_signal_connect(dark_btn_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer s) {
                         auto* a = static_cast<App*>(s);
                         // The toggle button changes state itself; only act if it disagrees with dark_.
                         bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->dark_btn_));
                         if(active != a->dark_)
                         {
                             a->toggle_dark();
                         }
                     }),
                     this);
    g_signal_connect(entry_, "activate", G_CALLBACK(+[](GtkEntry* e, gpointer s) {
                         auto* a = static_cast<App*>(s);
                         a->navigate_input(gtk_entry_get_text(e));
                         gtk_widget_grab_focus(a->area_);
                     }),
                     this);
}

void App::run(int argc, char** argv)
{
    GdkDisplay* disp = gdk_display_get_default();
    GdkMonitor* mon  = gdk_display_get_primary_monitor(disp);
    if(!mon)
    {
        mon = gdk_display_get_monitor(disp, 0);
    }
    if(mon)
    {
        GdkRectangle r;
        gdk_monitor_get_geometry(mon, &r);
        screen_w_ = r.width;
        screen_h_ = r.height;
    }

    notifier_ = std::make_shared<Notifier>(this);
    load_settings();
    load_blocklists();

    // User scripts (optional): may add block rules and extra CSS.
    atlas::script_host host;
    host.add_block_rule = [this](const std::string& r) { blocker_.add_rule(r); };
    host.add_css        = [this](const std::string& c) { extra_css_ += c; };
    int scripts         = atlas::run_user_scripts(config_dir() + "/scripts", host);
    if(scripts > 0)
    {
        g_message("AtlasLight: ran %d user script(s)", scripts);
    }

    build_ui();
    gtk_widget_show_all(window_);

    // First page: command-line URL, or the start page.
    std::string start;
    if(argc > 1)
    {
        start = normalize_input(argv[1]);
    }
    if(start.empty())
    {
        g_mkdir_with_parents(config_dir().c_str(), 0755);
        std::string sp = config_dir() + "/start.html";
        std::ofstream(sp) << kStartPage; // rewritten each launch so updates reach existing users
        start = sp;
    }
    navigate_link(start);
    update_state();
    gtk_widget_grab_focus(area_);
}

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);
    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        std::fprintf(stderr, "AtlasLight %s: cannot initialise libcurl\n", kVersion);
        return 1;
    }

    auto* app = new App(); // lives until the process exits
    app->run(argc, argv);
    gtk_main();

    // Download threads may still be running; exit without waiting for them.
    std::fflush(nullptr);
    _exit(0);
}
