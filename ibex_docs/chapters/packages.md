# Chapter: Packages

Source: [`packages.rst.txt`](../../../ibex-docs/_sources/packages.rst.txt) ·
`packages.html`. Building **distributable binary packages** — `make package` from the
build dir emits `.deb`/`.tar.gz`/`.zip`. Developer-facing; skip if you consume IBEX as
a library or binary. (This is *not* the plugin/extension mechanism — that is
[plugins-dev](plugins-dev.md).)

> **dReal status:** not used — dReal consumes the fork as a static lib via
> ExternalProject ([install-cmake](install-cmake.md)), never `make package`, and
> builds **no plugins**. The affine-linearization idea (`LinearizerAffine2`) is a
> *plugin* ([plugins-dev](plugins-dev.md)) shipping in the `ibex-affine` package —
> absent from the fork, so unbuilt (see [reference](reference.md),
> [`../AUDIT.md`](../AUDIT.md) D).
