function add_suite(suite, initialize, cleanup, count) {
       if (!suite) return
       suite_count++
       callback_count += count
       suites = suites "    {\n"
       suites = suites "        \"" suite "\",\n"
       suites = suites "        " initialize ",\n"
       suites = suites "        " cleanup ",\n"
       suites = suites "        _clar_cb_" suite ", " count ", 1\n"
       suites = suites "    },\n"
}

BEGIN {
       suites = "static struct clar_suite _clar_suites[] = {\n"
}

{
       print
       name = $3; sub(/\(.*$/, "", name)
       suite = name; sub(/^test_/, "", suite); sub(/__.*$/, "", suite)
       short_name = name; sub(/^.*__/, "", short_name)
       cb = "{ \"" short_name "\", &" name " }"
       if (suite != prev_suite) {
               add_suite(prev_suite, initialize, cleanup, count)
               if (callbacks) callbacks = callbacks "};\n"
               callbacks = callbacks "static const struct clar_func _clar_cb_" suite "[] = {\n"
               initialize = "{ NULL, NULL }"
               cleanup = "{ NULL, NULL }"
               count = 0
               prev_suite = suite
       }
       if (short_name == "initialize") {
               initialize = cb
       } else if (short_name == "cleanup") {
               cleanup = cb
       } else {
               callbacks = callbacks "    " cb ",\n"
               count++
       }
}

END {
       add_suite(suite, initialize, cleanup, count)
       suites = suites "};"
       if (callbacks) callbacks = callbacks "};"
       print callbacks
       print suites
       print "static const size_t _clar_suite_count = " suite_count ";"
       print "static const size_t _clar_callback_count = " callback_count ";"
}
