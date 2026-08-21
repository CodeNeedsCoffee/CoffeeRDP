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

outdir ?= $(shell pwd)
spec ?= packaging/coffeerdp.spec

.PHONY: srpm
srpm:
	dnf -y install git rpm-build
	$(eval VERSION := $(shell awk '/^Version:/ {print $$2; exit}' $(spec)))
	rm -rf /tmp/coffeerdp-srpmbuild
	mkdir -p /tmp/coffeerdp-srpmbuild/SOURCES
	git archive --prefix=coffeerdp-$(VERSION)/ HEAD \
	    -o /tmp/coffeerdp-srpmbuild/SOURCES/coffeerdp-$(VERSION).tar.gz
	rpmbuild \
	    --define "_topdir /tmp/coffeerdp-srpmbuild" \
	    --define "_sourcedir /tmp/coffeerdp-srpmbuild/SOURCES" \
	    --define "_srcrpmdir /tmp/coffeerdp-srpmbuild/SRPMS" \
	    -bs $(spec)
	mkdir -p $(outdir)
	cp /tmp/coffeerdp-srpmbuild/SRPMS/*.src.rpm $(outdir)/
