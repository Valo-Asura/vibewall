{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland,
  wayland-protocols,
  wayland-scanner,
  wlr-protocols,
  libxkbcommon,
  sqlite,
  curl,
  vips,
  ffmpeg,
  tomlplusplus,
  nlohmann_json,
  libGL,
  libepoxy,
  makeWrapper,
  mpvpaper,
  matugen,
  procps,
  systemd,
}:

stdenv.mkDerivation {
  pname = "vibewallREzero";
  version = "0.1.0";
  src = lib.cleanSourceWith {
    src = ../.;
    filter =
      path: type:
      let
        base = baseNameOf path;
      in
      !(base == "build" || base == ".git" || lib.hasSuffix ".o" base);
  };

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    makeWrapper
  ];

  doCheck = true;

  buildInputs = [
    wayland
    wayland-protocols
    wlr-protocols
    libxkbcommon
    sqlite
    curl
    vips
    ffmpeg
    tomlplusplus
    nlohmann_json
    libGL
    libepoxy
  ];

  postInstall = ''
    for bin in vibewall vibewall-daemon vibewall-picker; do
      wrapProgram "$out/bin/$bin" \
        --prefix PATH : "$out/bin:${
          lib.makeBinPath [
            ffmpeg
            matugen
            mpvpaper
            procps
            systemd
          ]
        }"
    done
    chmod +x "$out/share/vibewallREzero/scripts/benchmark.sh"
    mkdir -p "$out/bin"
    ln -sf "$out/share/vibewallREzero/scripts/benchmark.sh" "$out/bin/vibewall-benchmark"
  '';

  meta = {
    description = "Native Wayland wallpaper selector inspired by skwd-wall";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "vibewall";
  };
}
