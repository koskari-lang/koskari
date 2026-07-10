#!/bin/sh
# Generate the basic-auth file from env at container start, so no password
# hash is ever committed. Runs from nginx's /docker-entrypoint.d before the
# server starts.
#
# DEMO DEFAULTS: user and pass both fall back to ADMIN -- obvious
# placeholders, not a real credential. Override KOSRUNNER_USER /
# KOSRUNNER_PASS for anything that leaves your laptop.

if ! command -v openssl >/dev/null 2>&1; then
    apk add --no-cache openssl >/dev/null 2>&1 || true
fi

: "${KOSRUNNER_USER:=ADMIN}"
: "${KOSRUNNER_PASS:=ADMIN}"

printf '%s:%s\n' "$KOSRUNNER_USER" "$(openssl passwd -apr1 "$KOSRUNNER_PASS")" \
    > /etc/nginx/.htpasswd

echo "kosrunner: basic-auth user '${KOSRUNNER_USER}' (override KOSRUNNER_USER / KOSRUNNER_PASS)"
