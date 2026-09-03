# Copr "Build from SCM" -- SRPM build method "make srpm". Copr clones the
# repo, checks out the configured branch/commit, and runs:
#
#   make -f .copr/Makefile srpm outdir="<outdir>" spec="<spec_path>"
#
# as root inside a mock chroot, from the repo root (see PACKAGING.md for the
# exact project settings this expects: Spec File = packaging/coffeerdp.spec,
# Subdirectory = blank). outdir is where the resulting .src.rpm has to land
# for Copr to find it; spec is the path configured in the Copr project (also
# used directly below, so this Makefile doesn't hardcode it twice).
#
# There's no tagged release tarball to fetch (see coffeerdp.spec's Source0
# comment), so this builds one from the exact commit Copr checked out via
# `git archive` instead of downloading anything.
#
# FreeRDP (vendor/freerdp, see FREERDP_VENDORING.md) is a git submodule, and
# `git archive` does NOT include submodule content -- it only archives the
# parent repo's own tracked files (for vendor/freerdp that's just the
# gitlink, an empty directory once extracted). Left as a plain `git archive`
# this would silently ship a tarball with an empty vendor/freerdp/ and fail
# the build the moment CMake tries to add_subdirectory() into it. Instead,
# archive the main repo and each submodule (recursively, in case FreeRDP
# ever gains one of its own) separately via their own `git archive HEAD`,
# extracting each into the same staging tree before repacking it as one
# tarball -- this also means each submodule is captured at exactly the
# commit its parent pins, not whatever ref its own default branch points
# to, keeping the build reproducible.

outdir ?= $(shell pwd)
spec ?= packaging/coffeerdp.spec

.PHONY: srpm
srpm:
	dnf -y install git rpm-build
	$(eval VERSION := $(shell awk '/^Version:/ {print $$2; exit}' $(spec)))
	git submodule update --init --recursive
	rm -rf /tmp/coffeerdp-srpmbuild /tmp/coffeerdp-srpmtree
	mkdir -p /tmp/coffeerdp-srpmbuild/SOURCES /tmp/coffeerdp-srpmtree
	git archive --prefix=coffeerdp-$(VERSION)/ HEAD | tar -x -C /tmp/coffeerdp-srpmtree
	git submodule foreach --recursive '\
	    git archive --prefix=coffeerdp-$(VERSION)/$$displaypath/ HEAD \
	        | tar -x -C /tmp/coffeerdp-srpmtree \
	'
	tar -czf /tmp/coffeerdp-srpmbuild/SOURCES/coffeerdp-$(VERSION).tar.gz \
	    -C /tmp/coffeerdp-srpmtree coffeerdp-$(VERSION)
	rm -rf /tmp/coffeerdp-srpmtree
	rpmbuild \
	    --define "_topdir /tmp/coffeerdp-srpmbuild" \
	    --define "_sourcedir /tmp/coffeerdp-srpmbuild/SOURCES" \
	    --define "_srcrpmdir /tmp/coffeerdp-srpmbuild/SRPMS" \
	    -bs $(spec)
	mkdir -p $(outdir)
	cp /tmp/coffeerdp-srpmbuild/SRPMS/*.src.rpm $(outdir)/
