#!/ usr / bin / env bash
#M11.2 — package the Linux build into a relocatable tarball.
#
#Builds a Release binary, stages a bundle layout that runs from ANY directory
#(no working - directory assumptions), and tars it:
#
#mtg_cpp - < version> - linux - x86_64 /
#   ├── bin / mtg_cpp #the executable(rpath $ORIGIN /../ lib)
#   ├── lib / #our shared libs + SFML / cpr / curl runtime deps
#   ├── assets / #fonts + icon.svg(found next to the exe)
#   └── share / applications / mtg_cpp.desktop
#
#Usage : bash scripts / package - linux.sh
#Env : MTG_CPP_REPO_DIR overrides the repo location(default below)
#MTG_CPP_SFML_DIR overrides the prebuilt SFML dir(default auto)
#MTG_CPP_DIST_DIR where the tarball lands(default./ dist)

set - euo pipefail

          REPO_DIR =
    "${MTG_CPP_REPO_DIR:-$(cd " $(dirname "${BASH_SOURCE[0]}") /.." && pwd)}" SFML_DIR =
        "${MTG_CPP_SFML_DIR:-/tmp/opencode/sfml}" DIST_DIR = "${MTG_CPP_DIST_DIR:-$REPO_DIR/dist}"

    if[[-d / tmp / opencode / buildenv / bin]];
then export PATH = "/tmp/opencode/buildenv/bin:$PATH" fi cd "$REPO_DIR"

#The project version, from the `project(mtg_cpp VERSION x.y.z` stanza(the
#grep for `VERSION ` alone would also match `cmake_minimum_required`).
    VERSION =
        $(grep - A4 'project(mtg_cpp' CMakeLists.txt | grep - oP 'VERSION \K[0-9.]+' | head - 1)
            BUNDLE_NAME = "mtg_cpp-${VERSION}-linux-x86_64" STAGE =
                "$REPO_DIR/build-release/bundle/$BUNDLE_NAME"

    echo "== mtg_cpp packaging (Linux) ==" echo "version: $VERSION"

# 1. Release build with a relocatable rpath so the bundle works from anywhere.
#The inner single quotes keep $ORIGIN literal when the linker runs under a
#shell, so the recorded RPATH is `$ORIGIN /../ lib` (not an empty $ORIGIN).
    EXTRA_OPTS = (-DCMAKE_BUILD_TYPE =
                      Release '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,' "'" '$ORIGIN/../lib' "'"'' -
                      DMTG_CPP_BUILD_TESTS = OFF) if[[-d "$SFML_DIR"]];
then EXTRA_OPTS += (-DMTG_CPP_SFML_DIR = "$SFML_DIR") fi cmake - S.- B build - release -
                       G Ninja "${EXTRA_OPTS[@]}" >
                   / dev / null cmake-- build build -
                       release-- target mtg_cpp

# 2. Stage the bundle.
                           rm -
                       rf "$STAGE" mkdir -
                       p "$STAGE/bin"
                         "$STAGE/lib"
                         "$STAGE/share/applications" cp "$REPO_DIR/build-release/mtg_cpp"
                         "$STAGE/bin/" cp -
                       r "$REPO_DIR/assets"
                         "$STAGE/"

# 2b. Bundle the local card database when present(the app prefers the
#executable - adjacent data /).Off by default so the tarball stays small in
#dev; a release ships it(or the user runs scripts / fetch_card_db.sh).
                       if[["${MTG_CPP_BUNDLE_DATA:-0}" == "1" && -d "$REPO_DIR/data"]];
then echo "bundling data/ (card database)..." cp - r "$REPO_DIR/data"
                                                     "$STAGE/" fi

# 3. Copy every shared library the binary needs(ours + SFML + cpr / curl) into
#lib /, skipping system libs(glibc etc.).
                                                     copy_runtime_libs() {
  local binary = "$1" while IFS = read - r lib;
  do
  case "$lib" in linux - vdso * | / lib / ld - *| / lib / x86_64 - linux - gnu/*|/usr/lib/*|ld-linux*)
        continue ;; # glibc / system runtime
    esac
    if [[ -f "$lib" ]]; then
      cp -L "$lib" "$STAGE/lib/"
    fi
  done < <(ldd "$binary" | awk '/=>/ {print $3} /^\// {print $1}')
}
copy_runtime_libs "$STAGE/bin/mtg_cpp"
cp "$REPO_DIR"/build-release/libmtg_cpp_*.so "$STAGE/lib/"

# 4. .desktop launcher (freedesktop, references the bundled icon).
cat > "$STAGE/share/applications/mtg_cpp.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=mtg_cpp
Comment=Shandalar-style MTG virtual tabletop
Exec=$PWD/build-release/bundle/$BUNDLE_NAME/bin/mtg_cpp
Icon=$PWD/build-release/bundle/$BUNDLE_NAME/assets/icon.svg
Terminal=false
Categories=Game;BoardGame;
EOF

# 5. A small launcher that makes the bundle self-contained regardless of cwd.
cat > "$STAGE/bin/mtg_cpp.sh" <<'EOF'
#!/usr/bin/env bash
# Self-contained launcher: run the binary with its lib/ on the library path.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$SCRIPT_DIR/mtg_cpp" "$@"
EOF
chmod +x "$STAGE/bin/mtg_cpp.sh"

# 6. Tarball.
mkdir -p "$DIST_DIR"
tar -C "$STAGE/.." -czf "$DIST_DIR/$BUNDLE_NAME.tar.gz" "$BUNDLE_NAME"

echo "== packaged: $DIST_DIR/$BUNDLE_NAME.tar.gz =="
echo "bundle:  $STAGE"
ls -la "$STAGE/bin" "$STAGE/lib" | head -20
