#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SPEC_FILE="${ROOT_DIR}/redis-dtoe.spec"

if [[ ! -f "${SPEC_FILE}" ]]; then
  echo "spec not found: ${SPEC_FILE}" >&2
  exit 1
fi

NAME="$(awk -F':' '/^Name:/ {gsub(/[[:space:]]+/, "", $2); print $2}' "${SPEC_FILE}")"
VERSION="$(awk -F':' '/^Version:/ {gsub(/[[:space:]]+/, "", $2); print $2}' "${SPEC_FILE}")"

if [[ -z "${NAME}" || -z "${VERSION}" ]]; then
  echo "failed to parse Name/Version from ${SPEC_FILE}" >&2
  exit 1
fi

TARBALL="${NAME}-${VERSION}.tar.gz"
TOPDIR="${ROOT_DIR}/build/rpmbuild"

echo "Packaging ${NAME} version ${VERSION}"

rm -rf "${TOPDIR}"
mkdir -p "${TOPDIR}/SPECS" "${TOPDIR}/SOURCES" "${TOPDIR}/BUILD" "${TOPDIR}/RPMS" "${TOPDIR}/SRPMS"

echo "Creating source tarball: ${TARBALL}"
if command -v git >/dev/null 2>&1 && git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "${ROOT_DIR}" archive --format=tar.gz --prefix "${NAME}-${VERSION}/" -o "${TARBALL}" HEAD
else
  tar -C "${ROOT_DIR}/.." -czf "${TARBALL}" "$(basename "${ROOT_DIR}")"
fi

echo "Copying spec and sources to ${TOPDIR}"
cp -f "${SPEC_FILE}" "${TOPDIR}/SPECS/"
cp -f "${TARBALL}" "${TOPDIR}/SOURCES/"

echo "Building RPM..."
rpmbuild -ba --define "_topdir ${TOPDIR}" "${TOPDIR}/SPECS/$(basename "${SPEC_FILE}")"

echo "Done."
echo "RPMS:  ${TOPDIR}/RPMS"
echo "SRPMS: ${TOPDIR}/SRPMS"
