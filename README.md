mek.lu
====

This is the source code that powers the http://mek.lu/ URL shortening
service. The service itself is intended to be a private one, to be used
by the service administrator.

License
====

The source code is licensed CC0. This means you can do practically anything
you want with the source code. The full text of the license can be found in
the `COPYING.txt` file in the repository root or online at
https://creativecommons.org/publicdomain/zero/1.0/

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

Distinction between URL types
====

The difference between external URLs and base service URLs is mostly
semantic. An upcoming URL insertion utility, however, could be configured
to automatically insert URLs with certain host parts into the internal
tree.

Request Flow
====

1. Client sends a request
2. Server receives the request
3. Simple processing on the GET
    * Index exception: `/` -> `/index.html`
        * Notable feature: a GET request for `/index.html` will still be
          interpreted as a regular request, and will hence be read from
          `/i/ind/index.html`. Putting an actual HTML file in this path
          will merely output the first line in the file - usually the
          DOCTYPE declaration - as the Location header, and you probably
          wouldn't want that to happen.
    * robots.txt exception: `/robots.txt`
    * Pick filesystem tree: `/e/` for external URLs, `/i/` for base
      service URLs
    * Harsh directory traversal mitigation
        * 400 any silly request
4. Return the fun stuff!
   * 302 the user to the right place
