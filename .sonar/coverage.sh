#!/bin/sh
# Coverage of meta-aurora's Python helpers (recipes-bsp/aurora-boot-images/files/*.py) from tests/meta-aurora
# (pytest on synthetic DTBs, manifests and .ko files). Recipes, shell and perl are not analysed by SonarQube.
# Run by ~/stacks/sonarqube/scan.sh in the image from .sonar/Dockerfile, cwd = the repository root.
# Local run: sh .sonar/coverage.sh (needs python3 with pytest and coverage); report .sonar-reports/coverage.xml.
set -eu
out=.sonar-reports
mkdir -p "$out"
export COVERAGE_FILE="$out/.coverage"
python3 -m coverage run --branch --source=meta-aurora -m pytest -q -p no:cacheprovider tests/meta-aurora
python3 -m coverage xml -o "$out/coverage.xml"
python3 -m coverage report
