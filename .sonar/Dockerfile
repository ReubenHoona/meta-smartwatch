# Image for .sonar/coverage.sh: ~/stacks/sonarqube/scan.sh builds it (context: this .sonar/ directory only) and runs
# the script in it as the scanning user: Debian's python3 with pytest and coverage. The base image is pinned;
# nothing is downloaded when the coverage runs.
FROM debian:trixie-20261005-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-pytest python3-coverage \
    && rm -rf /var/lib/apt/lists/*
