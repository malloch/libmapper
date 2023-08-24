clean-local:
	-@rm -vfr build __pycache__

MOSTLYCLEANFILES = installed_files.log

install-exec-hook:
	if test -n "$(DESTDIR)"; then\
		$(PYTHON) setup.py install --root=$(DESTDIR) --prefix=$(prefix) --record=@top_builddir@/bindings/python/installed_files.log; \
	else \
		$(PYTHON) setup.py install --prefix=$(prefix) --record=@top_builddir@/bindings/python/installed_files.log; \
	fi

uninstall-hook:
	cat @top_builddir@/bindings/python/installed_files.log \
	  | awk '{print "$(DESTDIR)"$$1}' | xargs rm -vf

EXTRA_DIST = src/libmapper/__init__.py tests/test.py tests/testcallbacks.py tests/testconvergent.py \
             tests/testgetvalue.py tests/testinstance.py tests/testmapfromstr.py tests/testnumpy.py \
             tests/testrefcount.py tests/testreverse.py tests/testvector.py tests/tkgui.py

test_all_ordered = test testcallbacks testconvergent testgetvalue testinstance testmapfromstr testnumpy \
             testreverse testvector

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! python3 ./tests/$$i.py; then exit 1; fi; done
