#include "../src/adblock.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond)                                                       \
    do                                                                    \
    {                                                                     \
        if(!(cond))                                                       \
        {                                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);           \
            failures++;                                                   \
        }                                                                 \
    } while(0)

int main()
{
    atlas::adblock ab;
    CHECK(ab.add_rule("doubleclick.net"));
    CHECK(ab.add_rule("0.0.0.0 ads.example.com"));
    CHECK(ab.add_rule("||tracker.test^"));
    CHECK(ab.add_rule("/banner/ads/"));
    CHECK(ab.add_rule("@@good.doubleclick.net"));
    CHECK(!ab.add_rule("# comment"));
    CHECK(!ab.add_rule("! comment"));
    CHECK(!ab.add_rule("   "));
    CHECK(!ab.add_rule("127.0.0.1 localhost"));

    CHECK(ab.blocked("http://doubleclick.net/x.js"));
    CHECK(ab.blocked("https://static.DoubleClick.net/ad.gif"));       // subdomain + case
    CHECK(!ab.blocked("https://good.doubleclick.net/ok.js"));         // allow-list wins
    CHECK(ab.blocked("http://ads.example.com/p.png"));
    CHECK(!ab.blocked("http://example.com/index.html"));              // parent not blocked
    CHECK(ab.blocked("http://a.b.tracker.test:8080/pixel?x=1"));      // port + deep subdomain
    CHECK(ab.blocked("http://site.org/banner/ads/top.png"));          // substring
    CHECK(!ab.blocked("http://notdoubleclick.net/"));                 // not a suffix at a dot boundary
    CHECK(!ab.blocked("http://site.org/page.html"));
    CHECK(!ab.blocked("http://user:pw@site.org/ads.html"));
    CHECK(ab.blocked("http://user:pw@tracker.test/p"));               // userinfo skipped

    CHECK(atlas::adblock::host_of("HTTP://Example.COM:80/a?b#c") == "example.com");
    CHECK(atlas::adblock::host_of("//cdn.x.com/a.js") == "cdn.x.com");
    CHECK(ab.blocked_count() == 6); // doubleclick, static.doubleclick, ads.example, tracker, banner, user@tracker

    std::printf(failures ? "FAILED (%d)\n" : "adblock tests passed\n", failures);
    return failures ? 1 : 0;
}
