# Release Process

## Version Source

Version is defined in:

- `apps/desktop/CMakeLists.txt`

That value feeds:

- application metadata
- About dialog
- Windows executable version info

## Semantic Versioning

- `MAJOR`: breaking changes
- `MINOR`: backward-compatible features
- `PATCH`: backward-compatible fixes

Examples:

- `1.0.0`
- `1.1.0`
- `1.1.1`

## Creating a Release

1. update the version in `apps/desktop/CMakeLists.txt`
2. update `CHANGELOG.md` with a short release summary for user-visible changes only
3. commit the release prep
4. create a matching tag:

```powershell
git tag v1.0.1
git push origin v1.0.1
```

5. GitHub Actions will:
   - build the Windows release artifact
   - package it as `HexMaster-windows-x64-v1.0.1.zip`
   - create or publish the GitHub Release

## Changelog Policy

Keep `CHANGELOG.md` lightweight.

- update it only for release prep or an imminent release
- summarize user-visible changes, not internal implementation churn
- use commit history and GitHub Releases for day-to-day development detail

## GitHub Actions

Workflows:

- `ci.yml`: tests and Windows app build
- `release.yml`: tagged Windows release packaging
- `pages.yml`: GitHub Pages deployment

## Recommended Branching

- use `main` or `master` as the release branch
- merge feature work through pull requests
- let CI validate every push and PR
- only tag commits that are intended to become downloadable releases
