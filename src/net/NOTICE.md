# Vendored litehtml support code

The files in this folder (`web_page.*`, `http_request.*`, `http_requests_pool.*`,
`html_host.h`) are copied from litehtml's `support/webpage` directory
(https://github.com/litehtml/litehtml, commit ebb9d32b) and are licensed under
the BSD 3-clause licence of litehtml (see `LICENSE-litehtml` in the repo root).
Copyright (c) 2013, Yuri Kobets (tordex).

AtlasLight changes:
- TLS certificate verification is turned ON (upstream disabled it).
- Redirects are limited to http/https.
- `web_page::set_content_filter()` lets the host block CSS/image requests (ad blocker).
- Remote pages cannot load `file://` resources.
- `web_page::set_user_css()` passes user styles (dark mode) to the document.
