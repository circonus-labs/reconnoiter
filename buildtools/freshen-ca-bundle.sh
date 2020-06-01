#!/bin/bash
#
# Updates the built-in CA root cert bundle such that noitd/stratcond do not
# need to rely on the OS's (potentially old/stale) bundle.
#
# Uses cURL's Mozilla-sourced bundle: https://curl.haxx.se/docs/caextract.html
#
# This should get run at least quarterly by a developer, or whenever an update
# is deemed necessary, and the result committed. It needs to be run within a
# git checkout of the Reconnoiter source.

MYDIR="$PWD/`dirname $BASH_SOURCE[0]`"
outfile="$MYDIR/../src/default-ca-chain.crt"

bail() {
    printf "Error: %s\n" "$@"
    exit 1
}

cacert_file="cacert.pem"
cksum_file="cacert.pem.sha256"
cacert_pem_url="https://curl.haxx.se/ca/$cacert_file"
cacert_pem_cksum="https://curl.haxx.se/ca/$cksum_file"

echo "Fetching PEM bundle"
curl -s -O $cacert_pem_url || bail "failed to download $cacert_pem_url"

echo "Fetching PEM checksum"
curl -s -O $cacert_pem_cksum || bail "failed to download $cacert_pem_cksum"

echo "Verifying bundle checksum"
sha256sum -c $cksum_file || bail "checksum of $cacert_file does not match"

echo "Moving bundle into place"
mv $cacert_file $outfile || bail "failed to place $cacert_file as $outfile"

# Check whether it updated anything
git diff-index --quiet HEAD
case $? in
    0) echo "No update detected, nothing to commit." ;;
    1) echo "Please commit the updated file." ;;
esac

rm $cksum_file || bail "Failed to clean up $cksum_file"
exit 0
