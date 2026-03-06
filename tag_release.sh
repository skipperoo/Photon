#!/bin/bash

# Usage: ./tag_release.sh [patch|minor|major]

if [ -z "$1" ]; then
    VERSION_TYPE="patch"
else
    VERSION_TYPE="$1"
fi

# Get current version from git tags
CURRENT_VERSION=$(git describe --tags --abbrev=0 2>/dev/null || echo "v0.1.0")
# Remove 'v' prefix
CURRENT_VERSION=${CURRENT_VERSION#v}

# Split version into components
IFS='.' read -r -a parts <<< "$CURRENT_VERSION"
MAJOR=${parts[0]}
MINOR=${parts[1]}
PATCH=${parts[2]}

if [ "$VERSION_TYPE" == "major" ]; then
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
elif [ "$VERSION_TYPE" == "minor" ]; then
    MINOR=$((MINOR + 1))
    PATCH=0
else
    PATCH=$((PATCH + 1))
fi

NEW_VERSION="v$MAJOR.$MINOR.$PATCH"
CMAKE_VERSION="$MAJOR.$MINOR.$PATCH"

echo "Bumping version from v$CURRENT_VERSION to $NEW_VERSION"

# Update CMake project version line
if ! sed -i -E "s/^project\\(Photon VERSION [0-9]+\\.[0-9]+\\.[0-9]+ LANGUAGES CXX\\)$/project(Photon VERSION ${CMAKE_VERSION} LANGUAGES CXX)/" CMakeLists.txt; then
    echo "Failed to update CMakeLists.txt project version."
    exit 1
fi

# Create and push tag
git tag -a "$NEW_VERSION" -m "Release $NEW_VERSION"
git push origin "$NEW_VERSION"

echo "Tag $NEW_VERSION pushed to origin."
