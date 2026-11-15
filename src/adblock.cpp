#include "adblock.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    std::string trim(const std::string& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while(b < e && std::isspace((unsigned char) s[b]))
        {
            b++;
        }
        while(e > b && std::isspace((unsigned char) s[e - 1]))
        {
            e--;
        }
        return s.substr(b, e - b);
    }

    // True if the text looks like a bare hostname (letters, digits, '.', '-').
    bool looks_like_domain(const std::string& s)
    {
        if(s.empty() || s.find('.') == std::string::npos)
        {
            return false;
        }
        return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; });
    }
} // namespace

std::string atlas::adblock::host_of(const std::string& url)
{
    size_t start = url.find("://");
    if(start != std::string::npos)
    {
        start += 3;
    } else if(url.rfind("//", 0) == 0) // protocol-relative: //host/path
    {
        start = 2;
    } else
    {
        start = 0;
    }
    // Skip "user:pass@"
    size_t end_auth = url.find_first_of("/?#", start);
    if(end_auth == std::string::npos)
    {
        end_auth = url.size();
    }
    size_t at = url.rfind('@', end_auth);
    if(at != std::string::npos && at >= start && at < end_auth)
    {
        start = at + 1;
    }
    size_t end = url.find_first_of(":/?#", start);
    if(end == std::string::npos)
    {
        end = url.size();
    }
    return lower(url.substr(start, end - start));
}

bool atlas::adblock::add_rule(const std::string& raw)
{
    std::string line = trim(raw);
    if(line.empty() || line[0] == '#' || line[0] == '!')
    {
        return false;
    }
    // Strip trailing comment
    size_t hash = line.find(" #");
    if(hash != std::string::npos)
    {
        line = trim(line.substr(0, hash));
    }

    bool allow = false;
    if(line.rfind("@@", 0) == 0)
    {
        allow = true;
        line  = trim(line.substr(2));
    }

    // Adblock-style ||domain^
    if(line.rfind("||", 0) == 0)
    {
        line = line.substr(2);
        size_t caret = line.find_first_of("^/$");
        if(caret != std::string::npos)
        {
            line = line.substr(0, caret);
        }
    } else
    {
        // hosts-file style: "0.0.0.0 domain" or "127.0.0.1 domain"
        size_t sp = line.find_first_of(" \t");
        if(sp != std::string::npos)
        {
            std::string first = line.substr(0, sp);
            if(first == "0.0.0.0" || first == "127.0.0.1" || first == "::1" || first == "::")
            {
                line = trim(line.substr(sp));
            }
        }
    }

    line = lower(line);
    if(line.empty() || line == "localhost")
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if(looks_like_domain(line))
    {
        (allow ? m_allow_domains : m_block_domains).insert(line);
    } else if(!allow && line.size() >= 4)
    {
        m_substrings.push_back(line);
    } else
    {
        return false;
    }
    return true;
}

size_t atlas::adblock::load_file(const std::string& path)
{
    std::ifstream in(path);
    if(!in)
    {
        return 0;
    }
    size_t      added = 0;
    std::string line;
    while(std::getline(in, line))
    {
        if(add_rule(line))
        {
            added++;
        }
    }
    return added;
}

size_t atlas::adblock::rule_count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_block_domains.size() + m_allow_domains.size() + m_substrings.size();
}

// Checks host and every parent domain ("a.b.example.com" -> "b.example.com" -> "example.com").
bool atlas::adblock::domain_in(const std::unordered_set<std::string>& set, const std::string& host) const
{
    size_t pos = 0;
    while(pos < host.size())
    {
        if(set.count(host.substr(pos)))
        {
            return true;
        }
        size_t dot = host.find('.', pos);
        if(dot == std::string::npos)
        {
            break;
        }
        pos = dot + 1;
    }
    return false;
}

bool atlas::adblock::blocked(const std::string& url) const
{
    const std::string host  = host_of(url);
    const std::string lurl  = lower(url);
    bool              block = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!host.empty() && domain_in(m_allow_domains, host))
        {
            return false;
        }
        if(!host.empty() && domain_in(m_block_domains, host))
        {
            block = true;
        } else
        {
            for(const auto& sub : m_substrings)
            {
                if(lurl.find(sub) != std::string::npos)
                {
                    block = true;
                    break;
                }
            }
        }
    }
    if(block)
    {
        m_blocked_count++;
    }
    return block;
}
