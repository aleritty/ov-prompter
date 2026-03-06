PREFIX ?= /usr/local
DESTDIR ?=
BINDIR := $(DESTDIR)$(PREFIX)/bin
LIBDIR := $(DESTDIR)$(PREFIX)/lib/ov-prompter
DOCDIR := $(DESTDIR)$(PREFIX)/share/doc/ov-prompter
MANDIR := $(DESTDIR)$(PREFIX)/share/man/man1

ifeq ($(shell id -u),0)
SUDO ?=
else
SUDO ?= sudo
endif

BUILD_OUTPUT := build/prompter

.PHONY: all build clean install uninstall ensure-build

all: build

build:
	./build.sh

clean:
	rm -rf build libs

ensure-build:
	@if [ ! -x "$(BUILD_OUTPUT)" ]; then \
		echo "Error: $(BUILD_OUTPUT) not found. Run 'make build' as a normal user before installing." >&2; \
		exit 1; \
	fi

install: ensure-build
	@echo "Installing into $(DESTDIR)$(PREFIX)"
	$(SUDO) install -d $(BINDIR) $(LIBDIR) $(DOCDIR) $(MANDIR)
	$(SUDO) install -m755 $(BUILD_OUTPUT) $(BINDIR)/ov-prompter
	$(SUDO) cp -a libs/. $(LIBDIR)/
	$(SUDO) install -m644 README.md $(DOCDIR)/README.md
	$(SUDO) install -m644 src/ov-prompter.1 $(MANDIR)/ov-prompter.1
	@if command -v gzip >/dev/null 2>&1; then \
		$(SUDO) gzip -f $(MANDIR)/ov-prompter.1; \
	fi
	@if command -v patchelf >/dev/null 2>&1; then \
		$(SUDO) patchelf --set-rpath '$(PREFIX)/lib/ov-prompter' $(BINDIR)/ov-prompter; \
	else \
		echo "Warning: patchelf not found; set LD_LIBRARY_PATH to $(PREFIX)/lib/ov-prompter" >&2; \
	fi

uninstall:
	@echo "Removing files under $(DESTDIR)$(PREFIX)"
	$(SUDO) rm -f $(BINDIR)/ov-prompter
	$(SUDO) rm -rf $(LIBDIR)
	$(SUDO) rm -rf $(DOCDIR)
	$(SUDO) rm -f $(MANDIR)/ov-prompter.1.gz $(MANDIR)/ov-prompter.1
	-$(SUDO) rmdir --ignore-fail-on-non-empty $(BINDIR) $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/share/doc 2>/dev/null || true
	-$(SUDO) rmdir --ignore-fail-on-non-empty $(MANDIR) $(DESTDIR)$(PREFIX)/share/man 2>/dev/null || true
	-$(SUDO) rmdir --ignore-fail-on-non-empty $(DESTDIR)$(PREFIX)/share 2>/dev/null || true
