#!/bin/sh
set -eu

root=$(mktemp -d /tmp/cbs-install-test.XXXXXX)
trap 'rm -rf "$root"' EXIT

make -o version-check DESTDIR="$root" PREFIX=/usr install >/dev/null
test -f "$root/usr/bin/cbs"
test -f "$root/usr/lib/libcbs.a"
test -f "$root/usr/include/cbs/cbs.h"
test -f "$root/usr/lib/pkgconfig/cbs.pc"
! grep -q '^struct CbsNode {' "$root/usr/include/cbs/cbs.h"
! grep -q 'cbs_parse' "$root/usr/include/cbs/cbs.h"

capabilities=$("$root/usr/bin/cbs" --capabilities)
printf '%s\n' "$capabilities" | grep -q '"schema":"cbs.capabilities/v1"'
printf '%s\n' "$capabilities" | grep -q '"api_version":1'

pkg_config=$(PKG_CONFIG_PATH="$root/usr/lib/pkgconfig" \
    pkg-config --define-prefix --cflags --libs --static cbs)
printf '%s\n' "$pkg_config" | grep -q -- '-I'
printf '%s\n' "$pkg_config" | grep -q -- '-lcbs'

cat >"$root/consumer.c" <<'EOF'
#include <stdio.h>
#include <cbs/cbs.h>

int main(void) {
    CbsBuildEvent event = {0};
    CbsBuildReport report;
    FILE *stream;
    if (cbs_api_version() != CBS_API_VERSION ||
        cbs_abi_version() != CBS_ABI_VERSION ||
        cbs_execution_context_size() != sizeof(CbsExecutionContext) ||
        sizeof(event) == 0 || sizeof(report) == 0 || cbs_version()[0] == '\0')
        return 1;
    cbs_build_report_init(&report);
    event.version = 1;
    event.type = "build-begin";
    if (!cbs_build_report_consume(&event, &report))
        return 1;
    stream = tmpfile();
    if (stream == NULL || !cbs_build_report_write_json(&report, stream))
        return 1;
    fclose(stream);
    puts("consumer-ok");
    return 0;
}
EOF
${CC:-tcc} $(PKG_CONFIG_PATH="$root/usr/lib/pkgconfig" \
    pkg-config --define-prefix --cflags cbs) \
    "$root/consumer.c" \
    $(PKG_CONFIG_PATH="$root/usr/lib/pkgconfig" \
        pkg-config --define-prefix --libs --static cbs) \
    -o "$root/consumer"
test "$("$root/consumer")" = consumer-ok
echo 'install contract tests: PASS (staged pkg-config, capabilities, and consumer ABI guard)'
