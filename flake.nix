{
  description = "Motif Desk: a headless music sketchpad with an optional JUCE desktop app";

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
          ];
          guiLibraries = with pkgs; [
            alsa-lib
            fontconfig
            freetype
          ] ++ x11Libraries;
          # The optional JUCE GUI resolves X11 libraries at runtime.
          x11LibraryPath = pkgs.lib.makeLibraryPath x11Libraries;

          package = pkgs.stdenv.mkDerivation {
            pname = "motif-desk";
            version = "0.1.0";
            src = ./.;
            nativeBuildInputs = buildTools;
            cmakeFlags = [ "-DJUCE_SOURCE_DIR=${juceSource}" ];
            meta.mainProgram = "motif-desk";
          };
        in
        {
          inherit package;
          gui = package.overrideAttrs (old: {
            pname = "motif-desk-gui";
            nativeBuildInputs = old.nativeBuildInputs ++ [ pkgs.pkg-config pkgs.makeWrapper ];
            buildInputs = guiLibraries;
            cmakeFlags = old.cmakeFlags ++ [ "-DMOTIF_DESK_BUILD_GUI=ON" ];
            postFixup = ''
              wrapProgram "$out/bin/motif-desk-gui" \
                --suffix LD_LIBRARY_PATH : "${x11LibraryPath}"
            '';
            meta = old.meta // { mainProgram = "motif-desk-gui"; };
          });
          shell = pkgs.mkShell {
            nativeBuildInputs = buildTools;
            JUCE_SOURCE_DIR = juceSource;
          };
          guiShell = pkgs.mkShell {
            nativeBuildInputs = buildTools ++ [ pkgs.pkg-config ];
            buildInputs = guiLibraries;
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
        gui = (projectFor system).gui;
      });
      devShells = forLinux (system: {
        default = (projectFor system).shell;
        gui = (projectFor system).guiShell;
      });
      checks = forLinux (system: {
        headless = (projectFor system).engineCheck;
      });
    };
}
