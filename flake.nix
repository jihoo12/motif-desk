{
  description = "Motif Desk: a small JUCE desktop music sketchpad";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";

  outputs =
    { nixpkgs, ... }:
    let
      forLinux = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
      projectFor =
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          juceSource = pkgs.fetchFromGitHub {
            owner = "juce-framework";
            repo = "JUCE";
            tag = "8.0.12";
            hash = "sha256-mq7lpPHbb1uF3o50/UZY9LiT81ACAk9ptHQ98fhdk1Q=";
          };
          x11Libraries = with pkgs; [
            libx11
            libxcomposite
            libxcursor
            libxext
            libxinerama
            libxrandr
            libxrender
          ];
          buildTools = with pkgs; [
            cmake
            ninja
            pkg-config
          ];
          libraries = with pkgs; [
            alsa-lib
            fontconfig
            freetype
          ] ++ x11Libraries;
          # JUCE opens several X11 libraries dynamically instead of linking them.
          x11LibraryPath = pkgs.lib.makeLibraryPath x11Libraries;

          package = pkgs.stdenv.mkDerivation {
            pname = "motif-desk";
            version = "0.1.0";
            src = ./.;
            nativeBuildInputs = buildTools ++ [ pkgs.makeWrapper ];
            buildInputs = libraries;
            cmakeFlags = [ "-DJUCE_SOURCE_DIR=${juceSource}" ];
            postFixup = ''
              wrapProgram "$out/bin/motif-desk" \
                --suffix LD_LIBRARY_PATH : "${x11LibraryPath}"
            '';
            meta.mainProgram = "motif-desk";
          };
        in
        {
          inherit package;
          shell = pkgs.mkShell {
            nativeBuildInputs = buildTools;
            buildInputs = libraries;
            JUCE_SOURCE_DIR = juceSource;
            shellHook = ''
              export LD_LIBRARY_PATH="${x11LibraryPath}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            '';
          };
          engineCheck = package.overrideAttrs (old: {
            cmakeFlags = old.cmakeFlags ++ [ "-DBUILD_TESTING=ON" ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              ctest --output-on-failure
              runHook postCheck
            '';
          });
        };
    in
    {
      packages = forLinux (system: {
        default = (projectFor system).package;
      });
      devShells = forLinux (system: {
        default = (projectFor system).shell;
      });
      checks = forLinux (system: {
        engine = (projectFor system).engineCheck;
      });
    };
}
