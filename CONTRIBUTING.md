# Contribution Guidelines

Thank you for your interest in these ports. Bug reports, fixes and test
results are welcome, especially from watches and hardware revisions that have
not been tested yet (see the [README](README.md#notes-per-watch)).

## Where to contribute

- **Bugs in these ports:** open an
  [issue](https://github.com/bjsilvest/PebbleOS-legacy-watches/issues).
  Include the watch model and revision, the firmware build (for example
  v4.44.0-legacy.1), the phone and its OS, and steps to reproduce.
- **Bugs that also happen on Core's own watches:** report them to
  [Core Devices](https://github.com/coredevices/PebbleOS). This repository
  only takes Core's tagged releases, so fixes to Core's code are picked up
  from there.
- **Pull requests:** open them against `legacy` (Pebble Time, Pebble Time
  Steel, Pebble Time Round, Pebble 2) or `legacy-lite` (Pebble, Pebble
  Steel). The `main` branch is Core's code from when this repository was
  made and only carries the front page; the ports are not built from it.

Please keep changes to the ports themselves. Changes to Core's code are only
taken when a port needs them, so that following Core's releases stays easy.

## Testing

Every change must be tested on real hardware for each watch it affects. In
the pull request, list the watches and hardware revisions you tested on and
what you checked. The Pebble and Pebble Steel builds are close to their flash
limit, so mention the size change for `legacy-lite`.

## Developer Certificate of Origin (DCO)

Every commit must carry a `Signed-off-by:` line, which certifies that you
have the right to submit it under the project's license, as set out in the
[Developer Certificate of Origin 1.1](https://developercertificate.org/):

```
Signed-off-by: Your Name <your.email@example.com>
```

Use a known identity and the same email address you author the commit with.
`git commit -s` adds the line for you. If you change a commit made by someone
else, add your line without removing theirs.

## Commit Requirements

- Each commit is one self-contained logical change.
- The subject is in the form "area: description", for example
  "drivers: fix compass standby on tintin".
- Reference the issue a commit fixes, if there is one (e.g. "Fixes #12").

## Generative AI usage

LLM coding tools may be used, provided that you have reviewed and understood
all of the generated content, tested it on hardware as described above and
have the right to submit it. Disclose their use with an `Assisted-by: LLM`
line in each affected commit, next to your sign-off. All submitted text must
be in English, and AI-generated images, icons or other artwork are not
accepted.
