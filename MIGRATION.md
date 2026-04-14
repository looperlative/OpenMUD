# Migrating to the lib / lib-run split

This repo now separates distribution data (`lib/`, versioned) from
runtime data (`lib-run/`, gitignored). The server reads and writes
`lib-run/` exclusively. `lib/` is a pristine copy of the shipped
distribution and is overwritten on `git pull`.

## Fresh install

```
./configure
make
./bootstrap.sh     # copies lib/ to lib-run/
bin/circle         # default -d is lib-run
```

Edit tunables in `lib-run/etc/config` (copied from `lib/etc/config` by
bootstrap). Absent keys fall back to the compiled-in defaults from
`src/config.c`.

## Upgrading an existing install

Before pulling, move your live runtime files out of `lib/` and into
`lib-run/`. One-shot migration on a server that has been running out of
`lib/`:

```
cp -a lib lib-run
cd lib && git clean -fdX && git checkout HEAD -- .
```

This leaves `lib/` matching the repo's distribution and `lib-run/` with
all player data, player objects, aliases, boards, house data, mail,
and log pointers intact. Then:

```
git pull
./configure   # only if autoconf inputs changed
make
bin/circle    # now reads lib-run/
```

Optional: copy `lib/etc/config` to `lib-run/etc/config` and edit it in
place. If the file is absent, the server uses compiled defaults.

## Inspecting local drift

```
./libdiff.sh
```

Reports files that differ between `lib/` (distribution) and `lib-run/`
(runtime), excluding known runtime-only paths (player files, boards,
house data, aliases, rented objects, mudwho cache, the config file).
A freshly bootstrapped tree should report nothing.
