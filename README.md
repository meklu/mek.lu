mek.lu
====

This is the source code that powers the http://mek.lu/ URL shortening
service. The service itself is intended to be a private one, to be used
by the service administrator.

Features
====

* Shorten URLs!
* Simple filesystem layout
    * URLs are stored in a filesystem tree in the configured path
    * `/e/` contains external tree, `/i/` contains internal tree
    * Examples:
        * http://mek.lu/e/aeiou -> `/e/aei/aeiou`
            * `http://www.nasa.gov/moonbasealpha/`
        * http://mek.lu/marvelous -> `/i/mar/marvelous`
            * `http://meklu.org/imgs/gib.svg`

Request Flow
====

1. Client sends a request
2. Server receives the request
3. Simple processing on the GET
    * robots.txt exception: `/robots.txt`
    * Pick filesystem tree: `/e/` for external URLs, `/i/` for base
      service URLs
    * Harsh directory traversal mitigation
        * 400 any silly request
4. Return the fun stuff!
   * 302 the user to the right place
