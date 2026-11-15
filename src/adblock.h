// AtlasLight ad blocker: tiny, dependency-free, thread-safe.
//
// Rule formats (one per line, '#' and '!' start comments):
//   example.com                 block example.com and all its subdomains
//   0.0.0.0 ads.example.com     hosts-file style (first token is the IP, ignored)
//   ||ads.example.com^          Adblock-style domain rule
//   /banner/ads/                any other text = block URLs containing it
//   @@example.com               allow-list: never block this domain
#ifndef ATLAS_ADBLOCK_H
#define ATLAS_ADBLOCK_H

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace atlas
{
    class adblock
    {
      public:
        // Adds one rule line. Returns true if the line produced a rule.
        bool add_rule(const std::string& line);

        // Loads rules from a file. Returns the number of rules added (0 if missing).
        size_t load_file(const std::string& path);

        // Should this URL be blocked?
        bool blocked(const std::string& url) const;

        size_t blocked_count() const
        {
            return m_blocked_count.load();
        }
        size_t rule_count() const;

        // Extracts the lower-case host from a URL ("" if none). Public for tests.
        static std::string host_of(const std::string& url);

      private:
        bool domain_in(const std::unordered_set<std::string>& set, const std::string& host) const;

        mutable std::mutex              m_mutex;
        std::unordered_set<std::string> m_block_domains;
        std::unordered_set<std::string> m_allow_domains;
        std::vector<std::string>        m_substrings;
        mutable std::atomic<size_t>     m_blocked_count{0};
    };
} // namespace atlas

#endif
